/*
 * cache.c
 */


#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE; 
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static cache icache;
static cache dcache;
static cache unified_cache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value)
  int param;
  int value;
{

  switch (param) {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }

}
/************************************************************/

/************************************************************/
void perform_init(c, size)
  Pcache c;
  int size;
{
  /* the body of initialization */
  int set_bits, offset_bits, i;
  c->size = size/WORD_SIZE;
  c->associativity = cache_assoc;
  c->n_sets = c->size/(c->associativity*words_per_block);
  set_bits = LOG2(c->n_sets);
  offset_bits = LOG2(cache_block_size);
  c->index_mask = ((1<<set_bits)-1)<<offset_bits;
  c->index_mask_offset = offset_bits;
  c->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line)*c->n_sets);
  c->LRU_tail = (Pcache_line *)malloc(sizeof(Pcache_line)*c->n_sets);
  c->set_contents = (int *)malloc(sizeof(int)*c->n_sets);
    for(i=0;i<c->n_sets;i++)
  {
    c->LRU_head[i] = NULL;
    c->LRU_tail[i] = NULL;
    c->set_contents[i] = 0;
  }
}
/************************************************************/

/************************************************************/
void init_cache()
{

  /* initialize the cache, and cache statistics data structures */
  if(cache_split){
    perform_init(&icache,cache_isize);
    perform_init(&dcache,cache_dsize);
  }
    else
      perform_init(&unified_cache,cache_usize);


  cache_stat_data.accesses = 0;
  cache_stat_data.copies_back = 0;
  cache_stat_data.demand_fetches = 0;
  cache_stat_data.misses = 0;
  cache_stat_data.replacements = 0;
  cache_stat_inst.accesses = 0;
  cache_stat_inst.copies_back = 0;
  cache_stat_inst.demand_fetches = 0;
  cache_stat_inst.misses = 0;
  cache_stat_inst.replacements = 0;
}
/************************************************************/

