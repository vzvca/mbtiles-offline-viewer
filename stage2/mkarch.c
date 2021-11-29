#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>

#include "strhash.c"

typedef struct entry_s {
  struct entry_s *next;
  char *fname;           // file name
  char varname[32];      // C variable name
  int sz;
} entry_t;

typedef struct index_s {
  entry_t *head, *tail;
  char *ipath;
  char *opath;
  char *prefix;
  int cnt;
} index_t;

// forward
int dofilehex( FILE *fin, FILE *fout, char *varname );
FILE* dofopen( char *name, char *mode );

uint32_t prime( int sz )
{
  static uint32_t tab[] =
    {
     17  ,67  ,127 ,167 ,223 ,269 ,317 ,367 ,419 ,467 ,521 ,569 ,617 ,673 ,719 ,769 ,
     821 ,877 ,919 ,967 ,1019,1069,1117,1171,1217,1277,1319,1367,1423,1471,1523,1567,
     1619,1667,1721,1777,1823,1867,1931,1973,2017,2069,2129,2179,2221,2267,2333,2371,
     2417,2467,2521,2579,2617,2671,2719,2767,2819,2879,2917,2969,3019,3067,3119,3167,
     3217,3271,3319,3371,3433,3467,3517,3571,3617,3671,3719,3767,3821,3877,3917,3967,
     4019,4073,4127,4177,4217,4271,4327,4373,4421,4481,4517,4567,4621,4673,4721,4783,
     4817,4871,4919,4967,5021,5077,5119,5167,5227,5273,5323,5381,5417,5471,5519,5569,
     5623,5669,5717,5779,5821,5867,5923,5981,6029,6067,6121,6173,6217,6269,6317,6367,
     6421,6469,6521,6569,6619,6673,6719,6779,6823,6869,6917,6967,7019,7069,7121,7177,
     7219,7283,7321,7369,7417,7477,7517,7573,7621,7669,7717,7789,7817,7867,7919
    };
  int i;
  // try different load factors of the hash table
  // if the hash table is not too much loaded it will speed up the lookup
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= (5*sz)/2 ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= 2*sz ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= (3*sz)/2 ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= (4*sz)/3 ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= (5*sz)/4 ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= (6*sz)/5 ) return tab[i];
  }
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    if ( tab[i] >= sz ) return tab[i];
  }
  fputs( "size too big\n", stderr );
  exit(1);
  return 0;
}


char *emalloc( size_t sz )
{
  char *res = malloc(sz);
  if ( !res ) {
    fputs( "memory allocation error.\n", stderr );
    exit(1);
  }
  memset( res, 0, sz );
  return res;
}

int index_arch( index_t *index )
{
  entry_t *ent;
  char path[256];
  FILE *fin, *fout;
  
  for( ent = index->head; ent; ent = ent->next ) {
    if ( strlen(index->opath) + strlen(ent->varname) + 3 > sizeof(path) ) {
      fprintf( stderr, "pathname '%s/%s.c' too long. Skippig ...\n", index->opath, ent->varname);
      continue;
    }
    
    fin = dofopen( ent->fname, "r" );

    snprintf( path, sizeof(path),  "%s/%s.c", index->opath, ent->varname );
    fout = dofopen( path, "w" );

    ent->sz = dofilehex( fin, fout, ent->varname );

    fclose(fin);
    fclose(fout);
  }
}

char *rmprefix( char *pfx, char *s )
{
  int len;
  if ( !pfx ) return s;
  len = strlen(pfx);
  if ( !strncmp(s, pfx, len) ) return s+len;
  return s;
}

int index_hash( index_t *index )
{
  uint32_t h, p = prime( index->cnt );
  entry_t **tab, *ent;
  int i, e, me = 0;
  char path[256];
  FILE *fout;
  
  tab = (entry_t**) emalloc( p * sizeof(entry_t*));

  for( ent = index->head; ent; ent = ent->next ) {
    h = hash(p, rmprefix(index->prefix, ent->fname));
    e = 0;
    while( tab[h] ) { e++; h++; if ( h >= p ) h = 0; }
    tab[h] = ent;
    if ( e > me ) me = e;
  }

  snprintf( path, sizeof(path), "%s/__index__.c", index->opath );
  fout = dofopen( path, "w" );

  for( ent = index->head; ent; ent = ent->next ) {
    fprintf( fout, "extern char %s[];\n", ent->varname );
  }
  
  fputs( "struct { char *key; char *data; int sz; }  __arch__index__[] = {\n", fout );
  
  for( i = 0; i < p; ++i ) {
    if ( tab[i] ) {
      fprintf( fout, " { \"%s\", %s, %d },\n", rmprefix(index->prefix, tab[i]->fname), tab[i]->varname, tab[i]->sz );
    }
    else {
      fprintf( fout, " { (char*)0, (char*)0, 0 },\n" );
    }
  }
  
  fputs( "};\n", fout );

  fprintf( fout, "int __arch__prime__ = %d;\n", p );
  fprintf( fout, "int __arch__count__ = %d;\n", index->cnt );
  
  fprintf( fout, "// hastable size : %d\n", p );
  fprintf( fout, "// elt count     : %d\n", index->cnt );
  fprintf( fout, "// max excursion : %d\n", me );

  fclose(fout);
  return 0;
}

