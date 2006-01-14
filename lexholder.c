/* lexholder.c
 *
 * Lexicon database holding utility.
 */

/*
 * Copyright (C) 2006 Igor B. Poretsky <master@goga.energo.ru>
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "coder.h"

#if defined(FBSD_DATABASE)

#include <db.h>

static int db_access(const char *path, int mode)
{
  int status;
  char *db_file = malloc(strlen(path) + 4);
  if (!db_file)
    {
      (void)fprintf(stderr, "Memory allocation error\n");
      exit(EXIT_FAILURE);
    }
  (void)strcpy(db_file, path);
  (void)strcat(db_file, ".db");
  status = access(db_file, mode);
  free(db_file);
  return status;
}

#elif defined(BERKELEYDB)

#include <db_185.h>

#define db_access access

#else

#include <gdbm-ndbm.h>

#define RET_SUCCESS 0
#define RET_SPECIAL 1

static int db_access(const char *path, int mode)
{
  int status;
  char *dir_file = malloc(strlen(path) + 8);
  char *pag_file = malloc(strlen(path) + 8);
  if ((!dir_file) || (!pag_file))
    {
      (void)fprintf(stderr, "Memory allocation error\n");
      exit(EXIT_FAILURE);
    }
  (void)strcpy(dir_file, path);
  (void)strcpy(pag_file, path);
  (void)strcat(dir_file, ".dir");
  (void)strcat(pag_file, ".pag");
  status = access(dir_file, mode) || access(pag_file, mode);
  free(dir_file);
  free(pag_file);
  return status;
}

#endif

#define RET_FAILURE -1


/* Data size limits */
#define MAX_KEY_SIZE 50
#define MAX_RECORD_SIZE 120

/* Command line error codes */
#define CS_CONFLICT 1
#define NO_DB_FILE 2


#ifdef FBSD_DATABASE	/* this needs to be explicit  */
HASHINFO openinfo =
  {
    128,           /* bsize */
    8,             /* ffactor */
    500000,         /* nelem */
    2048 * 1024,   /* cachesize */
    NULL,          /* hash */
    0              /* lorder */
  };
#endif

static void usage(const char *name)
{
  (void)fprintf(stderr, "Lexical database holding utility.\n");
  (void)fprintf(stderr, "Use it as follows:\n");
  (void)fprintf(stderr, "Filling the DB:\t");
  (void)fprintf(stderr, "%s [-q|-v] [-a] [-r] [-f <file>] <db_path>\n",
		name);
  (void)fprintf(stderr, "Extracting the DB content:\t");
  (void)fprintf(stderr, "%s -l [-f <file>] <db_path>\n", name);
  (void)fprintf(stderr, "Searching the DB:\t");
  (void)fprintf(stderr, "%s -s <key> [-q] [-f <file>] <db_path>\n", name);
  (void)fprintf(stderr, "Deleting entry:\t");
  (void)fprintf(stderr, "%s -d <key> <db_path>\n", name);
  (void)fprintf(stderr, "Getting this help:\t");
  (void)fprintf(stderr, "%s -h\n\n", name);
  (void)fprintf(stderr, "When filling and updating the database,\n");
  (void)fprintf(stderr, "new records are read from the standard input.\n");
  (void)fprintf(stderr, "When extracting data from the database,\n");
  (void)fprintf(stderr, "The result is printed to the standard output.\n");
  (void)fprintf(stderr,
		"This behaviour can be changed by the \"-f\" switch.\n\n");
  (void)fprintf(stderr, "options:\n");
  (void)fprintf(stderr, "-l -- List database content\n");
  (void)fprintf(stderr, "-s <key> -- Search specified key\n");
  (void)fprintf(stderr, "-d <key> -- Delete record for specified key\n");
  (void)fprintf(stderr, "-f <file> -- Use specified file ");
  (void)fprintf(stderr, "instead of standard input or output\n");
  (void)fprintf(stderr, "-a -- Store all records (redundant too)\n");
  (void)fprintf(stderr, "-r -- Replace mode\n");
  (void)fprintf(stderr, "-q -- Be more quiet than usual\n");
  (void)fprintf(stderr, "      (don't print search results as well\n");
  (void)fprintf(stderr, "      as warnings about redundant ");
  (void)fprintf(stderr, "and duplicate records)\n");
  (void)fprintf(stderr, "-v -- Be more verbose than usual\n");
  (void)fprintf(stderr, "      (Print messages about work stages\n");
  (void)fprintf(stderr, "      and final statistical information)\n");
  (void)fprintf(stderr, "-h -- This help\n\n");
  return;
}


