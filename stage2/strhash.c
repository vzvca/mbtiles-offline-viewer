#include <stdint.h>

uint32_t hash( uint32_t prime, char *s )
{
  uint32_t c, h = 257;
  while( c = *s++ ) {
    h = ((h << 5) + h) + c;
  }
  return h % prime;
}

uint32_t hash2len( uint32_t prime, char *s, int l )
{
  uint32_t c, h = 257;
  while( l-- ) {
    c = *s++;
    h = ((h << 5) + h) + c;
  }
  return h % prime;
}
