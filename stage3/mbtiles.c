#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <json.h>

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

// externals
extern int g_port;
extern char *arch_data( char *k );


// forward
char *mbtiles_read( void *s, int z, int x, int y, int *len );
char *mbtile_auto_style_json( void *dbh, int *len );

/* --------------------------------------------------------------------------
 *  Open mbtiles sqlite database and returns a handle to it
 *  The handle is a prepared statement used to query tile data
 * --------------------------------------------------------------------------*/
void *mbtiles_open( char *path )
{
  sqlite3_stmt *stmt;
  sqlite3 *db;
  int rc;

  // open database
  rc = sqlite3_open( path, &db );
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  // check metadata
  //@todo

  // prepare statement
#define QUERY "SELECT tile_data FROM tiles WHERE zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3"
  rc = sqlite3_prepare_v2( db, QUERY, strlen(QUERY), &stmt, NULL);
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot prepare query: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }
#undef QUERY
  
  return (void*) stmt;
}

/* --------------------------------------------------------------------------
 *  Close mbtiles
 * --------------------------------------------------------------------------*/
void mbtiles_close( void *dbh )
{
  sqlite3 *db = sqlite3_db_handle( dbh );
  sqlite3_reset( dbh );
  sqlite3_finalize( dbh );
  sqlite3_close( db );
}

/* --------------------------------------------------------------------------
 *  Reads a tile
 * --------------------------------------------------------------------------*/
char *mbtiles_read( void *dbh, int z, int x, int y, int *len )
{
  static char *blob = NULL;
  sqlite3_stmt *stmt = (sqlite3_stmt*) dbh;
  int rc;
  
  rc = sqlite3_reset( stmt );
  if ( rc != SQLITE_OK ) {
    fprintf( stderr, "Failed to reset.\n" );
  }
  
  rc = sqlite3_bind_int( stmt, 1, z);
  if ( rc != SQLITE_OK ) {
    fprintf( stderr, "Failed to bind column.\n" );
  }
  rc = sqlite3_bind_int( stmt, 2, x);
  if ( rc != SQLITE_OK ) {
    fprintf( stderr, "Failed to bind column.\n" );
  }
  y = (1 << z) - 1 - y;
  rc = sqlite3_bind_int( stmt, 3, y);
  if ( rc != SQLITE_OK ) {
    fprintf( stderr, "Failed to bind column.\n" );
  }

  if ( sqlite3_step( stmt ) == SQLITE_ROW ) {
    *len = sqlite3_column_bytes( stmt, 0 );
    return (char*) sqlite3_column_blob( stmt, 0);
  }
  else {
    sqlite3 *db = sqlite3_db_handle( stmt );
    fprintf( stderr, "Failed to retreive tile %d/%d/%d : %s\n", z,x,y, sqlite3_errmsg(db));
    *len = 0;
    return NULL;
  }
}

/* --------------------------------------------------------------------------
 *  Utility
 * --------------------------------------------------------------------------*/
static char *skip( char* s)
{
  while( *s && (isspace(*s) || (*s == ','))) ++s;
  return (char*) s;
}

/* --------------------------------------------------------------------------
 *  Generates 'tiles/tiles.json' from 'metadata' table of mbtiles
 * --------------------------------------------------------------------------*/
