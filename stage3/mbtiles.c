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

#include "archrt.h"

// externals
extern int g_port;

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
  int zero = 0;
  char *style = "styles/auto/raster/style.json";
    
  char *data = arch_data(style, &zero);
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
  //@todo: implement me !!
  return 0;
}

/* --------------------------------------------------------------------------
 *  Create JSON object representing a layer
 *  'src'  is the name of the source 'vector_layer'
 *  'type' is the type of the layer 'line' or 'fill'
 *  'rgb'  is the color that will be used for rendering
 *
 * Generated object is as follow :
 *    {
 *         "id": "<src>-<type>",
 *         "type": "<type>",
 *         "paint": {
 *             "<type>-color": "<rgb>",
 *         },
 *         "filter": [
 *             "all"
 *         ],
 *         "layout": {
 *             "visibility": "visible"
 *         },
 *         "source": "mbtiles",
 *         "maxzoom": 24,
 *         "source-layer": "<src>"
 *     } 
 * --------------------------------------------------------------------------*/
struct json_object *mklayer( const char *src, char *type, char *rgb )
{
  char layerid[64], prop[32];
  struct json_object *layer, *paint, *filter_all, *layout;
  
  snprintf( layerid, sizeof(layerid), "%s-%s", src, type);
  layerid[sizeof(layerid)-1] = 0;
  
  layer = json_object_new_object();
  
  json_object_object_add( layer, "id",      json_object_new_string(layerid));
  json_object_object_add( layer, "type",    json_object_new_string(type));
  json_object_object_add( layer, "source",  json_object_new_string("mbtiles"));
  json_object_object_add( layer, "source-layer", json_object_new_string(src));
  json_object_object_add( layer, "minzoom", json_object_new_int(1));
  json_object_object_add( layer, "maxzoom", json_object_new_int(24));

  paint = json_object_new_object();
  json_object_object_add( layer, "paint",   json_object_get(paint));
  snprintf( prop, sizeof(prop), "%s-color", type);
  prop[sizeof(prop)-1] = 0;
  json_object_object_add( paint, prop, json_object_new_string(rgb));
  
  filter_all = json_object_new_array();
  json_object_object_add( layer, "filter",  json_object_get(filter_all));
  json_object_array_add( filter_all, json_object_new_string("all"));

  layout = json_object_new_object();
  json_object_object_add( layer, "layout",  json_object_get(layout));
  json_object_object_add( layout, "visibility", json_object_new_string("visible"));

  return layer;
}

/* --------------------------------------------------------------------------
 *  Automatically generate style.json for mbtiles files
 *  Tries to fall back to openmaptile bright style if the mbtiles
 *  database looks like an mbtiles database.
 *
 *  Otherwise :
 *  For each layer
 * --------------------------------------------------------------------------*/
char *mbtiles_auto_vectorial_style_json( void *dbh, int *len )
{
  int zero = 0;
  char *data = NULL;
  
  // check if an openmaptiles style can be used
  // better that we can do here !
  if (mbtiles_is_openmaptiles (dbh)) {
    char *style = "styles/openmaptiles/bright/style.json";
    
    data = arch_data( style, &zero );
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
    struct json_object *tiles, *layers, *style, *srcs, *src, *srclayers, *layer;
    enum json_tokener_error error;
    char *tiles_str;
    json_bool jb;
    int i, n;

    tiles_str = mbtiles_tiles_json( dbh, NULL );
	
    tiles = json_tokener_parse_verbose( tiles_str, &error );
    if ( error != json_tokener_success ) {
      fprintf( stderr, "failed to parse tiles.json: %s\n",
	       json_tokener_error_desc( error ));
      exit(1);
    }

    jb = json_object_object_get_ex( tiles, "vector_layers", &srclayers );
    if ( jb != TRUE ) {
      fprintf( stderr, "Missing field 'vector_layers'.\n");
      exit(1);
    }
    n = json_object_array_length( srclayers );
    if ( n <= 0 ) {
      fprintf( stderr, "'vector_layers' is empty.\n");
      exit(1);
    }
    
    style = json_object_new_object();

    json_object_object_add( style, "version", json_object_new_int(8) );
    json_object_object_add( style, "id",      json_object_new_string("mbtiles") );
    json_object_object_add( style, "name",    json_object_new_string("mbtiles") );
    json_object_object_add( style, "glyphs",  json_object_new_string("font/{fontstack}/{range}.pbf"));
    //@todo: is this needed ? I don't think so
    //json_object_object_add( style, "bearing", json_object_new_double(0) );
    //json_object_object_add( style, "pitch",   json_object_new_double(0) );

    srcs = json_object_new_object();
    json_object_object_add( style, "sources",  json_object_get(srcs));
      
    src = json_object_new_object();
    json_object_object_add( srcs, "mbtiles",  json_object_get(src));
      
    json_object_object_add( src, "url",    json_object_new_string("tiles/tiles.json") );
    //@todo: handle raster too
    json_object_object_add( src, "type",   json_object_new_string("vector") );

    // create layers
    layers = json_object_new_array();
    json_object_object_add( style, "layers",  json_object_get(layers));
    for( i = 0; i < n; ++i) {
      struct json_object *o = json_object_array_get_idx(srclayers, i);
      struct json_object *id;
      
      jb = json_object_object_get_ex( o, "id", &id );
      if ( jb != TRUE ) {
	fprintf( stderr, "Missing field 'id'.\n");
	exit(1);
      }
      const char *src = json_object_get_string(id);
      
      layer = mklayer( src, "fill", "#8080FF" /*fillcolor(color)*/);
      json_object_array_add( layers, layer );
      layer = mklayer( src, "line", "#3030FF" /*linecolor(color)*/);
      json_object_array_add( layers, layer );
    }
    
    data = (char*) json_object_to_json_string_ext( style, JSON_C_TO_STRING_PRETTY );
    data = strdup(data);

    puts(data);
    
    // free JSON objects
    //@todo json_object_put(vector_layers);
    json_object_put(srclayers);
    json_object_put(style);

    if (len != NULL) {
      *len = strlen(data);
    }
    return data;
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
      data = mbtiles_auto_vectorial_style_json( dbh, &dlen);
    }
  }
  
  if ( len != NULL ) {
    *len = dlen;
  }
  return data;
}