int main(int argc, char *argv[])
{
#if defined(FBSD_DATABASE)
  DBT inKey, inVal;
  DB * db;
#elif defined(BERKELEYDB)
  DBT inKey, inVal;
  DB *db, *tmpdb;
#else
  DBM * db;
  datum inKey, inVal;
#endif

  char *s = NULL, *d = NULL, line[256], packed_key[256], packed_data[256];
  char *db_path = NULL, *srcf = NULL;

  int ret, key_size, data_size, n, i = 0;
  int redundant = 0, invalid = 0, duplicate = 0;
  int verbose = 0, quiet = 0, replace_mode = 0, allrecs = 0;

  /* Parse command line */
  if(argc==1)
    {
      usage(argv[0]);
      return EXIT_FAILURE;
    }
  ret = NO_DB_FILE;
  while((n = getopt(argc,argv,"f:d:s:larqvh")) != -1)
    switch(n)
      {
	case 'f':
	  if (strcmp(optarg, "-"))
	    srcf = optarg;
	  break;
	case 'd':
	  if (d || s) ret = CS_CONFLICT;
	  else d = optarg;
	  break;
	case 's':
	  if (d || s) ret = CS_CONFLICT;
	  else s = optarg;
	  break;
	case 'l':
	  if (d || s) ret = CS_CONFLICT;
	  else s = line;
	  break;
	case 'a':
	  allrecs = 1;
	  break;
	case 'r':
	  replace_mode = 1;
	  break;
	case 'q':
	  if (verbose) ret = CS_CONFLICT;
	  else quiet = 1;
	  break;
	case 'v':
	  if (quiet) ret = CS_CONFLICT;
	  else verbose = 1;
	  break;
	case 'h':
	  usage(argv[0]);
	  return EXIT_SUCCESS;
	default:
	  usage(argv[0]);
	  return EXIT_FAILURE;
      }
  if (optind && argv[optind])
    {
      db_path = argv[optind];
      ret = 0;
    }
  switch (ret)
    {
      case CS_CONFLICT:
	(void)fprintf(stderr,
		      "Simultaneous use of mutually exclusive switches\n");
	return EXIT_FAILURE;
      case NO_DB_FILE:
	(void)fprintf(stderr, "DB file must be specified\n");
	return EXIT_FAILURE;
      default:
	break;
    }

  if (s) /* Database reading actions */
    {
      if (db_access(db_path, F_OK | R_OK))
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}
      if (srcf)
	if (!freopen(srcf, "w", stdout))
	  {
	    (void)fprintf(stderr, "Cannot open %s for output\n", srcf);
	    return EXIT_FAILURE;
	  }
#if defined(BERKELEYDB)
      db = dbopen(db_path, O_RDONLY, 0, DB_BTREE, NULL);
#elif defined(FBSD_DATABASE)
      db = dbopen(db_path, O_RDONLY, 0, DB_HASH, &openinfo);
#else
      db = dbm_open(db_path, O_RDONLY, 0);
#endif
      if (!db)
	{
	  (void)fprintf(stderr, "Cannot open the database %s\n", db_path);
	  return EXIT_FAILURE;
	}
      if (s == line) /* List full database content */
	{
	  n = 0;
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
	  while (!db->seq(db, &inKey, &inVal, R_NEXT))
	    {
	      if (unpack_key(inKey.data, inKey.size, packed_key, MAX_KEY_SIZE))
#else
	  for (inKey = dbm_firstkey(db); inKey.dptr; inKey = dbm_nextkey(db))
	    {
	      inVal = dbm_fetch(db, inKey);
	      if (unpack_key(inKey.dptr, inKey.dsize, packed_key, MAX_KEY_SIZE))
#endif
		{
		  (void)fprintf(stderr, "Database corruption\n");
		  ret = RET_FAILURE;
		  break;
		}
	      (void)strcpy(packed_data, packed_key);
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
	      unpack_data(packed_data, inVal.data, inVal.size);
#else
	      unpack_data(packed_data, inVal.dptr, inVal.dsize);
#endif
	      (void)printf("%s %s\n", packed_key, packed_data);
	      n++;
	    }
	  if (!quiet)
	    {
	      (void)fprintf(stderr, "%i record", n);
	      if (n != 1) (void)fputc('s', stderr);
	      (void)fprintf(stderr,
			    " have been extracted from the database\n");
	    }
	}
      else /* Search record for specified key */
	{
	  key_size = pack_key(s, packed_key);
	  if(key_size > 0)
	    {
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
	      inKey.data = packed_key;
	      inKey.size = key_size;
	      ret = db->get(db, &inKey, &inVal, 0);
#else
	      inKey.dptr = packed_key;
	      inKey.dsize = key_size;
	      inVal = dbm_fetch(db, inKey);
	      if (inVal.dptr) ret = RET_SUCCESS;
	      else ret = RET_SPECIAL;
#endif
	      switch (ret)
		{
		  case RET_SUCCESS:
		    if (!quiet)
		      {
			(void)strcpy(packed_data, s);
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
			unpack_data(packed_data, inVal.data, inVal.size);
#else
			unpack_data(packed_data, inVal.dptr, inVal.dsize);
#endif
			(void)printf("%s\n", packed_data);
		      }
		    break;
		  case RET_SPECIAL:
		    if (!quiet)
		      (void)fprintf(stderr, "No match\n");
		    break;
		  default:
		    break;
		}
	      if (ret < 0)
		perror("DB search:");
	    }
	  else
	    {
	      (void)fprintf(stderr, "Invalid key\n");
	      ret = RET_FAILURE;
	    }
	}
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
      (void)(db->close)(db);
#else
      dbm_close(db);
#endif
      if (ret) return EXIT_FAILURE;
      else return EXIT_SUCCESS;
    }
  else if (d) /* Delete record for specified key */
    {
      if (db_access(db_path, F_OK | R_OK | W_OK))
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}
#if defined(BERKELEYDB)
      db = dbopen(db_path, O_RDWR, 0, DB_BTREE, NULL);
#elif defined(FBSD_DATABASE)
      db = dbopen(db_path, O_RDWR, 0, DB_HASH, &openinfo);
#else
      db = dbm_open(db_path, O_RDWR, 0);
#endif
      if (!db)
	{
	  (void)fprintf(stderr, "Cannot open database %s\n", db_path);
	  return EXIT_FAILURE;
	}
      key_size = pack_key(d, packed_key);
      if (key_size > 0)
	{
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
	  inKey.data = packed_key;
	  inKey.size = key_size;
	  ret = db->del(db, &inKey, 0);
#else
	  inKey.dptr = packed_key;
	  inKey.dsize = key_size;
	  ret = dbm_delete(db, inKey);
#endif
	}
      else
	{
	  (void)fprintf(stderr, "Invalid key\n");
	  ret = RET_FAILURE;
	}
#if defined(BERKELEYDB) || defined(FBSD_DATABASE)
      (void)(db->close)(db);
#else
      dbm_close(db);
#endif
      if (ret) return EXIT_FAILURE;
      else return EXIT_SUCCESS;
    }
  else /* Put new records into the database */
    {
      if (!db_access(db_path, F_OK))
	{
	  if (db_access(db_path, R_OK | W_OK))
	    {
	      perror(db_path);
	      return EXIT_FAILURE;
	    }
	  else if (verbose)
	    (void)fprintf(stderr,
			  "Adding new data to lexical database %s\n",
			  db_path);
	}
      else if (verbose)
	(void)fprintf(stderr,
		      "Creating new lexical database %s\n", db_path);
      if (srcf)
	if (!freopen(srcf, "r", stdin))
	  {
	    (void)fprintf(stderr, "Cannot open %s for input\n", srcf);
	    return EXIT_FAILURE;
	  }
#if defined(BERKELEYDB)
      if (db_access(db_path, F_OK))
	{
	  db = dbopen(NULL, O_RDWR | O_CREAT,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
		      DB_BTREE, NULL);
	  if (!db)
	    {
	      (void)fprintf(stderr, "Cannot initialize temporary database\n");
	      return EXIT_FAILURE;
	    }
	  if (!quiet)
	    (void)fprintf(stderr, "Accumulating data...\n");
	}
      else
	{
	  db = dbopen(db_path, O_RDWR,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
		      DB_BTREE, NULL);
	  if (!db)
	    {
	      (void)fprintf(stderr,
			    "Cannot open database %s for output\n",
			    db_path);
	      return EXIT_FAILURE;
	    }
	}
#elif defined(FBSD_DATABASE)
      db = dbopen(db_path, O_RDWR | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
		  DB_HASH, &openinfo);
      if (!db)
	{
	  (void)fprintf(stderr, "Cannot open database %s for output\n",
			db_path);
	  return EXIT_FAILURE;
	}
#else
      db = dbm_open(db_path, O_RDWR | O_CREAT,
		    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (!db)
	{
	  (void)fprintf(stderr, "Cannot open database %s for output\n",
			db_path);
	  return EXIT_FAILURE;
	}
#endif
    }

  /* Collecting data */
  for (n = 0; fgets(line, 256, stdin); n++)
    {
      if (strlen(line) > MAX_RECORD_SIZE)
	{
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%i: warning: Record too long. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr, "Ignored too long record: %s\n", line);
	  invalid++;
	  continue;
	}
      s = strtok(line," ");
      if (strlen(s) > MAX_KEY_SIZE)
	{
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%i: warning: Too long key. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr, "Ignored too long key: %s\n", s);
	  invalid++;
	  continue;
	}
      key_size = pack_key(s, packed_key);
      if (key_size < 0)
	{
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%i: warning: Illegal symbols in key. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr,
			     "Ignored key containing illegal symbols: %s\n",
			     s);
	  invalid++;
	  continue;
	}
      data_size = pack_data(s, strtok(NULL,"\n"), packed_data);
      if (data_size < 0)
	{
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%i: warning: Invalid record. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr, "Ignored invalid record: %s\n", line);
	  invalid++;
	  continue;
	}
      if (!data_size)
	{
	  redundant++;
	  if (verbose)
	    {
	      if (srcf)
		(void)fprintf(stderr,
			      "%s:%i: warning: Redundant entry.",
			      srcf, n);
	      else (void)fprintf(stderr, "Redundant entry: %s\n", line);
	    }
	  if (!allrecs)
	    {
	      if (srcf && verbose)
		fprintf(stderr, " Ignored.\n");
	      continue;
	    }
	  else packed_data[data_size++] = 0;
	  if (srcf && verbose)
	    fprintf(stderr, "\n");
	}
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
      inKey.data = packed_key;
      inKey.size = key_size;
      inVal.data = packed_data;
      inVal.size = data_size;
      ret = (db->put)(db, &inKey, &inVal, R_NOOVERWRITE);
