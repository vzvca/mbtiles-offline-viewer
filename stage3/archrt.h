#ifndef __ARCHRT_H__
#define __ARCHRT_H__

#include <stdint.h>

uint32_t hash( uint32_t prime, char *s );
uint32_t hash2len( uint32_t prime, char *s, int l );

char *arch_data( char *k );
char *arch_data_ex( char *k, int len );
int arch_size( char *k );
int arch_size_ex( char *k, int len );

#endif
