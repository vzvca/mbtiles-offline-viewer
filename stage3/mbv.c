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
#include "archrt.h"

typedef struct req_s req_t;
struct req_s {
  char *url;
  char *hv[64];
  int   nhv;
  int   accept_deflate;
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

#define BLKIO 4096


int g_quiet = 1;
int g_zero = 0;
int g_port = 9000;
char *g_map, *g_style;

void *g_sql;  // sqlite map database handle

void *mbtiles_open( char *path );
void  mbtiles_close( void *stmt );
char *mbtiles_read( void *s, int x, int y, int z, int *len );
char *mbtiles_tiles_json( void *dbh, int *len );
char *mbtiles_auto_style_json( void *dbh, int *len );

// forward
int doclose( cnx_t *cnx );
char *emalloc( size_t sz );

/* --------------------------------------------------------------------------
 *  Basic logger
 * --------------------------------------------------------------------------*/
void logger(const char *fmt, ...)
{
  if (!g_quiet) {
    va_list va;
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
  }
}

/* --------------------------------------------------------------------------
 *  Retrieve connection by file descriptor
 *  Returns NULL is no connection is linked to 'fd'
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
	perror("write");
	return;
      }
    }
    w += (size_t)s;
  }
}

/* --------------------------------------------------------------------------
 *  Write a line with HTTP line endings '\r\n'
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
 * --------------------------------------------------------------------------*/
static void send_response( int fd, enum http_status code )
{
  writeln( fd, "HTTP/1.1 %d %s", code, http_status_str(code));
  logger("ANS %d %s\n", code, http_status_str(code));
}

/* --------------------------------------------------------------------------
 *  Send http error answer
 * --------------------------------------------------------------------------*/
int http_reply_error( cnx_t *cnx, enum http_status s )
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

  return 0;
}

/* --------------------------------------------------------------------------
 *  Reply with data
 * --------------------------------------------------------------------------*/
int http_reply_data_ex( cnx_t *cnx, char *mtype, char *data, int len, ... )
{
  int fd = cnx->fd;
  char *header;
  va_list va;
  
  send_response( fd, HTTP_STATUS_OK );
  
  writeln( fd, "Content-Type: %s", mtype);
  writeln( fd, "Content-Length: %d", len );
  if ( cnx->req.accept_deflate ) {
    writeln (fd, "Content-Encoding: deflate");
  }
  if ( !http_should_keep_alive( &cnx->parser) ) {
    writeln( fd, "Connection: Close");
  }
  va_start( va, len );
  while( header = va_arg( va, char*) ) {
    writeln( fd, header );
  }
  va_end( va );
  writeln( fd, "" );

  safewrite( fd, data, len );

  if ( !http_should_keep_alive( &cnx->parser) ) {
    doclose(cnx);
  }

  return 0;
}

/* --------------------------------------------------------------------------
 *  Reply data
 * --------------------------------------------------------------------------*/
int http_reply_data( cnx_t *cnx, char *mtype, char *data, int len )
{
  return http_reply_data_ex( cnx, mtype, data, len, NULL );
}

/* --------------------------------------------------------------------------
 *  Generates tiles/tiles.json
 * --------------------------------------------------------------------------*/
int http_reply_tiles_json( cnx_t *cnx, char *mtype )
{
  static char *data = NULL;
  static int len = 0;

  // force to reply with uncompressed data
  cnx->req.accept_deflate = 0;
  data = mbtiles_tiles_json( g_sql, &len );
  
  return http_reply_data( cnx, mtype, data, len);
}

/* --------------------------------------------------------------------------
 *  Serves style.json
 * --------------------------------------------------------------------------*/
