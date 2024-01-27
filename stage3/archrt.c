#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <zlib.h>

#include "strhash.c"

struct __arch__elem__s {
  char *key;
  char *data;
  unsigned int sz:24;
  unsigned int ratio:7;
  unsigned int compressed:1;
};
extern struct __arch__elem__s __arch__index__[];

extern int __arch__prime__;
extern int __arch__count__;

extern void logger(const char *fmt, ...);

/* --------------------------------------------------------------------------
 *  A LRU cache of uncompressed archive members is maintained
 *  It will be used if the client require uncompressed data for an archive
 *  member but its data is stored compressed.
 * --------------------------------------------------------------------------*/
struct arch_cache_s {
  struct __arch__elem__s *elem;  // handle to cached archive member
  int   usz;                     // uncompressed size
  char *udata;                   // uncompressed data
  int   next;                    // next element in list - LRU cache
  int   prev;                    // previous element in list - LRU cache
};

#define CACHESZ 48
static struct arch_cache_s cache[CACHESZ];
static int cache_head = -1;       // first element in cache
static int cache_last = -1;       // last element in cache

#define BLKSZ 4096

/* --------------------------------------------------------------------------
 *  Free memory used by cache entry
 *  Will not remove it from list
 * --------------------------------------------------------------------------*/
static void cache_free(int i)
{
  assert (i >= 0 && i < CACHESZ);
  cache[i].elem = NULL;
  cache[i].next = cache[i].prev = -1;
  free (cache[i].udata);
  cache[i].udata = NULL;
  cache[i].usz = 0;
}

/* --------------------------------------------------------------------------
 *  Find free slot in cache
 * --------------------------------------------------------------------------*/
static int cache_alloc()
{
  int i;
  for (i = 0; i < CACHESZ; ++i) {
    if (cache[i].elem == NULL) {
      cache[i].next = cache[i].prev = -1;
      return i;
    }
  }
  // reuse least recently used cached entry because cache is full
  i = cache_last;
  cache_last = cache[cache_last].prev;
  cache_free(i);
  memset (&cache[i], 0, sizeof(cache[0]));
  cache[i].next = cache[i].prev = -1;
  return i;
}

/* --------------------------------------------------------------------------
 *  Search cache for an element named 'name'
 *  Returns its index and -1 if not found.
 * --------------------------------------------------------------------------*/
static int cache_search(char *name)
{
  if (cache_head == -1) {
    return -1;
  }
  else {
    int i;
    for (i = cache_head; i >= 0; i = cache[i].next) {
      assert (i < CACHESZ);
      assert (cache[i].elem != NULL);
      if (!strcmp(cache[i].elem->key, name)) {
	return i;
      }
      return -1;
    }
  }
}

/* --------------------------------------------------------------------------
 *  Add element to cache
 * --------------------------------------------------------------------------*/
static void cache_add(int i, struct __arch__elem__s *elem, char *udata, int usz)
{
  assert (i >= 0 && i < CACHESZ);
  assert (cache[i].elem == NULL);
  assert (cache[i].udata == NULL);
  assert (cache[i].usz == 0);
  cache[i].elem = elem;
  cache[i].udata = udata;
  cache[i].usz = usz;
}

/* --------------------------------------------------------------------------
 *  Remove element from list
 * --------------------------------------------------------------------------*/
static int cache_unlink(int i)
{
  int p, n;
  assert (i >= 0 && i < CACHESZ);
  assert (cache[i].elem != NULL);
  n = cache[i].next;
  p = cache[i].prev;
  assert (n >= -1 && n < CACHESZ);
  assert (p >= -1 && p < CACHESZ);
  if (n >= 0 && p >= 0) {
    // was in middle of list 
    cache[p].next = n;
    cache[n].prev = p;
  }
  else if (p >= 0) {
    // was at end of list
    cache[p].next = -1;
    cache_last = p;
  }
  else if (n >= 0) {
    // was first of list
    cache[n].prev = -1;
    cache_head = n;
  }
  else {
    // was alone in list
    cache_head = cache_last = -1;
  }
}

/* --------------------------------------------------------------------------
 *  Insert cache entry at i at beggining of list
 *  Must not be in list already
 * --------------------------------------------------------------------------*/
static void cache_insert_at_head(int i)
{
  assert(i >= 0 && i < CACHESZ);
  assert(cache[i].elem != NULL);
  assert(cache[i].next == -1);
  assert(cache[i].prev == -1);
  if (cache_head >= 0) {
    cache[cache_head].prev = i;
    cache[i].next = cache_head;
    cache_head = i;
  }
  else {
    cache_head = cache_last = i;
  }
}


/* --------------------------------------------------------------------------
 *  Uncompress cache entry
 * --------------------------------------------------------------------------*/
static char *cache_uncompress (struct arch_cache_s *e)
{
  unsigned long ulen;
  int res;
  
  assert (e->elem->compressed == 1);

  printf("=======UNCOMPRESS %s\n", e->elem->key);

  ulen = (e->elem->ratio*e->elem->sz)>>3;
  e->udata = (char*) malloc (ulen);
  if (e->udata == NULL) {
    perror ("cache_uncompress: malloc()");
    exit(1);
  }
  e->usz = 0;

  res = uncompress (e->udata, &ulen, e->elem->data, e->elem->sz);
  if (res != Z_OK) {
    fprintf (stderr, "Failed to decompress '%s'\n", e->elem->key);
    exit (1);
  }
  e->usz = ulen;

  return e->udata;
}