char *mbtiles_tiles_json( void *dbh, int *len )
{
  static char *data = NULL;
  static int  dlen = 0;

  if ( data == NULL ) {
    sqlite3 *db = sqlite3_db_handle( dbh );
    sqlite3_stmt *stmt;
    struct json_object *o, *a;
    char *k, *v, *s, *format = NULL, url[64];
    double d;
    int i, rc;

#define QUERY "SELECT * FROM metadata"
    rc = sqlite3_prepare_v2( db, QUERY, strlen(QUERY), &stmt, NULL);
    if ( rc != SQLITE_OK ) {
      fprintf(stderr, "Cannot prepare query: %s\n", sqlite3_errmsg(db));
      sqlite3_close(db);
      return NULL;
    }
#undef QUERY
    
    o = json_object_new_object();

    json_object_object_add( o, "tilejson", json_object_new_string("2.0.0") );
    
    while( (rc = sqlite3_step( stmt )) == SQLITE_ROW ) {
      k = (char *) sqlite3_column_text( stmt, 0 );
      v = (char *) sqlite3_column_text( stmt, 1 );

      if ( !strcmp(k, "name") ) {
	json_object_object_add( o, k, json_object_new_string(v));
      }
      if ( !strcmp(k, "attribution") ) {
	json_object_object_add( o, k, json_object_new_string(v));
      }
      if ( !strcmp(k, "description") ) {
	json_object_object_add( o, k, json_object_new_string(v));
      }
      if ( !strcmp(k, "version") ) {
	json_object_object_add( o, k, json_object_new_string(v));
      }
      if ( !strcmp(k, "format") ) {
	json_object_object_add( o, k, json_object_new_string(v));
	if ( !strcmp(v, "jpg") ) format = "jpg";
	if ( !strcmp(v, "png") ) format = "png";
	if ( !strcmp(v, "pbf") ) format = "pbf";
      }
      if ( !strcmp(k, "minzoom") || !strcmp(k, "maxzoom") ) {
	long d;
	s = skip(v);
	d = strtol( s, &s, 10 );
	json_object_object_add( o, k, json_object_new_int(d));
      }
      if ( !strcmp(k, "bounds") ) {
	struct json_object* a;
	a = json_object_new_array();
	for( s = v, i = 0; (*s != 0) && (i < 4); ++i ) {
	  s = skip(s);
	  d = strtod(s, &s);
	  json_object_array_add( a, json_object_new_double(d));
	}
	if ( i < 4 ) {
	  fprintf( stderr, "unable to parse map bounds.\n" );
	  exit(1);
	}
	json_object_object_add( o, k, a );
      }
      if ( !strcmp(k, "center") ) {
	struct json_object* a;
	a = json_object_new_array();
	for( s = v, i = 0; (*s != 0) && (i < 3); ++i ) {
	  s = skip(s);
	  d = strtod(s, &s);
	  json_object_array_add( a, json_object_new_double(d));
	}
	if ( i < 3 ) {
	  fprintf( stderr, "unable to parse map bounds.\n" );
	  exit(1);
	}
	json_object_object_add( o, k, a );
      }
      if ( !strcmp(k, "json") ) {
	enum json_tokener_error error;
	struct json_object *so, *layers;
	json_bool jb;
	
	so = json_tokener_parse_verbose( v, &error );
	if ( error != json_tokener_success ) {
	  fprintf( stderr, "failed to parse metadata json field: %s\n",
		   json_tokener_error_desc( error ));
	  exit(1);
	}

	jb = json_object_object_get_ex( so, "vector_layers", &layers );
	if ( jb == TRUE ) {
	  json_object_object_add( o, "vector_layers", json_object_get(layers));
	}
	else {
	  fprintf( stderr, "Missing field 'vector_layers'.\n");
	}

	// free the subobject
	json_object_put(so);
      }
    }

    sqlite3_reset( stmt );
    sqlite3_finalize( stmt );

    // add the "tiles" property
    // @todo : diff between raster and vectorial
    a = json_object_new_array();
    sprintf( url, "http://127.0.0.1:%d/tiles/{z}/{x}/{y}.%s", g_port, format );
    json_object_array_add( a, json_object_new_string(url));
    json_object_object_add( o, "tiles", a );
    
    data = (char*) json_object_to_json_string_ext( o, JSON_C_TO_STRING_PRETTY );
    dlen = strlen(data);
  }
  
  if ( len != NULL ) {
    *len = dlen;
  }
  return data;
}


/* --------------------------------------------------------------------------
 *  Automatically generate style.json mbtiles files
 * --------------------------------------------------------------------------*/
char *mbtiles_auto_raster_style_json( void *dbh, int *len )
{
  char *style = "styles/auto/raster/style.json";
    
  char *data = arch_data( style );
  if ( data ) {
    if (len) *len = strlen(data);
  }
  else {
    fprintf( stderr, "Unknown style '%s'. Giving up...\n", style );
    exit(1);
  }
  return data;
}

/* --------------------------------------------------------------------------
 *  Guess if it is an openmaptile file
 * --------------------------------------------------------------------------*/
char *mbtiles_is_openmaptiles( void *dbh )
{
  return 0;
}

/* --------------------------------------------------------------------------
 *  Automatically generate style.json mbtiles files
 * --------------------------------------------------------------------------*/
char *mbtiles_auto_vectorial_style_json( void *dbh, int *len )
{
  // check if an openmaptiles style can be used
  // better that we can do here !
  if (mbtiles_is_openmaptiles (dbh)) {
    char *style = "styles/openmaptiles/positron/style.json";
    
    char *data = arch_data( style );
    if ( data ) {
      if (len) *len = strlen(data);
    }
    else {
      fprintf( stderr, "Unknown style '%s'. Giving up...\n", style );
      exit(1);
    }
    return data;
  }
  else {
    
  }
}

/* --------------------------------------------------------------------------
 *  Automatic style generation
 * --------------------------------------------------------------------------*/
char *mbtiles_auto_style_json( void *dbh, int *len )
{
  static char *data = NULL;
  static int  dlen = 0;

  if ( data == NULL ) {
    sqlite3 *db = sqlite3_db_handle( dbh );
    sqlite3_stmt *stmt;
    int rc, raster = 0;
    char *k, *v;

#define QUERY "SELECT * FROM metadata"
    rc = sqlite3_prepare_v2( db, QUERY, strlen(QUERY), &stmt, NULL);
    if ( rc != SQLITE_OK ) {
      fprintf(stderr, "Cannot prepare query: %s\n", sqlite3_errmsg(db));
      sqlite3_close(db);
      return NULL;
    }
#undef QUERY
    
    while( (rc = sqlite3_step( stmt )) == SQLITE_ROW ) {
      k = (char *) sqlite3_column_text( stmt, 0 );
      v = (char *) sqlite3_column_text( stmt, 1 );

      if ( !strcmp(k, "format") ) {
	if ( !strcmp(v, "jpg") ) raster = 1;
	if ( !strcmp(v, "png") ) raster = 1;
	if ( !strcmp(v, "pbf") ) raster = 0;
	break;
      }
    }

    sqlite3_reset( stmt );
    sqlite3_finalize( stmt );

    if ( raster ) {
      data = mbtiles_auto_raster_style_json( dbh, &dlen );
    }
    else {
      fprintf(stderr, "Cannot automatically generate style for vector tiles.\n");
      return NULL;
    }
  }
  
  if ( len != NULL ) {
    *len = dlen;
  }
  return data;
}