void index_free( index_t *index )
{
  entry_t *ent, *nent;

  for( ent = index->head; ent; ent = nent ) {
    nent = ent->next;
    free( ent->fname );
    free( ent );
  }

  index->head = index->tail = NULL;
  index->cnt = 0;
}

entry_t *index_add( index_t *index, char *path )
{
  entry_t *ent;
  ent = (entry_t*) emalloc(sizeof(entry_t));
  
  snprintf( ent->varname, sizeof(ent->varname), "__arch__%d", index->cnt );
  index->cnt++;

  ent->fname = strdup(path);
  if ( !ent->fname ) {
    fputs( "memory allocation error.\n", stderr );
    exit(1);
  }

  ent->next = NULL;
  if ( index->tail ) {
    index->tail->next = ent;
    index->tail = ent;
  }
  else {
    index->head = index->tail = ent;
  }
  
  return ent;
}

int walkdir( DIR *dirp, char *dirpath, index_t *index )
{
  struct dirent *ent;
  char path[256];
  
  while( ent = readdir(dirp) ) {
    if ( !strcmp(ent->d_name, ".") ) continue;
    if ( !strcmp(ent->d_name, "..") ) continue;
    switch( ent->d_type ) {
    case DT_DIR:
      {
	DIR *subdirp;
	snprintf( path, sizeof(path), "%s/%s", dirpath, ent->d_name );
	path[sizeof(path)-1] = 0;
	subdirp = opendir( path );
	if ( !subdirp ) {
	  perror( "opendir" );
	  exit(1);
	}
	walkdir( subdirp, path, index );
	if ( closedir(subdirp) ) {
	  perror( "closedir" );
	  exit(1);
	}
      }
      break;
    case DT_REG:
      snprintf( path, sizeof(path), "%s/%s", dirpath, ent->d_name );
      index_add( index, path );
      break;
    }
  }
}

#define APPEND(c) *d++ = c; if (d-bufo == sizeof(bufo)) { safewrite(fout, bufo, sizeof(bufo)); d = bufo; }
int safewrite( FILE *fout, char *buf, int sz )
{
  int r;
  while( (r = write(fileno(fout), buf, sz)) < sz ) {
    if ( r == -1 ) {
      perror("write");
      exit(1);
    }
    else {
      buf += r;
      sz -= r;
    }
  }
  return sz;
}

int dofiletxt( FILE *fin, FILE *fout, char *varname )
{
  char bufi[4096];
  char bufo[4096];
  char *s, *d;
  int  n, sz = 0;

  fprintf( fout, "char *%s = \"\\\n", varname );
  fflush( fout );
  
  d = bufo;
  while( n = read(fileno(fin), bufi, sizeof(bufi)) ) {
    if ( n == -1 ) {
      perror("read");
      exit(1);
    }
    sz += n;
    for( s = bufi; s - bufi < n; ++s ) {
      if ( *s == '\n' ) {
	APPEND( '\\' );
	APPEND( 'n' );
	APPEND( '\\' );
	APPEND( '\n' );
      }
      else {
	if ( *s == '"' ) {
	  APPEND( '\\' );
	}
	APPEND( *s );
      }
    }
  }
  if ( d - bufo ) {
    safewrite( fout, bufo, d - bufo );
  }
  fprintf( fout, "\";\n" );
  //fprintf( fout, "#include <stdio.h>\n" );
  //fprintf( fout, "int main() { return puts(%s); }\n", varname );
  fflush( fout );
  return sz;
}

int dofilehex( FILE *fin, FILE *fout, char *varname )
{
  static char *xdigit = "0123456789abcdef";
  char bufi[4096];
  char bufo[4096];
  char *s, *d;
  int  n, sz = 0;

  fprintf( fout, "char %s[] = {", varname );
  fflush( fout );
  
  d = bufo;
  while( n = read(fileno(fin), bufi, sizeof(bufi)) ) {
    if ( n == -1 ) {
      perror("read");
      exit(1);
    }
    sz += n;
    for( s = bufi; s - bufi < n; ++s ) {
      if ( (s - bufi) % 16 == 0 ) APPEND( '\n' );
      APPEND( '0' );
      APPEND( 'x' );
      APPEND( xdigit[ ((*s) >> 4) & 0xf] );
      APPEND( xdigit[ (*s) & 0xf] );
      APPEND( ',' );
    }
  }
  if ( d - bufo ) {
    safewrite( fout, bufo, d - bufo );
  }
  fprintf( fout, "};\n" );
  //fprintf( fout, "#include <stdio.h>\n" );
  //fprintf( fout, "int main() { return puts(%s); }\n", varname );
  fflush( fout );
  return sz;  
}

