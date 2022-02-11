#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "http_parser.h"

#include "strhash.c"

extern struct {
  char *key;
  char *data;
  int sz;
}  __arch__index__[];

extern int __arch__prime__;
extern int __arch__count__;

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

char *arch_data_len( char *k, int len )
{
  uint32_t h = hash2len( __arch__prime__, k, len );
  fprintf( stderr, "searching '%.*s'\n", len, k);
  while( __arch__index__[h].key ) {
    fprintf( stderr, "... scan '%s'\n", __arch__index__[h].key);
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

int arch_size_len( char *k, int len )
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


typedef struct req_s req_t;
struct req_s {
  char *url;
  char *hv[64];
  int   nhv;
  char *body;
};

typedef struct cnx_s cnx_t;
struct cnx_s {
  int fd;

  req_t req;
  
  http_parser_settings settings;
  struct http_parser_url urlp;
  http_parser parser;

};

// connection array
#define MAXCNX 16
cnx_t _cnxtab[MAXCNX];
cnx_t *cnxtab[MAXCNX];
int serverfd = -1;

// BACKLOG for listen
#define BACKLOG 8

int g_port = 9009;

// forward
int doclose( cnx_t *cnx );


/* --------------------------------------------------------------------------
 *  Retrieve a connection by file descriptor
 *  Returns NULL if no match found
 * --------------------------------------------------------------------------*/
cnx_t *fd2cnx( int fd )
{
  int i;
  for( i = 0; i < MAXCNX; ++i ) {
    if ( cnxtab[i] && cnxtab[i]->fd == fd ) return cnxtab[i];
  }
  return NULL;
}

/* --------------------------------------------------------------------------
 *  Write, checking for errors.
 * --------------------------------------------------------------------------*/
static void safewrite(int fd, const char *b, size_t n)
{
  size_t w = 0;
  while (w < n) {
    ssize_t s = write(fd, b + w, n - w);
    //if ( s > 0 ) {
    //  write(fileno(stdout), b+w, s);
    //}
    if (s < 0 ) {
      if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	usleep(1);
	continue;
      }
      else {
	perror("write failed");
	return;
      }
    }
    w += (size_t)s;
  }
}

/* --------------------------------------------------------------------------
 *  Write a line
 * --------------------------------------------------------------------------*/
void writeln( int fd, char *fmt, ... )
{
  va_list va;
  char buffer[512];
  va_start(va, fmt);
  vsnprintf( buffer, sizeof(buffer), fmt, va);
  buffer[sizeof(buffer)-1] = '\0';
  va_end(va);
  safewrite( fd, buffer, strlen(buffer));
  safewrite( fd, "\r\n", 2);
}

/* --------------------------------------------------------------------------
 *  Send HTTP return code
 * Only 2 codes supported
 * --------------------------------------------------------------------------*/
static void send_response( int fd, enum http_status code )
{
  writeln( fd, "HTTP/1.1 %d %s", code, http_status_str(code));
  fprintf( stderr, "ANS %d %s\n", code, http_status_str(code));
}

/* --------------------------------------------------------------------------
 *  Send http error answer
 * --------------------------------------------------------------------------*/
void http_reply_error( cnx_t *cnx, enum http_status s )
{
  int fd = cnx->fd;

  
  send_response( fd, s);

  writeln( fd, "Server: archrt (linux)");
  writeln( fd, "Content-Type: text/html; charset=iso-8859-1");
  writeln( fd, "Content-Length: 0");
  if ( !http_should_keep_alive( &cnx->parser) ) {
    writeln( fd, "Connection: Close");
  }
  writeln( fd, "");

  if ( !http_should_keep_alive( &cnx->parser) ) {
    doclose(cnx);
  }
}

/* --------------------------------------------------------------------------
 *  Reply with data
 * --------------------------------------------------------------------------*/
void http_reply_data( cnx_t *cnx, char *mtype, char *data, int len )
{
  int fd = cnx->fd;
  
  send_response( fd, HTTP_STATUS_OK );
  
  writeln( fd, "Content-Type: %s", mtype);
  writeln( fd, "Content-Length: %d", len );
  if ( !http_should_keep_alive( &cnx->parser) ) {
    writeln( fd, "Connection: Close");
  }
  writeln( fd, "" );

  safewrite( fd, data, len );

  if ( !http_should_keep_alive( &cnx->parser) ) {
    doclose(cnx);
  }
}

/* --------------------------------------------------------------------------
 *  Generates tiles/tiles.json
 * --------------------------------------------------------------------------*/
void http_reply_tiles( cnx_t *cnx, char *mtype, char *data, int len )
{
  char tab[len+4];
  snprintf( tab, sizeof(tab), data, g_port );

  http_reply_data( cnx, mtype, tab, strlen(tab));
}

/* --------------------------------------------------------------------------
 *  Compute mimetype from length
 * --------------------------------------------------------------------------*/
char* http_mimetype( char *k, int len )
{
  static struct {
    char *ext;
    char *mtype;
  } tab[] =
      {
       { ".pbf",  "application/x-protobuf" },
       { ".json", "application/json" },
       { ".js",   "application/javascript" },
       { ".html", "text/html" },
       { ".css",  "text/css" }
      };
  int i, elen;
  for( i = 0; i < sizeof(tab)/sizeof(tab[0]); ++i ) {
    elen = strlen(tab[i].ext);
    if ( !strncmp(k + (len-elen), tab[i].ext, elen) ) {
      return tab[i].mtype;
    }
  }
  return "text/plain";
}

int alphaval( char c )
{
  fprintf( stderr, "alphaval %c\n", c);
  if ( c >= '0' && c <= '9' ) return c - '0';
  c = toupper(c);
  if ( c >= 'A' && c <= 'F' ) return c + 10 - 'A';
  return '%';
}  

/* --------------------------------------------------------------------------
 *  Remove percent encoding sequences inplace
 * --------------------------------------------------------------------------*/
char *http_rm_percent( char *path, int len )
{
  char *s, *d;
  for( s = d = path; len-- && *s; ++s, ++d ) {
    if ( *s != '%' ) {
      *d = *s;
    }
    else {
      if (isalnum(s[1]) && isalnum(s[2])) {
	*d = (alphaval(s[1]) << 4) | alphaval(s[2]);
	//printf("alphaval res %02x\n", *d );
	s += 2;
      }
      else {
	*d = '%';
      }
    }
  }
  *d = 0;
  
  return path;
}



/* --------------------------------------------------------------------------
 *  Reply to HTTP request
 * --------------------------------------------------------------------------*/
void http_reply( cnx_t *cnx )
{
  char *data, *mtype, *k;
  int l;

  
  if ( cnx->urlp.field_set & (1 << UF_QUERY) ) {
    http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
  }
  if ( cnx->urlp.field_set & (1 << UF_FRAGMENT) ) {
    http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
  }
  
  switch( cnx->parser.method ) {
  case HTTP_GET:
    k = cnx->req.url + cnx->urlp.field_data[UF_PATH].off + 1;
    l = cnx->urlp.field_data[UF_PATH].len - 1;
    if ( l == 0 ) {
      // requesting "/"
      k = "index.html";
    }
    else {
      k = http_rm_percent( k, l );
    }
    l = strlen(k);
    fprintf( stderr, "URL %.*s\n", l, k );
    
    data = arch_data_len( k, l);
		       
    if ( data ) {
      mtype = http_mimetype(k,l);
      if ( cnx->parser.method == HTTP_GET ) {
	int len = arch_size_len( k, l);
	if ( !strcmp( k, "tiles/tiles.json") ) {
	  http_reply_tiles( cnx, mtype, data, len );
	}
	else {
	  http_reply_data( cnx, mtype, data, len);
	}
      }
    }
    else {
      http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
    }
    break;
  default:
    http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
  }
}

/* --------------------------------------------------------------------------
 *  malloc wrapper
 * --------------------------------------------------------------------------*/
char *emalloc( size_t sz )
{
  char *r = (char*) malloc(sz);
  if ( !r ) {
    fputs( "malloc failed: memory allocation error.\n", stderr );
    exit(1);
  }
  return r;
}

/* --------------------------------------------------------------------------
 *  realloc wrapper
 * --------------------------------------------------------------------------*/
char *erealloc( char *p, size_t sz )
{
  char *r = (char*) realloc(r, sz);
  if ( !r ) {
    fputs( "realloc failed: memory allocation error.\n", stderr );
    exit(1);
  }
  return r;
}

/* --------------------------------------------------------------------------
 *  Allocates a request
 * --------------------------------------------------------------------------*/
req_t *req_alloc()
{
  req_t *r;
  r = (req_t*) emalloc(sizeof(req_t));
  memset( r, 0, sizeof(req_t));
  return r;
}

/* --------------------------------------------------------------------------
 *  Free memory allocated by request
 * --------------------------------------------------------------------------*/
void req_clean( req_t *req )
{
  int i;
  free( req->url );
  req->url = NULL;
  free( req->body );
  req->body = NULL;
  for( i = 0; i < req->nhv; i++ ) {
    free( req->hv[i] );
    req->hv[i] = NULL;
  }
  req->nhv = 0;
}

/* --------------------------------------------------------------------------
 *  Called on new HTTP request
 * --------------------------------------------------------------------------*/
int message_begin_cb( http_parser *p )
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  req_clean( req );
  return 0;
}