int http_reply_style( cnx_t *cnx, char *mtype )
{
  static char *data = NULL;
  static int len = 0;

  logger("http_reply_style: %s\n", g_style );

  // force to reply with uncompressed data
  cnx->req.accept_deflate = 0;
  
  if ( g_style[0] == '@' ) {
    if ( !strcmp( g_style + 1, "basic" ) ||
	 !strcmp( g_style + 1, "bright" ) ||
	 !strcmp( g_style + 1, "dark" ) ||
	 !strcmp( g_style + 1, "positron" )) {
      
      char style[48] = "styles/openmaptiles/";
      char *p;
      
      strcat( style, g_style+1 );
      strcat( style, "/style.json");

      p = arch_data( style, &g_zero );
      if ( p ) {
	data = emalloc(strlen(p)+32);
	sprintf( data, p, g_port );
	len = strlen(data);
      }
      else {
	fprintf( stderr, "Unknown predefined style '%s'. Giving up...\n", g_style+1 );
	exit(1);
      }
    }
    else if ( !strcmp( g_style + 1, "auto" )) {
      data = mbtiles_auto_style_json( g_sql, &len );
    }
    else {
      fprintf( stderr, "Unknown predefined style '%s'.\n", g_style );
      return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
    }
  }
  else {
    FILE *fin = fopen( g_style, "r" );
    char *p;
      
    if ( fin == NULL ) {
      perror( g_style );
      exit(1);
    }
    if ( fseek( fin, 0L, SEEK_END) == -1 ) {
      perror( g_style );
      exit(1);
    }
    len = (int) ftell(fin);
    rewind(fin);

    if ( len > 0 ) {
      int n, t = 0;
      p = (char*) emalloc(len);
      while( t < len ) {
	n = fread( p + t, 1, (len-t > BLKIO) ? BLKIO : len - t, fin);
	if ( n == -1 ) {
	  perror( g_style );
	  exit(1);
	}
	t += n;
      }
      data = emalloc(len + 4);
      sprintf( data, p, g_port );
      len = strlen(data);
      free(p);
    }
    else {
      fprintf( stderr, "Unknown predefined style '%s'.\n", g_style );
      return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
    }
    
    fclose(fin);
  }

  http_reply_data( cnx, mtype, data, len );
}

/* --------------------------------------------------------------------------
 *  Reply with a tile
 * --------------------------------------------------------------------------*/