/* --------------------------------------------------------------------------
 *  Returns cache entry for archive member e
 *  If not found allocate an entry
 *  Move the cache entry for e at top of LRU list
 * --------------------------------------------------------------------------*/
static int cache_handle (struct __arch__elem__s *e)
{
  int i = cache_search(e->key);
  if (i == -1) {
    struct arch_cache_s *c;
    i = cache_alloc();
    assert (i >= 0 && i < CACHESZ);
    c = &cache[i];
    c->elem = e;
    cache_uncompress (c);
  }
  else {
    printf("==== FOUND CACHED DATA AT %d\n", i);
  }
  // move at top of LRU list
  cache_unlink(i);
  cache_insert_at_head(i);
  return i;
}

/* --------------------------------------------------------------------------
 *  Retrieve data from archive element given its key
 *  If *compressed is 1 return compressed data if available
 *  If compressed data asked but not available set *compressed to 0
 *  If *compressed is 0 return uncompressed data
 * --------------------------------------------------------------------------*/
char *arch_data( char *k, int *compressed )
{
  uint32_t h = hash( __arch__prime__, k );
  while( __arch__index__[h].key ) {
    if ( !strcmp( __arch__index__[h].key, k) ) {
      // if data is compressed and need to be decompressed,
      // look for it in uncompressed cache.
      // if present reuse uncompressed cached data
      // otherwise uncompress and cache data
      if ((*compressed == 0) && __arch__index__[h].compressed) {
	int i = cache_handle (&__arch__index__[h]);
	return cache[i].udata;
      }
      else {
	*compressed = __arch__index__[h].compressed;
	return __arch__index__[h].data;
      }
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return NULL;
}

/* --------------------------------------------------------------------------
 *  Retrieve data from archive element given its key
 * --------------------------------------------------------------------------*/
char *arch_data_ex( char *k, int len, int *compressed )
{
  uint32_t h = hash2len( __arch__prime__, k, len );
  logger("searching '%.*s'\n", len, k);
  while( __arch__index__[h].key ) {
    logger( "... scan '%s'\n", __arch__index__[h].key);
    if ( !strncmp( __arch__index__[h].key, k, len) ) {
      // if data is compressed and need to be decompressed,
      // look for it in uncompressed cache.
      // if present reuse uncompressed cached data
      // otherwise uncompress and cache data
      if ((*compressed == 0) &&__arch__index__[h].compressed) {
	int i = cache_handle (&__arch__index__[h]);
	return cache[i].udata;
      }
      else {
	*compressed = __arch__index__[h].compressed;
	return __arch__index__[h].data;
      }
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return NULL;
}

/* --------------------------------------------------------------------------
 *  Retrieve size from archive element given its key
 * --------------------------------------------------------------------------*/
int arch_size( char *k, int *compressed )
{
  uint32_t h = hash( __arch__prime__, k );
  while( __arch__index__[h].key ) {
    if ( !strcmp( __arch__index__[h].key, k) ) {
      // if data is compressed and need to be decompressed,
      // look for it in uncompressed cache.
      // if present reuse uncompressed cached data
      // otherwise uncompress and cache data
      if ((*compressed == 0) &&__arch__index__[h].compressed) {
	int i = cache_handle (&__arch__index__[h]);
	return cache[i].usz;
      }
      else {
	*compressed = __arch__index__[h].compressed;
	return __arch__index__[h].sz;
      }
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}

/* --------------------------------------------------------------------------
 *  Retrieve size from archive element given its key
 * --------------------------------------------------------------------------*/
int arch_size_ex( char *k, int len, int *compressed )
{
  uint32_t h = hash2len( __arch__prime__, k, len );

  while( __arch__index__[h].key ) {
    if ( !strncmp( __arch__index__[h].key, k, len) ) {
      // if data is compressed and need to be decompressed,
      // look for it in uncompressed cache.
      // if present reuse uncompressed cached data
      // otherwise uncompress and cache data
      if ((*compressed == 0) && __arch__index__[h].compressed) {
	int i = cache_handle (&__arch__index__[h]);
	return cache[i].usz;
      }
      else {
	*compressed = __arch__index__[h].compressed;
	return __arch__index__[h].sz;
      }
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}

/* --------------------------------------------------------------------------
 *  Tells if archive element is compressed given its key
 *  Returns 0 if not, 1 if compressed, -1 if not found
 * --------------------------------------------------------------------------*/
int arch_is_compressed( char *k )
{
  uint32_t h = hash( __arch__prime__, k );
  while( __arch__index__[h].key ) {
    if ( !strcmp( __arch__index__[h].key, k) ) {
      return __arch__index__[h].compressed;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}

/* --------------------------------------------------------------------------
 *  Tells if archive element is compressed given its key
 *  Returns 0 if not, 1 if compressed, -1 if not found
 * --------------------------------------------------------------------------*/
int arch_is_compressed_ex( char *k, int len )
{
  uint32_t h = hash2len( __arch__prime__, k, len );

  while( __arch__index__[h].key ) {
    if ( !strncmp( __arch__index__[h].key, k, len) ) {
      return __arch__index__[h].compressed;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}