/* --------------------------------------------------------------------------
 *  Called when HTTP headers parsing is completed
 * --------------------------------------------------------------------------*/
int headers_complete_cb( http_parser *p)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  // mark end of headers
  if ( req->nhv % 2 ) req->nhv++;
  return 0;
}

/* --------------------------------------------------------------------------
 *  HTTP debug utility
 * --------------------------------------------------------------------------*/
void dump_url( const char *url, const struct http_parser_url *u)
{
  unsigned int i;

  printf("\tfield_set: 0x%x, port: %u\n", u->field_set, u->port);
  for (i = 0; i < UF_MAX; i++) {
    if ((u->field_set & (1 << i)) == 0) {
      printf("\tfield_data[%u]: unset\n", i);
      continue;
    }

    printf("\tfield_data[%u]: off: %u, len: %u, part: %.*s\n",
	   i,
	   u->field_data[i].off,
	   u->field_data[i].len,
	   u->field_data[i].len,
	   url + u->field_data[i].off);
  }
}

/* --------------------------------------------------------------------------
 *  Called when HTTP request parsing is done
 * --------------------------------------------------------------------------*/
int message_complete_cb( http_parser *p)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  int i, r;

  puts( req->url );
  for( i = 0; i < req->nhv; i+=2 ) {
    printf("%s -> %s\n", req->hv[i], req->hv[i+1]);
  }

  http_parser_url_init( &cnx->urlp );
  r = http_parser_parse_url(req->url, strlen(req->url), 0, &cnx->urlp );
  if (r != 0) {
    fprintf( stderr, "URL parsing error : %d\n", r);
    return r;
  }
  
  //dump_url( req->url, &cnx->urlp);

  http_reply( cnx );
  
  req_clean( req );
  
  return 0;
}