/************************************************************/
void data_load(c, index, tag)
  Pcache c;
  int index, tag;
{
  cache_stat_data.accesses++;

  if(c->LRU_head[index] == NULL){// compulsory miss
    cache_stat_data.misses++;
    cache_stat_data.demand_fetches += words_per_block;
    c->set_contents[index]++;
    Pcache_line new_line = malloc(sizeof(cache_line));
    new_line->tag = tag;
    new_line->dirty = 0;
    insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
  }

  else{
      int i, hit;
      Pcache_line temp_line = c->LRU_head[index];
      hit = 0;
      for(i=0;i<c->set_contents[index];i++)
      {
        if(tag == temp_line->tag){
          hit = 1;
          break;
        }
        temp_line = temp_line->LRU_next;
      }
      if(hit){// hit
        delete(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
        insert(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
      }
      else{// miss
        cache_stat_data.misses++;
        cache_stat_data.demand_fetches += words_per_block;
        Pcache_line new_line = malloc(sizeof(cache_line));
        new_line->tag = tag;
        new_line->dirty = 0;
        if(c->set_contents[index] < c->associativity){
          c->set_contents[index]++;
          insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);          
        }
        else{
          cache_stat_data.replacements++;
          if(c->LRU_tail[index]->dirty){
            cache_stat_data.copies_back += words_per_block;
          }
          delete(&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
          insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
        }

      }
  }
}
/************************************************************/



/************************************************************/
void data_store(c, index, tag)
  Pcache c;
  int index, tag;
{
  cache_stat_data.accesses++;
  if(c->LRU_head[index] == NULL){// compulsory miss
    cache_stat_data.misses++;
    if(cache_writealloc){
    cache_stat_data.demand_fetches += words_per_block;
    c->set_contents[index]++;
    Pcache_line new_line = malloc(sizeof(cache_line));
    new_line->tag = tag;
    new_line->dirty = 1;
    if(cache_writeback == 0){
      new_line->dirty = 0;
      cache_stat_data.copies_back++;
    }
    insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
    }
    else{
      cache_stat_data.copies_back++;
    }
  }

  else{
      int i, hit;
      Pcache_line temp_line = c->LRU_head[index];
      hit = 0;
      for(i=0;i<c->set_contents[index];i++)
      {
        if(tag == temp_line->tag){
          hit = 1;
          break;
        }
        temp_line = temp_line->LRU_next;
      }
      if(hit){//hit
        delete(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
        insert(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
        c->LRU_head[index]->dirty = 1;
        if(cache_writeback == 0){//write through
          c->LRU_head[index]->dirty = 0;
          cache_stat_data.copies_back++;
        }
      }
      else{//miss
        cache_stat_data.misses++;
        if(cache_writealloc)
        {//write allocate
          cache_stat_data.demand_fetches += words_per_block;
          Pcache_line new_line = malloc(sizeof(cache_line));
          new_line->tag = tag;
          new_line->dirty = 1;
          if(cache_writeback==0){
            cache_stat_data.copies_back++;
            new_line->dirty = 0;
          }
          if(c->set_contents[index] < c->associativity){
            insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
            c->set_contents[index]++;
          }
          else{
            cache_stat_data.replacements++;
            if(c->LRU_tail[index]->dirty){
              cache_stat_data.copies_back += words_per_block;
            }
          delete(&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
          insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
          }
        }
        else{
          cache_stat_data.copies_back++;
        }
  }
 }
}
/************************************************************/


/************************************************************/
void instruction_load(c, index, tag)
  Pcache c;
  int index, tag;
{
  cache_stat_inst.accesses++;

  if(c->LRU_head[index] == NULL){// compulsory miss
    cache_stat_inst.misses++;
    cache_stat_inst.demand_fetches += words_per_block;
    c->set_contents[index]++;
    Pcache_line new_line = malloc(sizeof(cache_line));
    new_line->tag = tag;
    new_line->dirty = 0;
    insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
  }

  else{
      int i, hit;
      Pcache_line temp_line = c->LRU_head[index];
      hit = 0;
      for(i=0;i<c->set_contents[index];i++)
      {
        if(tag == temp_line->tag){
          hit = 1;
          break;
        }
        temp_line = temp_line->LRU_next;
      }
      if(hit){// hit
        delete(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
        insert(&c->LRU_head[index], &c->LRU_tail[index], temp_line);
      }
      else{
        cache_stat_inst.misses++;
        cache_stat_inst.demand_fetches += words_per_block;
        Pcache_line new_line = malloc(sizeof(cache_line));
        new_line->tag = tag;
        new_line->dirty = 0;
        if(c->set_contents[index] < c->associativity){// conflict miss
          c->set_contents[index]++;
          insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);          
        }
        else{// capacity miss
          cache_stat_inst.replacements++;
          if(c->LRU_tail[index]->dirty){
            cache_stat_inst.copies_back += words_per_block;
          }
          delete(&c->LRU_head[index], &c->LRU_tail[index], c->LRU_tail[index]);
          insert(&c->LRU_head[index], &c->LRU_tail[index], new_line);
        }

      }
  }
}
/************************************************************/

/************************************************************/
int calc_tag(c, addr)
  Pcache c;
  unsigned addr;
{
  /* calculate the tag of an address */
  int tag;
  unsigned tag_mask;
  int set_bits, offset_bits;
  set_bits = LOG2(c->n_sets);
  offset_bits = LOG2(cache_block_size);
  tag_mask = 0xFFFFFFFF<<(set_bits+offset_bits);
  tag = (addr & tag_mask) >>(set_bits+offset_bits);
  return tag;
}
/************************************************************/

/************************************************************/
void perform_access(addr, access_type)
  unsigned addr, access_type;
{
  /* handle an access to the cache */
  if(cache_split){//spilit
    int tag_i, tag_d, index_i, index_d;
    tag_i = calc_tag(&icache, addr);
    tag_d = calc_tag(&dcache, addr);
    index_i = (addr & icache.index_mask) >> icache.index_mask_offset;
    index_d = (addr & dcache.index_mask) >> dcache.index_mask_offset;
    switch(access_type){
      case 0: data_load(&dcache, index_d, tag_d); break;
      case 1: data_store(&dcache, index_d, tag_d); break;
      case 2: instruction_load(&icache, index_i, tag_i); break;
    }
  }
    else{//unified
      int index, tag;
      index = (addr & unified_cache.index_mask) >> unified_cache.index_mask_offset;
      tag = calc_tag(&unified_cache, addr);
      switch(access_type){
        case 0: data_load(&unified_cache, index, tag); break;
        case 1: data_store(&unified_cache, index, tag); break;
        case 2: instruction_load(&unified_cache, index, tag); break;
      }
    }
}
/************************************************************/

/************************************************************/
void perform_flush(c)
  Pcache c;
{
  /* If there are any dirty blocks left, write them back to memory */
  int i,j;
  Pcache_line temp_line;
  for(i=0;i<c->n_sets;i++){
    temp_line = c->LRU_head[i];
    for(j=0;j<c->set_contents[i];j++){
      if(temp_line->dirty){
        cache_stat_data.copies_back += words_per_block;
        }
      temp_line = temp_line->LRU_next;
    }
  }
}
/************************************************************/
void flush()
{
  /* flush the cache */
  if(cache_split){
    perform_flush(&icache);
    perform_flush(&dcache);
  }
    else 
  perform_flush(&unified_cache);
  
}
/************************************************************/

/************************************************************/
void delete(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  if (item->LRU_prev) {
    item->LRU_prev->LRU_next = item->LRU_next;
  } else {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next) {
    item->LRU_next->LRU_prev = item->LRU_prev;
  } else {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
  Pcache_line *head, *tail;
  Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split) {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  } else {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n", 
	 cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
	 cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
	 1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n"); 
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n", 
	 (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
	 1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches + 
	 cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
	 cache_stat_data.copies_back);
}
/************************************************************/