FILE* dofopen( char *name, char *mode )
{
  FILE *f;
  f = fopen( name, mode );
  if ( !f ) {
    perror( name );
    exit(1);
  }
  return f;
}

int isdir( char *path )
{
  struct stat stb;
  if ( !path ) return 0;
  if ( stat( path, &stb) == -1 ) {
    return -1;
  }
  return ((stb.st_mode & S_IFMT) == S_IFDIR );
}

int isreg( char *path )
{
  struct stat stb;
  if ( !path ) return 1; // path is NULL for stdin and stdout
  if ( stat( path, &stb) == -1 ) {
    return -1;
  }
  return ((stb.st_mode & S_IFMT) == S_IFREG );
}

int usage( char *fmt, ... )
{
  FILE *fout = fmt ? stderr : stdout;
  fprintf( fout, "usage: ");
  if ( fmt ) {
    va_list va;
    va_start(va, fmt );
    vfprintf( fout, fmt, va ); 
    va_end(va);
  }
  else {
    fputs( "program -v varname [-i inputfile] [-o outputfile]\n", fout );
  }

  fputs( "\t -h                  prints this help message\n", fout );
  fputs( "\t -v varname          C variable name\n", fout );
  fputs( "\t -i inputfile        input file name (defaults to stdin)\n", fout );
  fputs( "\t -i outputfile       output file name (defaults to stdout)\n", fout );

  exit( fmt ? 1 : 0 );
}

int main( int argc, char **argv )
{
  FILE *fin = stdin;
  FILE *fout = stdout;
  char *prefix = NULL;
  char *varname = NULL;
  char *ipath = NULL;
  char *opath = NULL;
  int opt, fireg, fidir, foreg, fodir;

  while ((opt = getopt(argc, argv, "hv:i:o:p:")) != -1) {
    switch (opt) {
    case 'h':
      usage(NULL);
      break;
    case 'p':
      if ( prefix ) usage( "option '%s' found more than once.\n", "-p" );
      prefix = optarg;
      break;
    case 'v':
      if ( varname ) usage( "option '%s' found more than once.\n", "-v" );
      varname = optarg;
      break;
    case 'i':
      if ( ipath  ) usage( "option '%s' found more than once.\n", "-i" );
      ipath = optarg;
      break;
    case 'o':
      if ( opath ) usage( "option '%s' found more than once.\n", "-o" );
      opath = optarg;
      break;
    default: /* '?' */
      usage( "unexpected value on command line '%s'.\n", optarg );
    }
  }

  fireg = isreg(ipath);
  fidir = isdir(ipath);
  foreg = isreg(opath);
  fodir = isdir(opath);

  if ( fireg == 1 ) { // input file specified or stdin
    if ( fodir == 1 ) {
      usage( "input is a file or stdin, output must be a file.\n" );
    }
    if ( !varname ) {
      usage( "option '-v' is mandatory.\n" );
    }
    fin = dofopen( ipath, "r" );
    if ( opath ) {
      fout = dofopen( opath, "w" );
    }
    dofilehex( fin, fout, varname );
    if ( ipath ) fclose(fin);
    if ( opath ) fclose(fout);
  }
  if ( fidir == 1 ) { // input directory specified
    index_t index;
    DIR *idir;
    int len;
    
    if ( foreg == 1 ) {
      usage( "input is a directory, output must be defined and be a directory if it exists.\n" );
    }
    if ( fodir == -1 ) {
      // try to create output directory
      if ( mkdir( opath, 0777 ) == -1 ) {
	perror( "mkdir" );
	exit(1);
      }
    }

    // init index
    index.head = index.tail = NULL;
    index.ipath = ipath;
    index.opath = opath;
    index.prefix = prefix;
    index.cnt = 0;

    // remove trailing /
    len = strlen(ipath)-1;
    if ( ipath[len] == '/' ) ipath[len] = 0;
    len = strlen(opath)-1;
    if ( opath[len] == '/' ) opath[len] = 0;
    
    idir = opendir( ipath );
    if ( !idir ) {
      perror( "opendir" );
      exit(1);
    }
    walkdir( idir, ipath, &index);
    index_arch( &index );
    index_hash( &index );
    index_free( &index );
    closedir( idir );
  }
  
  return 0;
}
