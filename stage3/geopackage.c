#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include <json.h>

#define GPKG_APPID_1_2 0x47504B47   /* this is ASCII for "GPKG" */
#define GPKG_APPID_1_0 0x47503130   /* this is ASCII for "GP10" */

/* --------------------------------------------------------------------------
 *  Check existence of table and its columns
 * --------------------------------------------------------------------------*/
static int sqlut_check_table (sqlite3 *db, char *tname, ...)
{
  sqlite3_stmt *stmt;
  char *colnam1, *colnam2;
  char  query[128];
  int rc, res = 0;
  va_list va;
  
  snprintf( query, sizeof(query),  "pragma table_info(%s);", tname);
  query[ sizeof(query)-1 ] = 0;

  rc = sqlite3_prepare_v2( db, query, strlen(query), &stmt, NULL);
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot prepare query '%s': %s\n", QUERY, sqlite3_errmsg(db));
    return 0;
  }
  
  va_start(va, tname);
  res = 1;
  while ( sqlite3_step( stmt ) == SQLITE_ROW ) {
    colnam1 = sqlite3_column_bytes( stmt, 1 );
    colnam2 = va_arg(va, char*);
    if (colnam2 == NULL) {
      fprintf( stderr, "Table '%s' contains extra column '%s'.\n",
	       tname, colnam1);
    }
    if (strcmp( colnam1, colnam2)) {
      fprintf( stderr, "Table '%s' does not contain column '%s' ('%s' seen instead).\n",
	       tname, colnam2, colnam1);
      res = 0;
      break;
    }
  }
  if (res) {
    colnam2 = va_arg(va, char*);
    if (colnam2 != NULL) {
      fprintf( stderr, "Table '%s' doesn't contain expect column '%s'.\n",
	       tname, colnam2);
      res = 0;
    }
  }
  
  sqlite3_reset( stmt );
  sqlite3_finalize( stmt );
  va_end(va);

  return res;
}

/* --------------------------------------------------------------------------
 *  Retrieve pragma value
 * --------------------------------------------------------------------------*/
static int sqlut_get_value( sqlite3 db, char *name, int *pval)
{
  sqlite3_stmt *stmt;
  char  query[64];
  int rc, res = 0;

  snprintf( query, sizeof(query),  "pragma %s;", name);
  query[ sizeof(query)-1 ] = 0;

  rc = sqlite3_prepare_v2( db, query, strlen(query), &stmt, NULL);
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot prepare query '%s': %s\n", QUERY, sqlite3_errmsg(db));
    return res;
  }

  while ( sqlite3_step( stmt ) == SQLITE_ROW ) {
    res++;
    rc = sqlite3_column_int(stmt, 0);
  }
  res = (res == 1);
  if (res) {
    *pval = rc;
  }
  
  sqlite3_reset( stmt );
  sqlite3_finalize( stmt );
  return res;
}

/* --------------------------------------------------------------------------
 *  Open geopackage sqlite database and returns a handle to it
 *  The handle is a prepared statement used to query tile data
 * --------------------------------------------------------------------------*/
void *gpkg_open( char *path )
{
  char query[256];
  sqlite3_stmt *stmt;
  sqlite3 *db;
  int rc, application_id = 0, user_version = 0;

  // open database
  rc = sqlite3_open( path, &db );
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }

  // check metadata
  rc = sqlut_get_value( db, "application_id", &application_id );
  if (!rc) {
    fprintf( stderr, "File '%s' doesn't contain valid geopackage data.\n", path);
    fprintf( stderr, "Pragma 'application_id' not found.\n");
    sqlite3_close(db);
    return NULL;
  }
  
  if (application_id == GPKG_APPID_1_2) {
    rc = sqlut_get_value( db, "user_version", &user_version );
    if (!rc) {
      fprintf( stderr, "File '%s' doesn't contain valid geopackage data.\n", path);
      fprintf( stderr, "Pragma 'user_version' not found.\n");
      sqlite3_close(db);
      return NULL;
    }
  }

  rc = sqlut_check_table (db, "gpkg_contents",
			  "table_name",
			  "data_type",
			  "identifier",
			  "description",
			  "last_change",
			  "min_x", "min_y", "max_x", "max_y",
			  "srs_id",
			  NULL);
  if (!rc) {
    fprintf( stderr, "File '%s' doesn't contain valid geopackage data.\n", path);
    sqlite3_close(db);
    return NULL;
  }
  
  rc = sqlut_check_table (db, "gpkg_spatial_ref_sys",
			  "srs_name",
			  "srs_id",
			  "organization",
			  "organization_coordsys_id",
			  "definition",
			  "description",
			  NULL);
  if (!rc) {
    fprintf( stderr, "File '%s' doesn't contain valid geopackage data.\n", path);
    sqlite3_close(db);
    return NULL;
  }
  

  // prepare statement
  snprintf( query, sizeof(query), "SELECT tile_data FROM %s WHERE zoom_level = ?1 AND tile_column = ?2 AND tile_row = ?3", tname );
  rc = sqlite3_prepare_v2( db, query, query, &stmt, NULL);
  if ( rc != SQLITE_OK ) {
    fprintf(stderr, "Cannot prepare query: %s\n", sqlite3_errmsg(db));
    sqlite3_close(db);
    return NULL;
  }
#undef QUERY
  
  return (void*) stmt;
}

/* --------------------------------------------------------------------------
 *  Generates 'tiles/tiles.json' from 'gpkg_contents' table of mbtiles
 * --------------------------------------------------------------------------*/
char *gpkg_tiles_json( void *dbh, int *len )
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

#define QUERY "SELECT * FROM gpkg_contents"
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
