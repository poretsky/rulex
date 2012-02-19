/* This file is part of the rulexdb library.
 *
 * Rulex database access routines.
 */

/*
 * Copyright (C) 2006 Igor B. Poretsky <poretsky@mlbox.ru>
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


#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "lexdb.h"
#include "coder.h"


/* Local constants */

/* Data storage methods */
#define LEXICON_DB_TYPE DB_BTREE
#define RULES_DB_TYPE DB_RECNO


/* Local data */

/* Datasets names */
static const char *lexicon_db_name = "Lexbases";
static const char *exceptions_db_name = "Exceptions";
static const char *rules_db_name = "General";
static const char *lexclasses_db_name = "Lexclasses";
static const char *corrections_db_name = "Corrections";

/* Error message prefixes */
static const char *db_reading = "Reading";
static const char *db_writing = "Writing";


/* Local routines */

static void *xcalloc(size_t nmemb, size_t size)
     /*
      * Allocate memory for nmemb data elements of given size
      * and initialize it by zero.
      *
      * Abort program with error message if failed.
      */
{
  void *ptr = calloc(nmemb, size);
  if (!ptr)
    {
      (void)fprintf(stderr, "Memory allocation error\n");
      exit(EXIT_FAILURE);
    }
  return ptr;
}

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
      if (rc != ENOENT)
	/*
	 * We don't want error message when the dataset doesn't exist.
	 * We prefer that search silently fails with an appropriate
	 * error code in this case. It is not crytical situation,
	 * so we pass it to the caller.
	 */
	db->err(db, rc, "Opening database %s:%s", env->app_private, name);
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

static void db_error(DB *db, int errcode, const char *msg)
     /*
      * Generate and emit a database error message.
      */
{
  const char *db_path, *db_name;

  if (db->get_dbname(db, &db_path, &db_name))
    db->err(db, errcode, "%s database", msg);
  else db->err(db, errcode, "%s database %s:%s", msg, db_path, db_name);
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
  inKey.size = rulexdb_pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inKey.data = packed_key;
  rc = db->get(db, NULL, &inKey, &inVal, 0);
  switch (rc)
    {
      case 0:
	rulexdb_unpack_data(value, inVal.data, inVal.size);
	return RULEXDB_SUCCESS;
      case DB_NOTFOUND:
	return RULEXDB_SPECIAL;
      default:
	break;
    }
  db_error(db, rc, db_reading);
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
  if (rc)
    {
      db_error(db, rc, db_reading);
      return 0;
    }
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
  switch (rc)
    {
      case 0:
	return inVal.data;
      case DB_KEYEMPTY:
      case DB_NOTFOUND:
	break;
      default:
	db_error(db, rc, db_reading);
	break;
    }
  return NULL;
}

static int rules_init(RULEX_RULESET *rules)
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
      rules->pattern = xcalloc(rules->nrules, sizeof(regex_t *));
      rules->replacement = xcalloc(rules->nrules, sizeof(char *));
    }
  return RULEXDB_SUCCESS;
}