#else
      inKey.dptr = packed_key;
      inKey.dsize = key_size;
      inVal.dptr = packed_data;
      inVal.dsize = data_size;
      ret = dbm_store(db, inKey, inVal, DBM_INSERT);
#endif
      switch(ret)
	{
	  case RET_SUCCESS:
	    i++;
	    break;
	  case RET_SPECIAL:
	    if (replace_mode)
	      {
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
		ret = (db->put)(db, &inKey, &inVal, 0);
#else
		ret = dbm_store(db, inKey, inVal, DBM_REPLACE);
#endif
		if (ret)
		  {
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
		    (void)(db->close)(db);
#else
		    dbm_close(db);
#endif
		    if (srcf)
		      (void)fprintf(stderr,
				    "%s:%i: error: data storing error\n",
				    srcf, n);
		    else (void)fprintf(stderr, "Data storing error\n");
		    return EXIT_FAILURE;
		  }
		i++;
	      }
	    if (!quiet)
	      {
		if (srcf)
		  {
		    (void)fprintf(stderr,
				  "%s:%i: warning: Duplicate entry. ",
				  srcf, n);
		    if (replace_mode)
		      (void)fprintf(stderr, "Replaced.\n");
		    else (void)fprintf(stderr, "Ignored.\n");
		  }
		else (void)fprintf(stderr, "Duplicate entry: %s\n", line);
	      }
	    duplicate++;
	    break;
	  default:
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
	    (void)(db->close)(db);
#else
	    dbm_close(db);
#endif
	    if (srcf)
	      (void)fprintf(stderr,
			    "%s:%i: error: data storing error\n",
			    srcf, n);
	    else (void)fprintf(stderr, "Data storing error\n");
	    return EXIT_FAILURE;
	}
    }