/* --------------------------------------------------------------------------
 *  HTTP parsing callback
 * --------------------------------------------------------------------------*/
int url_cb( http_parser *p, const char *at, size_t length)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  
  if ( req->url ) {
    req->url = erealloc( req->url, strlen(req->url) + length + 1 );
  }
  else {
    req->url = emalloc( length + 1 );
    *req->url = 0;
  }
  strncat( req->url, at, length );
  return 0;
}

/* --------------------------------------------------------------------------
 *  HTTP parsing callback
 * --------------------------------------------------------------------------*/
int header_field_cb( http_parser *p, const char *at, size_t length)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  char **f = req->hv + req->nhv;
  
  if ( (req->nhv % 2) == 1 ) {
    req->nhv++;
    f++;
  }

  if ( req->nhv >= 64 ) {
    fputs( "too many headers", stderr );
    exit(1);
  }
  
  if ( *f ) {
    *f = erealloc( *f, strlen(*f) + length + 1 );
  }
  else {
    *f = emalloc( length + 1 );
    **f = 0;
  }
  strncat( *f, at, length );

  return 0;
}

/* --------------------------------------------------------------------------
 *  HTTP parsing callback
 * --------------------------------------------------------------------------*/
int header_value_cb( http_parser *p, const char *at, size_t length)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  char **f = req->hv + req->nhv;
  
  if ( (req->nhv % 2) == 0 ) {
    req->nhv++;
    f++;
  }

  if ( req->nhv >= 64 ) {
    fputs( "too many headers", stderr );
    exit(1);
  }

  if ( *f ) {
    *f = erealloc( *f, strlen(*f) + length + 1 );
  }
  else {
    *f = emalloc( length + 1 );
    **f = 0;
  }
  strncat( *f, at, length );

  return 0;
}

