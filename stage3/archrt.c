#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "strhash.c"

extern struct {
  char *key;
  char *data;
  int sz;
}  __arch__index__[];

extern int __arch__prime__;
extern int __arch__count__;

extern void logger(const char *fmt, ...);

char *arch_data( char *k )
{
  uint32_t h = hash( __arch__prime__, k );
  while( __arch__index__[h].key ) {
    if ( !strcmp( __arch__index__[h].key, k) ) {
      return __arch__index__[h].data;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return NULL;
}

char *arch_data_ex( char *k, int len )
{
  uint32_t h = hash2len( __arch__prime__, k, len );
  logger("searching '%.*s'\n", len, k);
  while( __arch__index__[h].key ) {
    logger( "... scan '%s'\n", __arch__index__[h].key);
    if ( !strncmp( __arch__index__[h].key, k, len) ) {
      return __arch__index__[h].data;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return NULL;
}

int arch_size( char *k )
{
  uint32_t h = hash( __arch__prime__, k );
  while( __arch__index__[h].key ) {
    if ( !strcmp( __arch__index__[h].key, k) ) {
      return __arch__index__[h].sz;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}

int arch_size_ex( char *k, int len )
{
  uint32_t h = hash2len( __arch__prime__, k, len );

  while( __arch__index__[h].key ) {
    if ( !strncmp( __arch__index__[h].key, k, len) ) {
      return __arch__index__[h].sz;
    }
    ++h; if ( h > __arch__prime__ ) h = 0;
  }
  return -1;
}