#if defined(FBSD_DATABASE)
  (void)(db->close)(db);
#elif defined(BERKELEYDB)
  /* Dumping the database if needed */
  if (db_access(db_path, F_OK))
    {
      tmpdb = db;
      db = dbopen(db_path, O_RDWR | O_CREAT,
		  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH,
		  DB_BTREE, NULL);
      if (!db)
	{
	  (void)fprintf(stderr, "Cannot initialize database %s\n", db_path);
	  (void)(tmpdb->close)(tmpdb);
	  return EXIT_FAILURE;
	}
      if (!quiet)
	(void)fprintf(stderr, "Dumping the database...\n");
      while(!tmpdb->seq(tmpdb, &inKey, &inVal, R_NEXT))
	if (db->put(db, &inKey, &inVal, R_NOOVERWRITE))
	  {
	    (void)fprintf(stderr, "Database write error\n");
	    (void)(tmpdb->close)(tmpdb);
	    (void)(db->close)(db);
	    return EXIT_FAILURE;
	  }
      (void)(tmpdb->close)(tmpdb);
    }
  (void)(db->close)(db);
#else
  dbm_close(db);
#endif

  if (!quiet)
    {
      (void)fprintf(stderr, "Total: %d record", n);
      if (n != 1) (void)fputc('s', stderr);
      (void)fprintf(stderr, " processed.\n");
      if (invalid)
	(void)fprintf(stderr, "Invalid records: %d\n", invalid);
      if (duplicate)
	(void)fprintf(stderr, "Duplicates: %d\n", duplicate);
      if (redundant)
	(void)fprintf(stderr, "Redundant entries: %d\n", redundant);
      (void)fprintf(stderr, "%d record", i);
      if (i != 1) (void)fputc('s', stderr);
      (void)fprintf(stderr, " have been put into the database\n");
    }

  return EXIT_SUCCESS;
}