/* --------------------------------------------------------------------------
 *  Starts TCP server listening on 0.0.0.0:'portno'
 * --------------------------------------------------------------------------*/
int server( short portno )
{
  struct sockaddr_in server_addr;
  int serverfd;

  /* create listening socket */
  serverfd = socket(AF_INET, SOCK_STREAM, 0);
  if ( serverfd < 0 ) {
    perror("ERROR opening server socket");
    exit(1);
  }

  /* bind it */
  bzero( (char*) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(portno);
 
  if ( bind(serverfd, (struct sockaddr*) &server_addr, sizeof(server_addr)) < 0 ) {
    perror("ERROR on binding");
    exit(1);
  }
   
  /* start listening */
  if ( listen(serverfd, BACKLOG) < 0 ) {
    perror("ERROR on listen");
    exit(1);
  }

  return serverfd;
}

/* --------------------------------------------------------------------------
 *  Accept an incoming connection if possible
 * --------------------------------------------------------------------------*/
int doaccept( int fd )
{
  cnx_t *cnx = NULL;
  int i, flags;
  
  for( i = 0; i < MAXCNX; ++i ) {
    if ( cnxtab[i] == NULL ) {
      cnx = cnxtab[i] = _cnxtab + i;
      break;
    }
  }
  if ( !cnx ) {
    fprintf( stderr, "too many connection." );
    return -1;
  }
  memset( cnx, 0, sizeof(cnx_t));
  
  cnx->fd = accept( fd, NULL, NULL);
  if ( cnx->fd == -1 ) {
    perror("accept");
    req_clean( &cnx->req );
    cnxtab[i] == NULL;
    return -1;
  }

  flags = fcntl( cnx->fd ,F_GETFL, 0);
  fcntl( cnx->fd, F_SETFL, flags | O_NONBLOCK);

  cnx->settings.on_url = url_cb;
  cnx->settings.on_header_field = header_field_cb;
  cnx->settings.on_header_value = header_value_cb;
  cnx->settings.on_message_begin = message_begin_cb;
  cnx->settings.on_headers_complete = headers_complete_cb;;
  cnx->settings.on_message_complete = message_complete_cb;

  http_parser_init( &cnx->parser, HTTP_REQUEST);
  cnx->parser.data = cnx;
  
  return 0;
}

/* --------------------------------------------------------------------------
 *  Close connection 
 * --------------------------------------------------------------------------*/
int doclose( cnx_t *cnx )
{
  int i;
  
  close( cnx->fd );
  req_clean( &cnx->req );
  for( i = 0; i < MAXCNX; ++i ) {
    if (cnxtab[i] == cnx) cnxtab[i] = NULL;
  }
}

/* --------------------------------------------------------------------------
 *  Read incoming HTTP request and handle it 
 * --------------------------------------------------------------------------*/
int doinput( cnx_t *cnx )
{
  char buf[4096];
  int nr, tp, np, nt = 0;
  
  while( (nr = read( cnx->fd, buf, sizeof(buf))) > 0 ) {
    nt += nr;
    for( tp = 0; tp < nr; tp += np ) {
      np = http_parser_execute(&cnx->parser, &cnx->settings, buf + tp, nr - tp);
      if ( HTTP_PARSER_ERRNO( &cnx->parser ) ) {
	fprintf( stderr, "HTTP error %s : %s\n",
		 http_errno_name( HTTP_PARSER_ERRNO( &cnx->parser )),
		 http_errno_description( HTTP_PARSER_ERRNO( &cnx->parser )));
	doclose(cnx);
	return -1;
      }
      if ( cnx->parser.upgrade ) {
	fprintf( stderr, "HTTP connexion upgrade not supported.\n" );
	doclose(cnx);
	return -1;
      }
    }
  }
  if ( nt == 0 ) {
    fprintf( stderr, "remote end closed connection." );
    doclose( cnx );
    return -1;
  }
  return nt;
}

/* --------------------------------------------------------------------------
 *  IO loop based on select
 * --------------------------------------------------------------------------*/
int selectloop()
{
  fd_set rdset;
  int i, r, m;
  
  while(1) {
    FD_ZERO( &rdset );
    
    FD_SET( serverfd, &rdset );

    m = serverfd;
    for( i = 0; i < MAXCNX; ++i ) {
      if ( cnxtab[i] ) {
	FD_SET( cnxtab[i]->fd, &rdset );
	if ( cnxtab[i]->fd > m ) {
	  m = cnxtab[i]->fd;
	}
      }
    }

    r = select( m+1, &rdset, NULL, NULL, NULL );
    if ( r == -1 ) {
      perror("select");
      return -1;
    }

    if ( FD_ISSET( serverfd, &rdset ) ) {
      doaccept( serverfd );
    }
    for( i = 0; i < MAXCNX; ++i ) {
      if ( cnxtab[i] && FD_ISSET( cnxtab[i]->fd, &rdset ) ) {
	doinput( cnxtab[i] );
      }
    }
  }

  return 0;
}

/* --------------------------------------------------------------------------
 *  Close connections
 * --------------------------------------------------------------------------*/
void byebye()
{
  int i;
  close( serverfd );
  for( i = 0; i < MAXCNX; ++i ) {
    if ( cnxtab[i] ) {
      close( cnxtab[i]->fd );
    }
  }
}

/* --------------------------------------------------------------------------
 *  Prints program usage and exits
 * --------------------------------------------------------------------------*/
void usage( char *fmt, ... )
{
  FILE *fout = fmt ? stderr : stdout;
  va_list va;
  if ( !fmt ) fmt = "";
  fprintf( fout, "usage: ");
  va_start( va, fmt );
  vfprintf( fout, fmt, va );
  fprintf( fout, "\n" );

  fprintf( fout, "\t -h            Prints this help message.\n");
  fprintf( fout, "\t -x            Starts web browser to display map.\n");
  fprintf( fout, "\t -p port       Sets port number to listen on.\n");
  fprintf( fout, "\t -s style      Sets style.json file to use for rendering.\n");

  exit( fmt ? 1 : 0 );
}

/* --------------------------------------------------------------------------
 *  Main program
 * --------------------------------------------------------------------------*/
int main( int argc, char **argv )
{
#define F_PORT  0x01
#define F_EXEC  0x02
  int opt, flags = 0;
  int nr, np, tp;

  signal( SIGPIPE, SIG_IGN );
  atexit( byebye );

  while ((opt = getopt(argc, argv, "hxp:m:s:")) != -1) {
    switch (opt) {
    case 'h':
      usage( NULL );
      break;
    case 'x':
      if ( flags & F_EXEC ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      flags |= F_EXEC;
      break;
    case 'p':
      if ( flags & F_PORT ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      g_port = atoi(optarg);
      flags |= F_PORT;
      break;
    default:
      usage("unrecognized option.\n");
    }
  }

  serverfd = server(g_port);

  if ( flags & F_EXEC ) {
    char cmd[64];
    sprintf( cmd, "xdg-open http://127.0.0.1:%d", g_port );
    system( cmd );
  }

  selectloop();
  
  return 0;
}