static int rule_load(RULEX_RULESET *rules, int n)
     /*
      * Preload specified rule and turn it into internal representation.
      *
      * This routine at first checks if specified rule is already loaded
      * and exits successfully if so.
      *
      * The ruleset itself must be initialized before.
      */
{
  int rc, emsgsize;
  char *s, *rule_src;

  if (n >= rules->nrules) /* Specified rule number validation */
    return RULEXDB_FAILURE;
  if (rules->pattern[n]) /* Already loaded */
    return RULEXDB_SUCCESS;

  /* Get rule source */
  rule_src = rule_get(rules->db, n + 1);
  if (!rule_src)
    return RULEXDB_FAILURE;

  /* Allocate memory for compiled pattern */
  rules->pattern[n] = xcalloc(1, sizeof(regex_t));

  /* Compile pattern */
  rc = regcomp(rules->pattern[n], strtok(rule_src, " "),
	       REG_EXTENDED | REG_ICASE);
  if (rc) /* Pattern compiling failure */
    {
      emsgsize = regerror(rc, rules->pattern[n], NULL, 0);
      s = xcalloc(emsgsize, sizeof(char));
      (void)regerror(rc, rules->pattern[n], s, emsgsize);
      (void)fprintf(stderr, "Rule %i from %s: %s\n", n + 1, rules->db_name, s);
      free(s);
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
      * for it's sake.
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
  if (rules->db)
    {
      db_close(rules->db);
      rules->db = NULL;
    }
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

  if (rules_init(&rulexdb->rules))
    return RULEXDB_FAILURE;
  for (i = 0; i < rulexdb->rules.nrules; i++)
    if (!rule_load(&rulexdb->rules, i))
      if (!regexec(rulexdb->rules.pattern[i], s, 2, match, 0))
	{
	  (void)strncpy(t, s, match[1].rm_eo);
	  t[match[1].rm_eo] = '+';
	  (void)strcpy(t + match[1].rm_eo + 1, s + match[1].rm_eo);
	  return RULEXDB_SUCCESS;
	}
  return RULEXDB_SPECIAL;
}

static void postcorrect(RULEXDB *rulexdb, char *s)
     /*
      * This routine performs some additional word corrections
      * according to the correction rules from the database if needed.
      */
{
  int i, k, l;
  char *r, *t, *orig;
  regmatch_t match[10];

  if (rules_init(&rulexdb->correctors))
    return;
  for (i = 0; i < rulexdb->correctors.nrules; i++)
    if (!rule_load(&rulexdb->correctors, i))
      if (!regexec(rulexdb->correctors.pattern[i], s, 10, match, 0))
	{
	  t = s + match[0].rm_so;
	  orig = strdup(t);
	  if (!orig) break;
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
  return;
}

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

static RULEX_RULESET *choose_ruleset(RULEXDB *rulexdb, int rule_type)
     /*
      * Choose ruleset and open it if necessary.
      *
      * Returns opened ruleset handler pointer when success
      * or NULL otherwise.
      */
{
  RULEX_RULESET *rules;

  if (!rulexdb) return NULL;
  switch (rule_type)
    {
      case RULEXDB_RULE:
	rules = &rulexdb->rules;
	break;
      case RULEXDB_LEXCLASS:
	rules = &rulexdb->lexclasses;
	break;
      case RULEXDB_CORRECTOR:
	rules = &rulexdb->correctors;
	break;
      default:
	return NULL;
    }
  if (!rules->db)
    rules->db = db_open(rules->env, rules->db_name,
			RULES_DB_TYPE, rulexdb->mode);
  if (rules->db) rules->nrules = -1;
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
  RULEXDB *rulexdb = xcalloc(1, sizeof(RULEXDB));

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

  return rulexdb;
}

void rulexdb_close(RULEXDB *rulexdb)
     /*
      * Close lexical database and free all resources
      * allocated for it's sake.
      */
{
  rules_release(&rulexdb->rules);
  rules_release(&rulexdb->lexclasses);
  rules_release(&rulexdb->correctors);
  if (rulexdb->lexicon_db)
    db_close(rulexdb->lexicon_db);
  if (rulexdb->exceptions_db)
    db_close(rulexdb->exceptions_db);
  (void)rulexdb->env->close(rulexdb->env, 0);
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
      * rule_type - rule type (RULEXDB_LEXCLASS, RULEXDB_RULE or RULEXDB_CORRECTOR);
      * n - rule number. If 0, this rule is appended at the end of ruleset,
      *     otherwise the new rule will be inserted at the specified position.
      *
      * Returns 0 (RULEXDB_SUCCESS) when success or non-zero
      * error code otherwise.
      */
{
  int rc;
  DBT inKey, inVal;
  db_recno_t recno;
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;
  if (!rules->db) return RULEXDB_EACCESS;
  if (n) /* Explicit rule number */
    {
      rc = db_nrecs(rules->db);
      if (n > rc) /* Ruleset must be continuous */
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
    {
      db_error(rules->db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
  return RULEXDB_SUCCESS;
}

char * rulexdb_fetch_rule(RULEXDB *rulexdb, int rule_type, int n)
     /*
      * Extract specified rule from lexical database.
      *
      * This routine retrieves rule in it's text representation
      * and return pointer to it or NULL when failure. This pointer
      * remains valid only until next database operation.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * rule_type - Specifies the ruleset
      *             (RULEXDB_LEXCLASS, RULEXDB_RULE or RULEXDB_CORRECTOR);
      * n - rule number in the ruleset.
      */
{
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return NULL;
  if (!rules->db) return NULL;
  return rule_get(rules->db, n);
}

int rulexdb_remove_rule(RULEXDB *rulexdb, int rule_type, int n)
     /*
      * Remove specified rule from the database.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * rule_type - specifies the ruleset
      *             (RULEXDB_LEXCLASS, RULEXDB_RULE or RULEXDB_CORRECTOR);
      * n - rule number in the ruleset.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * there is no rule with such number, or an appropriate
      * error code when failure.
      */
{
  int rc;
  DBT inKey;
  db_recno_t recno = n;
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;
  if (!rules->db) return RULEXDB_EACCESS;
  (void)memset(&inKey, 0, sizeof(DBT));
  inKey.data = &recno;
  inKey.size = sizeof(db_recno_t);
  rc = rules->db->del(rules->db, NULL, &inKey, 0);
  if (rc)
    {
      if (rc == DB_NOTFOUND)
	return RULEXDB_SPECIAL;
      db_error(rules->db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
  return RULEXDB_SUCCESS;
}

int rulexdb_subscribe_item(RULEXDB *rulexdb, const char *key, const char * value,
			 int item_type, int overwrite)
     /*
      * Put new item into the lexical database.
      *
      * Arguments description:
      * rulexdb - points to the opened lexical database handler structure;
      * key - the original word;
      * value - it's pronunciation;
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
  DBT inKey, inVal;
  DB **db = choose_dictionary(rulexdb, key, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  (void)memset(&inKey, 0, sizeof(DBT));
  (void)memset(&inVal, 0, sizeof(DBT));
  inKey.size = rulexdb_pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inVal.size = rulexdb_pack_data(key, value, packed_data);
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
  db_error(*db, rc, db_writing);
  return RULEXDB_FAILURE;
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
  int i, rc = 0;
  regmatch_t match[2];

  if (n < 1) return RULEXDB_EPARM;
  if (rules_init(&rulexdb->lexclasses))
    return RULEXDB_EACCESS;
  for (i = n - 1; i < rulexdb->lexclasses.nrules; i++)
    if ((rc = rule_load(&rulexdb->lexclasses, i)))
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
      * If it succeeds, the result is returned and procedure
      * exits successfully. Otherwise the word is treated
      * as an implicit form and program tries to guess it's base
      * and find it in the lexbases dictionary. If this process
      * succeeds, the pronunciation string is constructed
      * according to the acquired data and procedure exits successfully.
      * At last, the word is matched against general rules.
      * If no match succeeds, then program exits with RULEXDB_SPECIAL code,
      * returning original word as a result.
      *
      * If this process appears not fully unsuccessful and some information
      * was found in the database, then the resulting string
      * is matched against correction rules and the first matched one
      * is applied if any.
      *
      * The last argument specified which steps of the described
      * process are to be performed. It consists of following flags
      * which can be joined by "or" operation:
      * RULEXDB_EXCEPTIONS - search the word in the exceptions dictionary.
      * RULEXDB_FORMS - try to treat specified word as an implicit form.
      * RULEXDB_RULES - try to apply general rules.
      * Zero value (no flags) means that full search (all stages)
      * should be performed.
      */
{
  int i, j, rc = RULEXDB_SPECIAL;
  char *s;
  DB **db;

  (void)strcpy(value, key);

  /* The first stage: looking up in the exceptions dictionary */
  if ((!flags) || (flags & RULEXDB_EXCEPTIONS))
    {
      db = choose_dictionary(rulexdb, NULL, RULEXDB_EXCEPTION);
      if (!db) return RULEXDB_EPARM;
      if (*db)
	rc = db_get(*db, key, value);
    }

  /* The second stage: treating the word as an implicit form */
  if ((rc == RULEXDB_SPECIAL) && ((!flags) || (flags & RULEXDB_FORMS)))
    {
      db = choose_dictionary(rulexdb, NULL, RULEXDB_LEXBASE);
      if (!db) return RULEXDB_EPARM;
      if (*db)
	{
	  s = malloc(strlen(key) + 32);
	  if (s)
	    for (i = 1; rc == RULEXDB_SPECIAL; i++)
	      {
		i = rulexdb_lexbase(rulexdb, key, s, i);
		if (!i) break;
		if (i < 0) return i;
		if (strlen(key) < strlen(s))
		  {
		    for (j = strlen(key); j < strlen(s); j++)
		      value[j] ='_';
		    value[strlen(s)] = 0;
		  }
		else value[strlen(key)] = 0;
		rc = db_get(*db, s, value);
	      }
	  else return RULEXDB_EMALLOC;
	  free(s);
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
  if (!rc)
    postcorrect(rulexdb, value);

  return rc;
}

int rulexdb_seq(RULEXDB *rulexdb, char *key, char *value, int item_type, int mode)
     /*
      * Sequential retrieving dictionary items.
      *
      * This routine sequentially fetches lexical records one by one.
      * It's operations are not disturbed by the key-based search.
      *
      * Arguments key and value must point to the buffers with enough space
      * for retrieved data. Argument item_type specifies the dictionary.
      * The dictionary must be specified explicitly: RULEXDB_EXCEPTION
      * or RULEXDB_LEXBASE (RULEXDB_DEFAULT is not allowed in this case).
      * When fetching data from the exception dictionary the correction
      * will be applied. To prevent this feature you can specify
      * RULEXDB_EXCEPTION_RAW instead of RULEXDB_EXCEPTION.
      * The last argument specifies direction of the dictionary scanning.
      * Allowed values are: DB_FIRST, DB_NEXT, DB_PREV or DB_LAST
      * as defined for the underlying database library.
      *
      * Returns 0 (RULEXDB_SUCCESS) on success, RULEXDB_SPECIAL when
      * no more records can be fetched, or an appropriate error code
      * in the case of absolute failure.
      */
{
  int rc;
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
	  db_error(*db, rc, db_reading);
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
	if (rulexdb_unpack_key(inKey.data, inKey.size, key, RULEXDB_MAX_KEY_SIZE))
	  return RULEXDB_FAILURE;
	(void)strcpy(value, key);
	rulexdb_unpack_data(value, inVal.data, inVal.size);
	if (item_type == RULEXDB_EXCEPTION)
	  postcorrect(rulexdb, value);
	return RULEXDB_SUCCESS;
      case DB_NOTFOUND:
	return RULEXDB_SPECIAL;
      default:
	break;
    }
  db_error(*db, rc, db_reading);
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
  DBT inKey;
  DB **db = choose_dictionary(rulexdb, key, item_type);

  if (!db) return RULEXDB_EPARM;
  if (!(*db)) return RULEXDB_EACCESS;
  (void)memset(&inKey, 0, sizeof(DBT));
  inKey.size = rulexdb_pack_key(key, packed_key);
  if ((signed int)(inKey.size) <= 0)
    return RULEXDB_EINVKEY;
  inKey.data = packed_key;
  rc = (*db)->del(*db, NULL, &inKey, 0);
  if (rc)
    {
      if (rc == DB_NOTFOUND)
	return RULEXDB_SPECIAL;
      db_error(*db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
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
      db_error(*db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
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
    {
      db_error(*db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
  return n;
}

int rulexdb_discard_ruleset(RULEXDB *rulexdb, int rule_type)
     /*
      * Discard the ruleset.
      *
      * This routine deletes all data from specified ruleset.
      * Returns number of deleted records or negative error code.
      * Rule type specifies target ruleset
      * (RULEXDB_RULE, RULEXDB_LEXCLASS or RULEXDB_CORRECTOR).
      */
{
  int rc;
  u_int32_t n;
  RULEX_RULESET *rules = choose_ruleset(rulexdb, rule_type);

  if (!rules) return RULEXDB_EPARM;
  if (!rules->db) return RULEXDB_EACCESS;
  rc = rules->db->truncate(rules->db, NULL, &n, 0);
  if (rc)
    {
      db_error(rules->db, rc, db_writing);
      return RULEXDB_FAILURE;
    }
  return n;
}

const char *rulexdb_dataset_name(int item_type)
     /*
      * Get pointer to the dataset name string.
      *
      * Item type here may specify a dictionary (RULEXDB_EXCEPTION
      * or RULEXDB_LEXBASE) as well as a ruleset (RULEXDB_RULES, RULEXDB_LEXCLASS
      * or RULEXDB_CORRECTOR). The dictionary or ruleset must be specified
      * explicitly (RULEXDB_DEFAULT is not allowed here).
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
