/* This file is part of the rulexdb library.
 *
 * Copyright (C) 2006 Igor B. Poretsky <poretsky@mlbox.ru>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * Rulex database access routines.
 */


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef USE_BDB
#include <fcntl.h>
#else
#include <stdint.h>
#endif
#include "lexdb.h"
#include "coder.h"


/* Local constants */

/* Internal flag for the recursive call of rulexdb_search() */
#define RULEXDB_NOPREFIX 0x80

/* Data storage methods */
#ifdef USE_BDB
#define LEXICON_DB_TYPE DB_BTREE
#define RULES_DB_TYPE DB_RECNO
#else
#define LMDB_MAP_SIZE (64 * 1024 * 1024)
#define LMDB_MAX_DBS 6
#endif


/* Local data */

/* Datasets names */
static const char *lexicon_db_name = "Lexbases";
static const char *exceptions_db_name = "Exceptions";
static const char *rules_db_name = "General";
static const char *lexclasses_db_name = "Lexclasses";
static const char *prefixes_db_name = "Prefixes";
static const char *corrections_db_name = "Corrections";


/* Local routines */

#ifdef USE_BDB

static DB *db_open(DB_ENV *env, const char *name, int type, int mode)
     /*
      * Open DB for specified dataset.
      *
      * Parameters:
      * env - pointer to the database environment
      *       initialized by rulexdb_open();
      * name - the dataset name;
      * type - data storage type;
      * mode - data access mode (RULEXDB_SEARCH, RULEXDB_UPDATE or RULEXDB_CREATE).
      *
      * This routine returns pointer to initialized DB handler
      * when success or NULL when failure.
      */
{
  int rc;
  DB *db;

  if (db_create(&db, env, 0))
    return NULL;
  switch (type)
    {
      case DB_RECNO:
	rc = db->set_flags(db, DB_RENUMBER);
	break;
      case DB_BTREE:
	rc = db->set_flags(db, DB_REVSPLITOFF);
	break;
      default:
	rc = 0;
    }
  if (rc)
    {
      (void)db->close(db, 0);
      return NULL;
    }
  switch (mode)
    {
      case RULEXDB_SEARCH:
	rc = db->open(db, NULL, env->app_private, name, type, DB_RDONLY, 0);
	break;
      case RULEXDB_UPDATE:
	rc = db->open(db, NULL, env->app_private, name, type, 0, 0);
	break;
      case RULEXDB_CREATE:
	rc = db->open(db, NULL, env->app_private, name, type, DB_CREATE,
		      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	break;
      default:
	rc = EINVAL;
	break;
    }
  if (rc)
    {
      (void)db->close(db, 0);
      db = NULL;
    }
  return db;
}

static void db_close(DB *db)
     /*
      * Safely close the database.
      */
{
  DBC *dbc = db->app_private;

  if (dbc) /* Close cursor at first if it was opened */
    (void)dbc->c_close(dbc);
  (void)db->close(db, 0);
  return;
}

static int db_get(DB *db, const char *key, char *value)
     /*
      * Retrieve data from dictionary dataset.
      * This routine performs all the work concerning key and value coding.
      * The argument "value" must point to memory area where
      * the resulting string will be placed. This area must have
      * enough space. It will be a copy of key if search fails.
      *
      * This routine returns 0 in the case of success.
      * If specified key doesn't exist, then RULEXDB_SPECIAL is returned.
      * In other cases an appropriate error code will be returned.
      */
{
  int rc;
  char packed_key[RULEXDB_BUFSIZE];
  DBT inKey, inVal;

  (void)memset(&inKey, 0, sizeof(DBT));
  (void)memset(&inVal, 0, sizeof(DBT));
  inKey.size = pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inKey.data = packed_key;
  rc = db->get(db, NULL, &inKey, &inVal, 0);
  switch (rc)
    {
      case 0:
	unpack_data(value, inVal.data, inVal.size);
	return RULEXDB_SUCCESS;
      case DB_NOTFOUND:
	return RULEXDB_SPECIAL;
      default:
	break;
    }
  return RULEXDB_FAILURE;
}

static int db_nrecs(DB *db)
     /*
      * Count records in the database.
      * only for rules datasets.
      */
{
  int rc;
  DB_BTREE_STAT *sp;

  rc = db->stat(db, NULL, &sp, DB_FAST_STAT);
  if (rc) return 0;

  rc = sp->bt_nkeys;
  free(sp);
  return rc;
}

static char *rule_get(DB *db, int n)
     /*
      * Retrieve rule by number.
      * This routine returns pointer to the rule text representation
      * when success or NULL when failure.
      * This pointer is valid only until the next database operation.
      */
{
  int rc;
  DBT inKey, inVal;
  db_recno_t recno;

  (void)memset(&inKey, 0, sizeof(DBT));
  (void)memset(&inVal, 0, sizeof(DBT));
  recno = n;
  inKey.data = &recno;
  inKey.size = sizeof(db_recno_t);
  rc = db->get(db, NULL, &inKey, &inVal, 0);
  if (rc)
    return NULL;
  return inVal.data;
}

#else /* !USE_BDB: LMDB helpers */

static unsigned int lmdb_nrecs(MDB_txn *txn, MDB_dbi dbi)
{
  MDB_stat st;
  if (mdb_stat(txn, dbi, &st))
    return 0;
  return (unsigned int)st.ms_entries;
}

static int lmdb_get(MDB_txn *txn, MDB_dbi dbi, const char *key, char *value)
{
  int rc;
  char packed_key[RULEXDB_BUFSIZE];
  MDB_val inKey, inVal;

  inKey.mv_size = (size_t)pack_key(key, packed_key);
  if ((signed int)inKey.mv_size <= 0)
    return RULEXDB_EINVKEY;
  inKey.mv_data = packed_key;
  rc = mdb_get(txn, dbi, &inKey, &inVal);
  switch (rc)
    {
      case 0:
        unpack_data(value, inVal.mv_data, (int)inVal.mv_size);
        return RULEXDB_SUCCESS;
      case MDB_NOTFOUND:
        return RULEXDB_SPECIAL;
      default:
        break;
    }
  return RULEXDB_FAILURE;
}

static char *lmdb_rule_get(MDB_txn *txn, MDB_dbi dbi, char *rule_buf, int n)
{
  uint32_t recno = (uint32_t)n;
  MDB_val key, data;
  size_t len;

  key.mv_data = &recno;
  key.mv_size = sizeof(uint32_t);
  if (mdb_get(txn, dbi, &key, &data))
    return NULL;
  len = data.mv_size < (size_t)(RULEXDB_BUFSIZE - 1)
        ? data.mv_size : (size_t)(RULEXDB_BUFSIZE - 1);
  memcpy(rule_buf, data.mv_data, len);
  rule_buf[len] = '\0';
  return rule_buf;
}

#endif /* USE_BDB */

static int rules_init(RULEXDB *rulexdb, RULEX_RULESET *rules)
     /*
      * Initialize ruleset for subsequent fetching and loading
      * (not for updating).
      *
      * This routine at first checks if the ruleset is already initialized
      * and exits successfully if so. If the ruleset appears initialized
      * for updating or failed to initialize earlier,
      * RULEXDB_EACCESS error code will be returned.
      */
{
#ifdef USE_BDB
  (void)rulexdb;
  if (rules->nrules < 0) /* Cannot be initialized for loading */
    return RULEXDB_EACCESS;
  if (rules->db) /* Already initialized */
    return RULEXDB_SUCCESS;
  rules->db = db_open(rules->env, rules->db_name,
		      RULES_DB_TYPE, RULEXDB_SEARCH);
  if (!rules->db) /* DB open failed, so smudge the ruleset for future. */
    {
      rules->nrules = -1;
      return RULEXDB_FAILURE;
    }
  rules->nrules = db_nrecs(rules->db);
  if (rules->nrules > 0) /* Not empty */
    {
      /* Allocate memory for pointers */
      rules->pattern = calloc(rules->nrules, sizeof(regex_t *));
      if (!rules->pattern)
	{
	  db_close(rules->db);
	  rules->db = NULL;
	  rules->nrules = -1;
	  return RULEXDB_EMALLOC;
	}
      rules->replacement = calloc(rules->nrules, sizeof(char *));
      if (!rules->replacement)
	{
	  free(rules->pattern);
	  db_close(rules->db);
	  rules->db = NULL;
	  rules->nrules = -1;
	  return RULEXDB_EMALLOC;
	}
    }
  return RULEXDB_SUCCESS;
#else
  unsigned int flags;

  if (rules->nrules < 0)
    return RULEXDB_EACCESS;
  if (rules->dbi_open)
    return RULEXDB_SUCCESS;

  flags = MDB_INTEGERKEY;
  if (rulexdb->mode != RULEXDB_SEARCH)
    flags |= MDB_CREATE;

  if (mdb_dbi_open(rulexdb->txn, rules->db_name, flags, &rules->dbi))
    {
      rules->nrules = 0;
      return RULEXDB_SUCCESS;
    }
  rules->dbi_open = 1;
  rules->nrules = (int)lmdb_nrecs(rulexdb->txn, rules->dbi);

  if (rules->nrules > 0)
    {
      rules->pattern = calloc((size_t)rules->nrules, sizeof(regex_t *));
      if (!rules->pattern)
        {
          rules->nrules = -1;
          return RULEXDB_EMALLOC;
        }
      rules->replacement = calloc((size_t)rules->nrules, sizeof(char *));
      if (!rules->replacement)
        {
          free(rules->pattern);
          rules->pattern = NULL;
          rules->nrules = -1;
          return RULEXDB_EMALLOC;
        }
    }
  return RULEXDB_SUCCESS;
#endif
}

static int rule_load(RULEXDB *rulexdb, RULEX_RULESET *rules, int n)
     /*
      * Preload specified rule and turn it into internal representation.
      *
      * This routine at first checks if specified rule is already loaded
      * and exits successfully if so.
      *
      * The ruleset itself must be initialized before.
      */
{
  int rc;
  char *s, *rule_src;

  if (n >= rules->nrules) /* Specified rule number validation */
    return RULEXDB_EPARM;
  if (rules->pattern[n]) /* Already loaded */
    return RULEXDB_SUCCESS;

  /* Get rule source */
#ifdef USE_BDB
  (void)rulexdb;
  rule_src = rule_get(rules->db, n + 1);
#else
  rule_src = lmdb_rule_get(rulexdb->txn, rules->dbi, rules->rule_buf, n + 1);
#endif
  if (!rule_src)
    return RULEXDB_FAILURE;

  /* Allocate memory for compiled pattern */
  rules->pattern[n] = calloc(1, sizeof(regex_t));
  if (!rules->pattern[n])
    return RULEXDB_EMALLOC;

  /* Compile pattern */
#ifdef USE_BDB
  rc = regcomp(rules->pattern[n], strtok(rule_src, " "),
	       REG_EXTENDED | REG_ICASE);
#else
  rc = regcomp(rules->pattern[n], strtok(rules->rule_buf, " "),
               REG_EXTENDED | REG_ICASE);
#endif
  if (rc) /* Pattern compiling failure */
    {
      regfree(rules->pattern[n]);
      free(rules->pattern[n]);
      rules->pattern[n] = NULL;
      return RULEXDB_FAILURE;
    }

  /* Save replacement if needed */
  s = strtok(NULL, " ");
  if (s)
    rules->replacement[n] = strdup(s);

  return RULEXDB_SUCCESS;
}

static void rules_release(RULEX_RULESET *rules)
     /*
      * Release the ruleset and free all resources allocated
      * for its sake.
      */
{
  int i;

  for (i = 0; i < rules->nrules; i++)
    {
      if (rules->pattern[i])
	{
	  regfree(rules->pattern[i]);
	  free(rules->pattern[i]);
	  rules->pattern[i] = NULL;
	}
      if (rules->replacement[i])
	{
	  free(rules->replacement[i]);
	  rules->replacement[i] = NULL;
	}
    }
  free(rules->pattern);
  rules->pattern = NULL;
  free(rules->replacement);
  rules->replacement = NULL;
#ifdef USE_BDB
  if (rules->db)
    {
      db_close(rules->db);
      rules->db = NULL;
    }
#else
  rules->dbi_open = 0;
#endif
  rules->nrules = 0;
  return;
}

static int lexguess(RULEXDB *rulexdb, const char *s, char *t)
     /*
      * This routine tries to guess stressing for the word
      * pointed by s by general rules from the database. If success,
      * the result is placed into memory area pointed by t,
      * which must have enough space for it.
      *
      * Return value indicates whether the guessing succeeded or not.
      * If no rule has matched, then RULEXDB_SPECIAL is returned.
      */
{
  int i;
  regmatch_t match[2];

  i = rules_init(rulexdb, &rulexdb->rules);
  if (i) return i;

  for (i = 0; i < rulexdb->rules.nrules; i++)
    if (!rule_load(rulexdb, &rulexdb->rules, i))
      if (!regexec(rulexdb->rules.pattern[i], s, 2, match, 0))
	{
	  (void)strncpy(t, s, match[1].rm_eo);
	  t[match[1].rm_eo] = '+';
	  (void)strcpy(t + match[1].rm_eo + 1, s + match[1].rm_eo);
	  return RULEXDB_SUCCESS;
	}
  return RULEXDB_SPECIAL;
}

static int postcorrect(RULEXDB *rulexdb, char *s)
     /*
      * This routine performs some additional word corrections
      * according to the correction rules from the database if needed.
      */
{
  int i, k, l;
  char *r, *t, *orig;
  regmatch_t match[10];

  i = rules_init(rulexdb, &rulexdb->correctors);
  if (i) return i;

  for (i = 0; i < rulexdb->correctors.nrules; i++)
    if (!rule_load(rulexdb, &rulexdb->correctors, i))
      if (!regexec(rulexdb->correctors.pattern[i], s, 10, match, 0))
	{
	  t = s + match[0].rm_so;
	  orig = strdup(t);
	  if (!orig) return RULEXDB_EMALLOC;
	  for (r = rulexdb->correctors.replacement[i]; *r; r++)
	    if (((*r) >= '0') && ((*r) <= '9'))
	      {
		k = (*r) - '0';
		l = match[k].rm_eo - match[k].rm_so;
		if (l)
		  {
		    (void)strncpy(t, orig + match[k].rm_so - match[0].rm_so, l);
		    t += l;
		  }
	      }
	    else *t++ = *r;
	  l = strlen(orig) + match[0].rm_so - match[0].rm_eo;
	  if (l)
	    {
	      (void)strcpy(t, orig + match[0].rm_eo - match[0].rm_so);
	      t += l;
	    }
	  *t = 0;
	  free(orig);
	}
  return RULEXDB_SUCCESS;
}

#ifndef USE_BDB
/*
 * Internal helper: reference to one of the two dictionaries.
 * Holds pointers to the related fields in RULEXDB so that
 * choose_dictionary() can return a single value callers dereference uniformly.
 */
typedef struct
{
  MDB_dbi    *dbi;
  int        *dbi_open;
  MDB_cursor **cursor;
} DictHandle;
#endif

#ifdef USE_BDB
static DB **choose_dictionary(RULEXDB *rulexdb, const char *key, int item_type)
     /*
      * Choose the dictionary and open it if necessary.
      * If item_type specified as RULEXDB_DEFAULT, then choosing is based
      * on key: if key is recognized as a lexical base,
      * then the lexbases dictionary is chosen, otherwise exceptions
      * dictionary is used.
      *
      * Returns pointer to the DB handler when success
      * or NULL when failure.
      */
{
  const char *db_name;
  DB **db;

  if (!rulexdb) return NULL;
  switch (item_type)
    {
      case RULEXDB_EXCEPTION:
      case RULEXDB_EXCEPTION_RAW:
	db = &rulexdb->exceptions_db;
	db_name = exceptions_db_name;
	break;
      case RULEXDB_LEXBASE:
	db = &rulexdb->lexicon_db;
	db_name = lexicon_db_name;
	break;
      case RULEXDB_DEFAULT:
	if (key)
	  {
	    if (rulexdb_classify(rulexdb, key) == RULEXDB_SPECIAL)
	      {
		db = &rulexdb->lexicon_db;
		db_name = lexicon_db_name;
	      }
	    else
	      {
		db = &rulexdb->exceptions_db;
		db_name = exceptions_db_name;
	      }
	  }
	else return NULL;
	break;
      default:
	return NULL;
    }
  if (!(*db))
    *db = db_open(rulexdb->env, db_name, LEXICON_DB_TYPE, rulexdb->mode);
  return db;
}
#else
static DictHandle choose_dictionary(RULEXDB *rulexdb, const char *key, int item_type)
{
  DictHandle h = {NULL, NULL, NULL};
  const char *db_name;
  unsigned int flags;

  if (!rulexdb)
    return h;

  switch (item_type)
    {
      case RULEXDB_EXCEPTION:
      case RULEXDB_EXCEPTION_RAW:
        h.dbi      = &rulexdb->exceptions_dbi;
        h.dbi_open = &rulexdb->exceptions_dbi_open;
        h.cursor   = &rulexdb->exceptions_cursor;
        db_name    = exceptions_db_name;
        break;
      case RULEXDB_LEXBASE:
        h.dbi      = &rulexdb->lexicon_dbi;
        h.dbi_open = &rulexdb->lexicon_dbi_open;
        h.cursor   = &rulexdb->lexicon_cursor;
        db_name    = lexicon_db_name;
        break;
      case RULEXDB_DEFAULT:
        if (key)
          {
            if (rulexdb_classify(rulexdb, key) == RULEXDB_SPECIAL)
              {
                h.dbi      = &rulexdb->lexicon_dbi;
                h.dbi_open = &rulexdb->lexicon_dbi_open;
                h.cursor   = &rulexdb->lexicon_cursor;
                db_name    = lexicon_db_name;
              }
            else
              {
                h.dbi      = &rulexdb->exceptions_dbi;
                h.dbi_open = &rulexdb->exceptions_dbi_open;
                h.cursor   = &rulexdb->exceptions_cursor;
                db_name    = exceptions_db_name;
              }
          }
        else return h;
        break;
      default:
        return h;
    }

  if (!*(h.dbi_open))
    {
      flags = (rulexdb->mode != RULEXDB_SEARCH) ? MDB_CREATE : 0;
      if (!mdb_dbi_open(rulexdb->txn, db_name, flags, h.dbi))
        *(h.dbi_open) = 1;
    }
  return h;
}
#endif

static RULEX_RULESET *get_ruleset_handler(RULEXDB *rulexdb, int rule_type)
     /*
      * Get ruleset handler pointer for specified rule type.
      * Returns NULL for unknown type.
      */
{
  if (!rulexdb) return NULL;
  switch (rule_type)
    {
      case RULEXDB_RULE:
	return &rulexdb->rules;
      case RULEXDB_LEXCLASS:
	return &rulexdb->lexclasses;
      case RULEXDB_PREFIX:
	return &rulexdb->prefixes;
      case RULEXDB_CORRECTOR:
	return &rulexdb->correctors;
      default:
	break;
    }
  return NULL;
}

static RULEX_RULESET *choose_ruleset(RULEXDB *rulexdb, int rule_type)
     /*
      * Choose ruleset and open it if necessary.
      *
      * Returns opened ruleset handler pointer when success
      * or NULL otherwise.
      */
{
  RULEX_RULESET *rules = get_ruleset_handler(rulexdb, rule_type);

  if (!rules) return NULL;
#ifdef USE_BDB
  if (!rules->db)
    rules->db = db_open(rules->env, rules->db_name,
			RULES_DB_TYPE, rulexdb->mode);
  if (rules->db) rules->nrules = -1;
#else
  if (!rules->dbi_open)
    {
      unsigned int flags = MDB_INTEGERKEY;
      if (rulexdb->mode != RULEXDB_SEARCH)
        flags |= MDB_CREATE;
      if (!mdb_dbi_open(rulexdb->txn, rules->db_name, flags, &rules->dbi))
        {
          rules->dbi_open = 1;
          rules->nrules = -1;
        }
    }
#endif
  return rules;
}


/* Externally visible routines */

RULEXDB *rulexdb_open(const char *path, int mode)
     /*
      * Open lexical database.
      *
      * This routine does not actually open any dataset
      * (the datasets are to be opened later by demand),
      * but it allocates and initializes new RULEXDB structure
      * and opens the database environment. By the way
      * this routine checks accessibility of the database file.
      *
      * Arguments description:
      * path - path to the database file;
      * mode - Access mode: RULEXDB_SEARCH, RULEXDB_UPDATE or RULEXDB_CREATE.
      *
      * Returns pointer to the new RULEXDB structure when success
      * or NULL otherwise.
      */
{
  RULEXDB *rulexdb = calloc(1, sizeof(RULEXDB));

  if (!rulexdb)
    return NULL;

#ifdef USE_BDB

  /* Create database environment */
  if (db_env_create(&rulexdb->env, 0))
    {
      free(rulexdb);
      return NULL;
    }
  /* Open it */
  if (rulexdb->env->open(rulexdb->env, NULL,
		       DB_INIT_MPOOL | DB_INIT_LOCK | DB_PRIVATE | DB_CREATE,
		       0))
    {
      (void)rulexdb->env->close(rulexdb->env, 0);
      free(rulexdb);
      return NULL;
    }

  /* Initialize necessary RULEXDB fields */
  rulexdb->env->app_private = (char *)path;
  rulexdb->rules.env = rulexdb->env;
  rulexdb->rules.db_name = rules_db_name;
  rulexdb->lexclasses.env = rulexdb->env;
  rulexdb->lexclasses.db_name = lexclasses_db_name;
  rulexdb->prefixes.env = rulexdb->env;
  rulexdb->prefixes.db_name = prefixes_db_name;
  rulexdb->correctors.env = rulexdb->env;
  rulexdb->correctors.db_name = corrections_db_name;
  rulexdb->mode = mode;

  /* Check database file accessibility according to specified access mode */
  switch (mode)
    {
      case RULEXDB_SEARCH:
	if (access(path, F_OK | R_OK))
	  {
	    (void)rulexdb->env->close(rulexdb->env, 0);
	    free(rulexdb);
	    rulexdb = NULL;
	  }
	break;
      case RULEXDB_UPDATE:
	if (access(path, F_OK | R_OK | W_OK))
	  {
	    (void)rulexdb->env->close(rulexdb->env, 0);
	    free(rulexdb);
	    rulexdb = NULL;
	  }
	break;
      case RULEXDB_CREATE:
	if (!access(path, F_OK))
	  if (access(path, R_OK | W_OK))
	    {
	      (void)rulexdb->env->close(rulexdb->env, 0);
	      free(rulexdb);
	      rulexdb = NULL;
	    }
	break;
      default:
	(void)rulexdb->env->close(rulexdb->env, 0);
	free(rulexdb);
	rulexdb = NULL;
	break;
    }

#else /* LMDB */

  /* Check file accessibility according to specified access mode */
  switch (mode)
    {
      case RULEXDB_SEARCH:
	if (access(path, F_OK | R_OK))
	  {
	    free(rulexdb);
	    return NULL;
	  }
	break;
      case RULEXDB_UPDATE:
	if (access(path, F_OK | R_OK | W_OK))
	  {
	    free(rulexdb);
	    return NULL;
	  }
	break;
      case RULEXDB_CREATE:
	if (!access(path, F_OK))
	  if (access(path, R_OK | W_OK))
	    {
	      free(rulexdb);
	      return NULL;
	    }
	break;
      default:
	free(rulexdb);
	return NULL;
    }

  if (mdb_env_create(&rulexdb->env))
    {
      free(rulexdb);
      return NULL;
    }
  if (mdb_env_set_mapsize(rulexdb->env, LMDB_MAP_SIZE) ||
      mdb_env_set_maxdbs(rulexdb->env, LMDB_MAX_DBS))
    {
      mdb_env_close(rulexdb->env);
      free(rulexdb);
      return NULL;
    }

  {
    unsigned int env_flags = MDB_NOSUBDIR | MDB_NOLOCK;
    mdb_mode_t file_mode;
    unsigned int txn_flags;

    if (mode == RULEXDB_SEARCH)
      env_flags |= MDB_RDONLY;
    file_mode = (mode == RULEXDB_CREATE) ? 0644 : 0;
    if (mdb_env_open(rulexdb->env, path, env_flags, file_mode))
      {
	mdb_env_close(rulexdb->env);
	free(rulexdb);
	return NULL;
      }

    txn_flags = (mode == RULEXDB_SEARCH) ? MDB_RDONLY : 0;
    if (mdb_txn_begin(rulexdb->env, NULL, txn_flags, &rulexdb->txn))
      {
	mdb_env_close(rulexdb->env);
	free(rulexdb);
	return NULL;
      }
  }

  rulexdb->rules.db_name       = rules_db_name;
  rulexdb->lexclasses.db_name  = lexclasses_db_name;
  rulexdb->prefixes.db_name    = prefixes_db_name;
  rulexdb->correctors.db_name  = corrections_db_name;
  rulexdb->mode = mode;

#endif /* USE_BDB */

  return rulexdb;
}

void rulexdb_close(RULEXDB *rulexdb)
     /*
      * Close lexical database and free all resources
      * allocated for its sake.
      */
{
  rules_release(&rulexdb->rules);
  rules_release(&rulexdb->lexclasses);
  rules_release(&rulexdb->prefixes);
  rules_release(&rulexdb->correctors);
#ifdef USE_BDB
  if (rulexdb->lexicon_db)
    db_close(rulexdb->lexicon_db);
  if (rulexdb->exceptions_db)
    db_close(rulexdb->exceptions_db);
  (void)rulexdb->env->close(rulexdb->env, 0);
#else
  if (rulexdb->lexicon_cursor)
    mdb_cursor_close(rulexdb->lexicon_cursor);
  if (rulexdb->exceptions_cursor)
    mdb_cursor_close(rulexdb->exceptions_cursor);
  if (rulexdb->txn)
    {
      if (rulexdb->mode == RULEXDB_SEARCH)
	mdb_txn_abort(rulexdb->txn);
      else
	mdb_txn_commit(rulexdb->txn);
    }
  mdb_env_close(rulexdb->env);
#endif
  free(rulexdb);
  return;
}

int rulexdb_subscribe_rule(RULEXDB *rulexdb, const char *src,
			 int rule_type, unsigned int n)
     /*
      * Store new rule into the database.
      *
      * This routine adds new rule to the specified ruleset
      * in the lexical database.
      *
      * Arguments description:
      * rulexdb - pointer to the opened lexical database handler structure;
      * src - text representation of the rule;
      * rule_type - specifies the ruleset
      *             (RULEXDB_LEXCLASS, RULEXDB_RULE, RULEXDB_PREFIX or RULEXDB_CORRECTOR);
      * n - rule number. If 0, this rule is appended at the end of ruleset,
      *     otherwise the new rule will be inserted at the specified position.
      *
      * Returns 0 (RULEXDB_SUCCESS) when success or non-zero
      * error code otherwise.
      */
{
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;

#ifdef USE_BDB
  {
    int rc;
    DBT inKey, inVal;
    db_recno_t recno;

    if (!rules->db) return RULEXDB_EACCESS;
    if (n) /* Explicit rule number */
      {
	rc = db_nrecs(rules->db);
	if (n > (unsigned int)rc) /* Ruleset must be continuous */
	  return RULEXDB_EINVKEY;
      }
    (void)memset(&inKey, 0, sizeof(DBT));
    (void)memset(&inVal, 0, sizeof(DBT));
    if (n)
      {
	recno = n;
	inKey.data = &recno;
	inKey.size = sizeof(db_recno_t);
      }
    inVal.data = (char *)src;
    inVal.size = strlen(src) + 1;
    rc = rules->db->put(rules->db, NULL, &inKey, &inVal, n ? 0 : DB_APPEND);
    if (rc)
      return RULEXDB_FAILURE;
    return RULEXDB_SUCCESS;
  }
#else
  {
    int rc;
    unsigned int count;
    uint32_t recno;
    MDB_val key, data;
    char buf[RULEXDB_BUFSIZE];

    if (!rules->dbi_open) return RULEXDB_EACCESS;
    count = lmdb_nrecs(rulexdb->txn, rules->dbi);

    if (n == 0)
      {
	recno = count + 1;
	key.mv_data = &recno; key.mv_size = sizeof(uint32_t);
	data.mv_data = (char *)src; data.mv_size = strlen(src) + 1;
	rc = mdb_put(rulexdb->txn, rules->dbi, &key, &data, 0);
	return rc ? RULEXDB_FAILURE : RULEXDB_SUCCESS;
      }

    if (n > count) return RULEXDB_EINVKEY;

    /* Shift records n..count → n+1..count+1 (backward to avoid overwrite) */
    {
      uint32_t i;
      for (i = count; i >= n; i--)
        {
          uint32_t sk = i, dk = i + 1;
          MDB_val sv = {sizeof(uint32_t), &sk};
          size_t len;
          rc = mdb_get(rulexdb->txn, rules->dbi, &sv, &data);
          if (rc) return RULEXDB_FAILURE;
          len = data.mv_size < sizeof(buf) ? data.mv_size : sizeof(buf) - 1;
          memcpy(buf, data.mv_data, len); buf[len] = '\0';
          {
            MDB_val dv = {sizeof(uint32_t), &dk};
            MDB_val dd = {strlen(buf) + 1, buf};
            rc = mdb_put(rulexdb->txn, rules->dbi, &dv, &dd, 0);
            if (rc) return RULEXDB_FAILURE;
          }
          if (i == 0) break;
        }
    }
    recno = n;
    key.mv_data = &recno; key.mv_size = sizeof(uint32_t);
    data.mv_data = (char *)src; data.mv_size = strlen(src) + 1;
    rc = mdb_put(rulexdb->txn, rules->dbi, &key, &data, 0);
    return rc ? RULEXDB_FAILURE : RULEXDB_SUCCESS;
  }
#endif
}

char * rulexdb_fetch_rule(RULEXDB *rulexdb, int rule_type, int n)
     /*
      * Extract specified rule from lexical database.
      *
      * This routine retrieves rule in its text representation
      * and return pointer to it or NULL when failure. This pointer
      * remains valid only until next database operation.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * rule_type - specifies the ruleset
      *             (RULEXDB_LEXCLASS, RULEXDB_RULE, RULEXDB_PREFIX or RULEXDB_CORRECTOR);
      * n - rule number in the ruleset.
      */
{
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return NULL;
#ifdef USE_BDB
  if (!rules->db) return NULL;
  return rule_get(rules->db, n);
#else
  if (!rules->dbi_open) return NULL;
  return lmdb_rule_get(rulexdb->txn, rules->dbi, rules->rule_buf, n);
#endif
}

int rulexdb_remove_rule(RULEXDB *rulexdb, int rule_type, int n)
     /*
      * Remove specified rule from the database.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * rule_type - specifies the ruleset
      *             (RULEXDB_LEXCLASS, RULEXDB_RULE, RULEXDB_PREFIX or RULEXDB_CORRECTOR);
      * n - rule number in the ruleset.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * there is no rule with such number, or an appropriate
      * error code when failure.
      */
{
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;

#ifdef USE_BDB
  {
    int rc;
    DBT inKey;
    db_recno_t recno = n;

    if (!rules->db) return RULEXDB_EACCESS;
    (void)memset(&inKey, 0, sizeof(DBT));
    inKey.data = &recno;
    inKey.size = sizeof(db_recno_t);
    rc = rules->db->del(rules->db, NULL, &inKey, 0);
    if (rc)
      {
	if (rc == DB_NOTFOUND)
	  return RULEXDB_SPECIAL;
	return RULEXDB_FAILURE;
      }
    return RULEXDB_SUCCESS;
  }
#else
  {
    int rc;
    unsigned int count;
    char buf[RULEXDB_BUFSIZE];
    MDB_val data;

    if (!rules->dbi_open) return RULEXDB_EACCESS;
    count = lmdb_nrecs(rulexdb->txn, rules->dbi);
    if (n < 1 || (unsigned int)n > count) return RULEXDB_SPECIAL;

    /* Shift n+1..count → n..count-1 */
    {
      uint32_t i;
      for (i = (uint32_t)n + 1; i <= count; i++)
        {
          uint32_t sk = i, dk = i - 1;
          MDB_val sv = {sizeof(uint32_t), &sk};
          size_t len;
          rc = mdb_get(rulexdb->txn, rules->dbi, &sv, &data);
          if (rc) return RULEXDB_FAILURE;
          len = data.mv_size < sizeof(buf) ? data.mv_size : sizeof(buf) - 1;
          memcpy(buf, data.mv_data, len); buf[len] = '\0';
          {
            MDB_val dv = {sizeof(uint32_t), &dk};
            MDB_val dd = {strlen(buf) + 1, buf};
            rc = mdb_put(rulexdb->txn, rules->dbi, &dv, &dd, 0);
            if (rc) return RULEXDB_FAILURE;
          }
        }
    }
    {
      uint32_t last = count;
      MDB_val lk = {sizeof(uint32_t), &last};
      rc = mdb_del(rulexdb->txn, rules->dbi, &lk, NULL);
      if (rc == MDB_NOTFOUND) return RULEXDB_SPECIAL;
      if (rc) return RULEXDB_FAILURE;
    }
    return RULEXDB_SUCCESS;
  }
#endif
}

int rulexdb_subscribe_item(RULEXDB *rulexdb, const char *key, const char * value,
			 int item_type, int overwrite)
     /*
      * Put new item into the lexical database.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * key - the original word;
      * value - its pronunciation;
      * item_type - target dictionary specification
      *             (RULEXDB_LEXBASE, RULEXDB_EXCEPTION or RULEXDB_DEFAULT);
      * overwrite - if true (non-zero) the new item will replace
      *             already existing one with the same key if any.
      *             Otherwise the new item will not be stored.
      *
      * If item type is specified as RULEXDB_DEFAULT, then target dictionary
      * will be guessed according to specified key: if it represents
      * any lexical base, then lexbases dictionary will be chosen,
      * otherwise the exceptions dictionary will be used.
      *
      * If item type is specified as RULEXDB_DEFAULT and key word is
      * recognized as a lexical base, but the record for this key
      * already exist in the Lexbases dictionary, then Exceptions
      * dictionary will be tried instead.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * specified key already exists in the dictionary,
      * or an appropriate error code when failure.
      */
{
  int rc;
  char packed_key[RULEXDB_BUFSIZE], packed_data[RULEXDB_BUFSIZE];

#ifdef USE_BDB
  DBT inKey, inVal;
  DB **db = choose_dictionary(rulexdb, key, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  (void)memset(&inKey, 0, sizeof(DBT));
  (void)memset(&inVal, 0, sizeof(DBT));
  inKey.size = pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inVal.size = pack_data(key, value, packed_data);
  if ((signed int)(inVal.size) < 0)
    return RULEXDB_EINVREC;
  if (!inVal.size)
    packed_data[inVal.size++] = 0;
  inKey.data = packed_key;
  inVal.data = packed_data;
  rc = (*db)->put(*db, NULL, &inKey, &inVal, DB_NOOVERWRITE);
  if ((item_type == RULEXDB_DEFAULT) && (rc == DB_KEYEXIST)
      && (db == &rulexdb->lexicon_db))
    {
      db = choose_dictionary(rulexdb, NULL, RULEXDB_EXCEPTION);
      if (!db) return RULEXDB_EPARM;
      if (!(*db)) return RULEXDB_EACCESS;
      rc = (*db)->put(*db, NULL, &inKey, &inVal, DB_NOOVERWRITE);
    }
  switch (rc)
    {
      case 0:
	return RULEXDB_SUCCESS;
      case DB_KEYEXIST:
	if (overwrite)
	  {
	    rc = (*db)->put(*db, NULL, &inKey, &inVal, 0);
	    if (rc) break;
	    else return RULEXDB_SPECIAL;
	  }
	else return RULEXDB_SPECIAL;
      default:
	break;
    }
#else
  MDB_val inKey, inVal;
  DictHandle h = choose_dictionary(rulexdb, key, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_EACCESS;
  inKey.mv_size = (size_t)pack_key(key, packed_key);
  if ((signed int)inKey.mv_size <= 0)
    return RULEXDB_EINVKEY;
  inVal.mv_size = (size_t)pack_data(key, value, packed_data);
  if ((signed int)inVal.mv_size < 0)
    return RULEXDB_EINVREC;
  if (!inVal.mv_size)
    packed_data[inVal.mv_size++] = 0;
  inKey.mv_data = packed_key;
  inVal.mv_data = packed_data;
  rc = mdb_put(rulexdb->txn, *(h.dbi), &inKey, &inVal, MDB_NOOVERWRITE);
  if ((item_type == RULEXDB_DEFAULT) && (rc == MDB_KEYEXIST)
      && (h.dbi == &rulexdb->lexicon_dbi))
    {
      h = choose_dictionary(rulexdb, NULL, RULEXDB_EXCEPTION);
      if (!h.dbi) return RULEXDB_EPARM;
      if (!*(h.dbi_open)) return RULEXDB_EACCESS;
      rc = mdb_put(rulexdb->txn, *(h.dbi), &inKey, &inVal, MDB_NOOVERWRITE);
    }
  switch (rc)
    {
      case 0:
	return RULEXDB_SUCCESS;
      case MDB_KEYEXIST:
	if (overwrite)
	  {
	    rc = mdb_put(rulexdb->txn, *(h.dbi), &inKey, &inVal, 0);
	    if (rc) break;
	    else return RULEXDB_SPECIAL;
	  }
	else return RULEXDB_SPECIAL;
      default:
	break;
    }
#endif

  return RULEXDB_FAILURE;
}

int rulexdb_retrieve_item(RULEXDB *rulexdb, const char *key, char *value, int item_type)
     /*
      * Retrieve an item from the lexical database.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * key - the word to retrieve item for;
      * value - memory area for its pronunciation string;
      * item_type - target dictionary specification
      *             (RULEXDB_LEXBASE, RULEXDB_EXCEPTION or RULEXDB_DEFAULT);
      *
      * If item type is specified as RULEXDB_DEFAULT, then target dictionary
      * will be guessed according to specified key: if it represents
      * any lexical base, then lexbases dictionary will be chosen,
      * otherwise the exceptions dictionary will be used.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * specified key does not exist in the dictionary,
      * or an appropriate error code when failure.
      */
{
#ifdef USE_BDB
  DB **db = choose_dictionary(rulexdb, key, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_FAILURE;

  (void)strcpy(value, key);
  return db_get(*db, key, value);
#else
  DictHandle h = choose_dictionary(rulexdb, key, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_FAILURE;

  (void)strcpy(value, key);
  return lmdb_get(rulexdb->txn, *(h.dbi), key, value);
#endif
}

int rulexdb_lexbase(RULEXDB *rulexdb, const char *s, char *t, int n)
     /*
      * Try to find lexical base for the specified word.
      *
      * This routine scans lexclasses ruleset beginning from n
      * trying to match the word pointed by s. When match succeeds,
      * the lexical base is constructed in memory area pointed by t,
      * which must have enough space for it, and the number of matched rule
      * is returned. If no match has occurred 0 is returned.
      * In the case of error an appropriate error code is returned.
      */
{
  int i, rc;
  regmatch_t match[2];

  if ((n < 1) || (!rulexdb) || (!s) || (!t)) return RULEXDB_EPARM;
  rc = rules_init(rulexdb, &rulexdb->lexclasses);
  if (rc) return rc;

  for (i = n - 1; i < rulexdb->lexclasses.nrules; i++)
    if ((rc = rule_load(rulexdb, &rulexdb->lexclasses, i)))
      break;
    else if (!regexec(rulexdb->lexclasses.pattern[i], s, 2, match, 0))
      {
	(void)strncpy(t, s, match[1].rm_eo);
	t[match[1].rm_eo] = 0;
	if (rulexdb->lexclasses.replacement[i])
	  (void)strcat(t, rulexdb->lexclasses.replacement[i]);
	rc = i + 1;
	break;
      }
  return rc;
}

int rulexdb_search(RULEXDB *rulexdb, const char * key, char *value, int flags)
     /*
      * Search lexical database for specified word.
      *
      * This routine searches lexical database and tries to guess
      * pronunciation of specified word according to the acquired info.
      * The resulting string is placed into the buffer pointed
      * by value. This buffer must have enough space for it.
      * When no useful info is found, the original word (key)
      * is copied to the value buffer and RULEXDB_SPECIAL code
      * is returned.
      *
      * Searching is performed in the following order:
      * Specified word is searched in the exceptions dictionary.
      * If found, the result is returned and procedure
      * exits successfully. Otherwise the word is treated
      * as an implicit form and program tries to guess its base
      * and find it in the lexbases dictionary. If this process
      * succeeds, the pronunciation string is constructed
      * according to the acquired data and procedure exits successfully.
      * At last, the word is matched against general rules.
      * If no match succeeds, then program exits with RULEXDB_SPECIAL code,
      * returning original word as a result.
      *
      * If this process appears to be not fully unsuccessful and some
      * information was found in the database, then the resulting string
      * is matched against correction rules and the first matched one
      * is applied if any.
      *
      * When no information is found, the word is matched against
      * prefix rules and the process is repeated for the word stem
      * with the matched prefix stripped off.
      *
      * The last argument specifies which steps of the described
      * process are to be performed. It consists of following flags
      * which may be joined by "or" operation:
      * RULEXDB_EXCEPTIONS - search the word in the exceptions dictionary.
      * RULEXDB_FORMS - try to treat specified word as an implicit form.
      * RULEXDB_RULES - try to apply general rules.
      * Zero value (no flags) means that full search (all stages)
      * should be performed.
      *
      * RULEXDB_NOPREFIX - internal flag used in the recursive call
      * for the words with prefix stripped.
      */
{
  int i, j, rc = RULEXDB_SPECIAL;
  char *s;

  (void)strcpy(value, key);

  /* The first stage: looking up in the exceptions dictionary */
  if ((!flags) || (flags & RULEXDB_EXCEPTIONS))
    {
#ifdef USE_BDB
      DB **db = choose_dictionary(rulexdb, NULL, RULEXDB_EXCEPTION);
      if (!db) return RULEXDB_EPARM;
      if (*db)
	{
	  rc = db_get(*db, key, value);
	  if (rc < 0) return rc;
	}
#else
      DictHandle h = choose_dictionary(rulexdb, NULL, RULEXDB_EXCEPTION);
      if (!h.dbi) return RULEXDB_EPARM;
      if (*(h.dbi_open))
	{
	  rc = lmdb_get(rulexdb->txn, *(h.dbi), key, value);
	  if (rc < 0) return rc;
	}
#endif
    }

  /* The second stage: treating the word as an implicit form */
  if ((rc == RULEXDB_SPECIAL) && ((!flags) || (flags & RULEXDB_FORMS)))
    {
#ifdef USE_BDB
      DB **db = choose_dictionary(rulexdb, NULL, RULEXDB_LEXBASE);
      if (!db) return RULEXDB_EPARM;
      if (*db)
	{
	  s = malloc(strlen(key) + 32);
	  if (s)
	    for (i = 1; rc == RULEXDB_SPECIAL; i++)
	      {
		i = rulexdb_lexbase(rulexdb, key, s, i);
		if (!i) break;
		if (i < 0)
		  {
		    free(s);
		    return i;
		  }
		if (strlen(key) < strlen(s))
		  {
		    for (j = strlen(key); j < strlen(s); j++)
		      value[j] ='_';
		    value[strlen(s)] = 0;
		  }
		else value[strlen(key)] = 0;
		rc = db_get(*db, s, value);
		if (rc < 0)
		  {
		    free(s);
		    return rc;
		  }
	      }
	  else return RULEXDB_EMALLOC;
	  free(s);
	}
#else
      DictHandle h = choose_dictionary(rulexdb, NULL, RULEXDB_LEXBASE);
      if (!h.dbi) return RULEXDB_EPARM;
      if (*(h.dbi_open))
	{
	  s = malloc(strlen(key) + 32);
	  if (s)
	    for (i = 1; rc == RULEXDB_SPECIAL; i++)
	      {
		i = rulexdb_lexbase(rulexdb, key, s, i);
		if (!i) break;
		if (i < 0)
		  {
		    free(s);
		    return i;
		  }
		if (strlen(key) < strlen(s))
		  {
		    for (j = strlen(key); j < strlen(s); j++)
		      value[j] ='_';
		    value[strlen(s)] = 0;
		  }
		else value[strlen(key)] = 0;
		rc = lmdb_get(rulexdb->txn, *(h.dbi), s, value);
		if (rc < 0)
		  {
		    free(s);
		    return rc;
		  }
	      }
	  else return RULEXDB_EMALLOC;
	  free(s);
	}
#endif

      /* Prefix detection stage */
      if ((rc == RULEXDB_SPECIAL) &&
          !rules_init(rulexdb, &rulexdb->prefixes))
        {
          s = malloc(strlen(key));
          if (s)
            {
              regmatch_t match;
              for (i = 0; (rc == RULEXDB_SPECIAL) && (i < rulexdb->prefixes.nrules); i++)
                if ((!rule_load(rulexdb, &rulexdb->prefixes, i)) &&
                    (!regexec(rulexdb->prefixes.pattern[i], key, 1, &match, 0)) &&
                    (!match.rm_so) &&
                    (match.rm_eo < strlen(key)))
                  {
                    if (rulexdb->prefixes.replacement[i])
                      (void)strcpy(s, rulexdb->prefixes.replacement[i]);
                    else *s = 0;
                    j = strlen(s);
                    (void)strcat(s, key + match.rm_eo);
                    rc = rulexdb_search(rulexdb, s, value + match.rm_eo - j, RULEXDB_FORMS | RULEXDB_NOPREFIX);
                    if (rc == RULEXDB_EINVKEY)
                      rc = RULEXDB_SPECIAL;
                    (void)strncpy(value, key, match.rm_eo);
                  }
              free(s);
            }
          else rc = RULEXDB_EMALLOC;
        }
    }

  /* The last resort: trying to use a general rule */
  if (rc == RULEXDB_SPECIAL)
    {
      value[strlen(key)] = 0;
      if ((!flags) || (flags & RULEXDB_RULES))
	rc = lexguess(rulexdb, key, value);
    }

  /* Applying a post-correction if needed */
  if (!rc && !(flags & RULEXDB_NOPREFIX))
    rc = postcorrect(rulexdb, value);

  return rc;
}

int rulexdb_seq(RULEXDB *rulexdb, char *key, char *value, int item_type, int mode)
     /*
      * Sequential retrieving dictionary items.
      *
      * This routine sequentially fetches lexical records one by one.
      * its operations are not disturbed by the key-based search.
      *
      * Arguments key and value must point to the buffers with enough space
      * for retrieved data. Argument item_type specifies the dictionary.
      * The dictionary must be specified explicitly: RULEXDB_EXCEPTION
      * or RULEXDB_LEXBASE (RULEXDB_DEFAULT is not allowed in this case).
      * When fetching data from the exception dictionary the correction
      * will be applied. To prevent this feature you can specify
      * RULEXDB_EXCEPTION_RAW instead of RULEXDB_EXCEPTION.
      * The last argument specifies direction of the dictionary scanning.
      * Allowed values are: RULEXDB_SEQ_FIRST, RULEXDB_SEQ_NEXT,
      * RULEXDB_SEQ_PREV or RULEXDB_SEQ_LAST.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * no more records can be fetched, or an appropriate error code
      * in the case of absolute failure.
      */
{
  int rc;

#ifdef USE_BDB
  DBT inKey, inVal;
  DBC *dbc;
  DB **db = choose_dictionary(rulexdb, NULL, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_FAILURE;
  dbc = (*db)->app_private;
  /* Initialize cursor if it is not done already */
  if (!dbc)
    {
      rc = (*db)->cursor(*db, NULL, &dbc, 0);
      if (rc)
	{
	  dbc = NULL;
	  return RULEXDB_FAILURE;
	}
      else (*db)->app_private = dbc;
    }
  (void)memset(&inKey, 0, sizeof(DBT));
  (void)memset(&inVal, 0, sizeof(DBT));
  rc = dbc->c_get(dbc, &inKey, &inVal, mode);
  switch (rc)
    {
      case 0:
	if (unpack_key(inKey.data, inKey.size, key, RULEXDB_MAX_KEY_SIZE))
	  return RULEXDB_FAILURE;
	(void)strcpy(value, key);
	unpack_data(value, inVal.data, inVal.size);
	if (item_type == RULEXDB_EXCEPTION)
	  return postcorrect(rulexdb, value);
	return RULEXDB_SUCCESS;
      case DB_NOTFOUND:
	return RULEXDB_SPECIAL;
      default:
	break;
    }
#else
  MDB_val inKey, inVal;
  DictHandle h = choose_dictionary(rulexdb, NULL, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_FAILURE;
  if (!*(h.cursor))
    {
      rc = mdb_cursor_open(rulexdb->txn, *(h.dbi), h.cursor);
      if (rc)
	{
	  *(h.cursor) = NULL;
	  return RULEXDB_FAILURE;
	}
    }
  rc = mdb_cursor_get(*(h.cursor), &inKey, &inVal, (MDB_cursor_op)mode);
  switch (rc)
    {
      case 0:
	if (unpack_key(inKey.mv_data, (unsigned int)inKey.mv_size,
		       key, RULEXDB_MAX_KEY_SIZE))
	  return RULEXDB_FAILURE;
	(void)strcpy(value, key);
	unpack_data(value, inVal.mv_data, (int)inVal.mv_size);
	if (item_type == RULEXDB_EXCEPTION)
	  return postcorrect(rulexdb, value);
	return RULEXDB_SUCCESS;
      case MDB_NOTFOUND:
	return RULEXDB_SPECIAL;
      default:
	break;
    }
#endif
  return RULEXDB_FAILURE;
}

int rulexdb_remove_item(RULEXDB *rulexdb, const char *key, int item_type)
     /*
      * Remove specified record from the dictionary.
      *
      This routine removes lexical item for specified key.
      * Item type must be specified explicitly:
      * only RULEXDB_LEXBASE or RULEXDB_EXCEPTION are allowed.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL
      * when no record exists for specified key, or an appropriate
      * error code when other failure.
      */
{
  int rc;
  char packed_key[RULEXDB_BUFSIZE];

#ifdef USE_BDB
  DBT inKey;
  DB **db = choose_dictionary(rulexdb, key, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  (void)memset(&inKey, 0, sizeof(DBT));
  inKey.size = pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inKey.data = packed_key;
  rc = (*db)->del(*db, NULL, &inKey, 0);
  if (rc)
    {
      if (rc == DB_NOTFOUND)
	return RULEXDB_SPECIAL;
      return RULEXDB_FAILURE;
    }
#else
  MDB_val inKey;
  DictHandle h = choose_dictionary(rulexdb, key, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_EACCESS;
  inKey.mv_size = (size_t)pack_key(key, packed_key);
  if ((signed int)inKey.mv_size <= 0)
    return RULEXDB_EINVKEY;
  inKey.mv_data = packed_key;
  rc = mdb_del(rulexdb->txn, *(h.dbi), &inKey, NULL);
  if (rc)
    {
      if (rc == MDB_NOTFOUND)
	return RULEXDB_SPECIAL;
      return RULEXDB_FAILURE;
    }
#endif
  return RULEXDB_SUCCESS;
}

int rulexdb_remove_this_item(RULEXDB *rulexdb, int item_type)
     /*
      * Remove sequentially retrieved item.
      *
      * This routine removes from the dictionary the item retrieved
      * by the rulexdb_seq() routine, that is the item
      * pointed by cursor.
      *
      * The item type must be specified explicitly:
      * only RULEXDB_LEXBASE or RULEXDB_EXCEPTION are allowed.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL
      * when pointed record is already deleted, or an appropriate
      * error code when other failure. If specified dictionary
      * never was accessed sequentially, so it has no initialized
      * cursor, the code RULEXDB_EACCESS is returned.
      */
{
  int rc;

#ifdef USE_BDB
  DBC *dbc;
  DB **db = choose_dictionary(rulexdb, NULL, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  dbc = (*db)->app_private;
  if(!dbc) return RULEXDB_EACCESS;
  rc = dbc->c_del(dbc, 0);
  if (rc)
    {
      if (rc == DB_NOTFOUND)
	return RULEXDB_SPECIAL;
      return RULEXDB_FAILURE;
    }
#else
  DictHandle h = choose_dictionary(rulexdb, NULL, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_EACCESS;
  if (!*(h.cursor)) return RULEXDB_EACCESS;
  rc = mdb_cursor_del(*(h.cursor), 0);
  if (rc)
    {
      if (rc == MDB_NOTFOUND)
	return RULEXDB_SPECIAL;
      return RULEXDB_FAILURE;
    }
#endif
  return RULEXDB_SUCCESS;
}

int rulexdb_classify(RULEXDB *rulexdb, const char *s)
     /*
      * Test specified word whether it represents a lexical base.
      *
      * Returns 0 (RULEXDB_SUCCESS) when the word does not represent
      * a lexical base, RULEXDB_SPECIAL if it does, or an appropriate
      * error code when failure.
      */
{
  int i;
  char *t = malloc(strlen(s) + 32);

  if (!t)
    return RULEXDB_EMALLOC;
  for (i = 1; i > 0; i++)
    {
      i = rulexdb_lexbase(rulexdb, s, t, i);
      if (i < 0)
	{
	  free(t);
	  return i;
	}
      if (i)
	{
	  if (!strcmp(s, t))
	    {
	      free(t);
	      return RULEXDB_SPECIAL;
	    }
	}
      else i--;
    }
  free(t);
  return RULEXDB_SUCCESS;
}

int rulexdb_discard_dictionary(RULEXDB *rulexdb, int item_type)
     /*
      * Discard the dictionary.
      *
      * This routine deletes all data from specified dictionary.
      * Returns number of deleted records or negative error code.
      * Item type specifies a dictionary
      * (RULEXDB_EXCEPTION or RULEXDB_LEXBASE).
      * RULEXDB_DEFAULT is not allowed here.
      */
{
  int rc;

#ifdef USE_BDB
  u_int32_t n;
  DB **db = choose_dictionary(rulexdb, NULL, item_type);
  DBC *dbc;

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  dbc = (*db)->app_private;
  if (dbc) /* Close cursor at first if it was opened */
    {
      (void)dbc->c_close(dbc);
      (*db)->app_private = NULL;
    }
  rc = (*db)->truncate(*db, NULL, &n, 0);
  if (rc)
    return RULEXDB_FAILURE;
  return n;
#else
  unsigned int n;
  MDB_stat st;
  DictHandle h = choose_dictionary(rulexdb, NULL, item_type);

  if (!h.dbi) return RULEXDB_EPARM;
  if (!*(h.dbi_open)) return RULEXDB_EACCESS;
  if (*(h.cursor))
    {
      mdb_cursor_close(*(h.cursor));
      *(h.cursor) = NULL;
    }
  rc = mdb_stat(rulexdb->txn, *(h.dbi), &st);
  n = rc ? 0 : (unsigned int)st.ms_entries;
  rc = mdb_drop(rulexdb->txn, *(h.dbi), 0);
  if (rc)
    return RULEXDB_FAILURE;
  return (int)n;
#endif
}

int rulexdb_load_ruleset(RULEXDB *rulexdb, int rule_type)
     /*
      * Load ruleset data.
      *
      * This routine initializes specified ruleset
      * and preloads all its data.
      * Returns number of loaded records or negative error code.
      * Rule type specifies target ruleset
      * (RULEXDB_RULE, RULEXDB_LEXCLASS, RULEXDB_PREFIX or RULEXDB_CORRECTOR).
      *
      * Loaded ruleset cannot be modified.
      */
{
  int i, rc;
  RULEX_RULESET *rules = get_ruleset_handler(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;
  rc = rules_init(rulexdb, rules);
  if (!rc)
    for (i = 0; (i < rules->nrules) && !rc; i++)
      rc = rule_load(rulexdb, rules, i);

  return rc ? rc : rules->nrules;
}

int rulexdb_discard_ruleset(RULEXDB *rulexdb, int rule_type)
     /*
      * Discard the ruleset.
      *
      * This routine deletes all data from specified ruleset.
      * Returns number of deleted records or negative error code.
      * Rule type specifies target ruleset
      * (RULEXDB_RULE, RULEXDB_LEXCLASS, RULEXDB_PREFIX or RULEXDB_CORRECTOR).
      */
{
  int rc;
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;

#ifdef USE_BDB
  {
    u_int32_t n;
    if (!rules->db) return RULEXDB_EACCESS;
    rc = rules->db->truncate(rules->db, NULL, &n, 0);
    if (rc)
      return RULEXDB_FAILURE;
    return n;
  }
#else
  {
    unsigned int n;
    MDB_stat st;
    if (!rules->dbi_open) return RULEXDB_EACCESS;
    rc = mdb_stat(rulexdb->txn, rules->dbi, &st);
    n = rc ? 0 : (unsigned int)st.ms_entries;
    rc = mdb_drop(rulexdb->txn, rules->dbi, 0);
    if (rc)
      return RULEXDB_FAILURE;
    return (int)n;
  }
#endif
}

const char *rulexdb_dataset_name(int item_type)
     /*
      * Get pointer to the dataset name string.
      *
      * Item type here may specify a dictionary (RULEXDB_EXCEPTION
      * or RULEXDB_LEXBASE) as well as a ruleset (RULEXDB_RULES, RULEXDB_LEXCLASS,
      * RULEXDB_PREFIX or RULEXDB_CORRECTOR). The dictionary or ruleset
      * must be specified explicitly (RULEXDB_DEFAULT is not allowed here).
      *
      * For unknown item type returns NULL;
      */
{
  switch (item_type)
    {
      case RULEXDB_EXCEPTION:
      case RULEXDB_EXCEPTION_RAW:
	return exceptions_db_name;
      case RULEXDB_LEXBASE:
	return lexicon_db_name;
      case RULEXDB_LEXCLASS:
	return lexclasses_db_name;
      case RULEXDB_RULE:
	return rules_db_name;
      case RULEXDB_CORRECTOR:
	return corrections_db_name;
      default:
	break;
    }
  return NULL;
}