int http_reply_tile( cnx_t *cnx, char *mtype, int x, int y, int z, int gzip)
{
  char *data = NULL;
  int len = 0;

  logger("http_reply_tile: %d/%d/%d (%s%s)\n", z, x, y, mtype, gzip ? " compressed" : "");

  cnx->req.accept_deflate = 0;  // data is identity or gzip but not deflate
  data = mbtiles_read( g_sql, z, x, y, &len );
  if ( data ) {
    return http_reply_data_ex( cnx, mtype, data, len,
			       gzip ? "Content-encoding: gzip" : NULL,
			       NULL );
  }
  else {
    return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
  }
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

/* --------------------------------------------------------------------------
 *  Returns numeric value of an hexadecimal digit
 * --------------------------------------------------------------------------*/
int alphaval( char c )
{
  if ( c >= '0' && c <= '9' ) return c - '0';
  c = toupper(c);
  if ( c >= 'A' && c <= 'F' ) return c + 10 - 'A';
  return 0;
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
int http_reply( cnx_t *cnx )
{
  char *data = NULL, *mtype, *k;
  int n = 0, l, x, y, z;
  
  if ( cnx->urlp.field_set & (1 << UF_QUERY) ) {
    return http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
  }
  if ( cnx->urlp.field_set & (1 << UF_FRAGMENT) ) {
    return http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
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
    logger("URL %.*s\n", l, k );

    // special case for "/tiles/*" URL which are served
    // using mbtiles file content
    if ( !strncmp( k, "tiles/", 6) ) {
      if ( isdigit(k[6]) ) {
	n = sscanf( k, "tiles/%d/%d/%d.", &z, &x, &y );
	if ( n == 3 ) {
	  l = strlen(k) - 4;
	  if ( !strcmp( k+l, ".pbf") ) {
	    return http_reply_tile( cnx, "application/x-protobuf", x, y, z, 1);	    
	  }
	  else if ( !strcmp( k+l, ".jpg") ) {
	    return http_reply_tile( cnx, "image/jpeg", x, y, z, 0);
	  }
	  else if ( !strcmp( k+l, ".png") ) {
	    return http_reply_tile( cnx, "image/png", x, y, z, 0);
	  }
	  // reached for usupported format
	}
	
	return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
      }
      else {
	if ( !strcmp( k, "tiles/tiles.json") ) {
	  mtype = http_mimetype(k,l);
	  return http_reply_tiles_json( cnx, mtype );
	}
	else {
	  return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
	}
      }
    }
    else {
      // get data maybe compressed if gzip encoding is supported
      data = arch_data_ex( k, l, &cnx->req.accept_deflate );
    }
    
    if ( data ) {
      int len = arch_size_ex( k, l, &cnx->req.accept_deflate );
      mtype = http_mimetype(k,l);
      return http_reply_data( cnx, mtype, data, len);
    }
    else if ( !strcmp( k, "style.json") ) {
      return http_reply_style( cnx, "application/json" );
    }
    else {
      return http_reply_error( cnx, HTTP_STATUS_NOT_FOUND );
    }
    break;
    
  default:
    return http_reply_error( cnx, HTTP_STATUS_BAD_REQUEST );
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
  logger("-------------------------------------\n");
  return 0;
}

/* --------------------------------------------------------------------------
 *  Called when HTTP headers parsing is completed
 * --------------------------------------------------------------------------*/
int headers_complete_cb( http_parser *p)
{
  cnx_t *cnx = (cnx_t*) p->data;
  req_t *req = &cnx->req;
  int i;
  // mark end of headers
  if ( req->nhv % 2 ) req->nhv++;
  // check if deflate compression method supported
  for (i = 0; i < req->nhv; i+=2) {
    if (!strcasecmp(req->hv[i], "accept-encoding")) {
      if (strstr(req->hv[i+1], "deflate") != NULL) {
	logger ("deflate encoding supported");
	req->accept_deflate = 1;
      }
      break;
    }
  }
  return 0;
}

/* --------------------------------------------------------------------------
 *  Debug helper function dumps URL
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

  logger( req->url );
  for( i = 0; i < req->nhv; i+=2 ) {
    logger("%s -> %s\n", req->hv[i], req->hv[i+1]);
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

  logger("-------------------------------------\n");
  
  return 0;
}

/* --------------------------------------------------------------------------
 *  Called when URL is parsed
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
 *  Called when a header field name is parsed
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
 *  Called when a header value is parsed
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
 *  Opens TCP server listening on port 'portno'
 *  Binds it to 0.0.0.0.
 *  Exits on failure
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
 *  Accept a new connection if possible
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
 *  Close a connection
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
 *  Reads input and parses incoming HTTP requests
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
    fprintf( stderr, "remote end closed connection.\n" );
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
  mbtiles_close( g_sql );
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
  fprintf( fout, "\t -x            Open web browser.\n");
  fprintf( fout, "\t -v            Be verbose.\n");
  fprintf( fout, "\t -p port       Sets port number to listen on.\n");
  fprintf( fout, "\t -m mbtiles    Sets mbtile file to display.\n");
  fprintf( fout, "\t -s style      Sets style.json file to use for rendering.\n");

  exit( fmt ? 1 : 0 );
}

/* --------------------------------------------------------------------------
 *  Main program
 * --------------------------------------------------------------------------*/
int main( int argc, char **argv )
{
#define F_PORT  0x01
#define F_MAP   0x02
#define F_STYLE 0x04
#define F_EXEC  0x08
#define F_VERB  0x10
  int opt, flags = 0;
  
  signal( SIGPIPE, SIG_IGN );
  atexit( byebye );
  
  while ((opt = getopt(argc, argv, "hxvp:m:s:")) != -1) {
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
    case 'v':
      if ( flags & F_VERB ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      flags |= F_VERB;
      break;
    case 'p':
      if ( flags & F_PORT ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      g_port = atoi(optarg);
      flags |= F_PORT;
      break;
    case 'm':
      if ( flags & F_MAP ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      g_map = optarg;
      flags |= F_MAP;
      break;
    case 's':
      if ( flags & F_STYLE ) {
	usage( "option '-%c' can be specified only once.\n", opt);
      }
      g_style = optarg;
      flags |= F_STYLE;
      break;
    default:
      usage("unrecognized option.\n");
    }
  }

  if ( !(flags & F_MAP) ) {
    usage( "option '%s' is mandatory.\n", "-m" );
  }
  if ( !(flags & F_STYLE) ) {
    g_style = "@auto";
  }
  if ( flags & F_VERB ) {
    g_quiet = 0;
  }
  
  serverfd = server(g_port);

  g_sql = mbtiles_open( g_map ); 
  mbtiles_tiles_json( g_sql, NULL );

  if ( flags & F_EXEC ) {
    char cmd[64];
    sprintf( cmd, "xdg-open http://127.0.0.1:%d", g_port );
    system( cmd );
  }
  else {
    printf("Visit http://127.0.0.1:%d", g_port );
  }
  
  selectloop();
  
  return 0;
}

