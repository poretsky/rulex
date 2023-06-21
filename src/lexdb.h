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
 * Interface to the rulex database.
 */


#ifndef RULEXDB_H
#define RULEXDB_H

#ifdef _WIN32
#include <pcre2posix.h>
#else
#include <regex.h>
#endif
#include <db.h>

/* BEGIN_C_DECLS should be used at the beginning of C declarations,
   so that C++ compilers don't mangle their names.  Use END_C_DECLS at
   the end of C declarations. */
#undef BEGIN_C_DECLS
#undef END_C_DECLS
#ifdef __cplusplus
# define BEGIN_C_DECLS extern "C" {
# define END_C_DECLS }
#else
# define BEGIN_C_DECLS /* empty */
# define END_C_DECLS /* empty */
#endif

BEGIN_C_DECLS


/* Constants */

/* Data size limits */
#define RULEXDB_MAX_KEY_SIZE 50
#define RULEXDB_MAX_RECORD_SIZE 200
#define RULEXDB_BUFSIZE 256

/* Database access modes */
#define RULEXDB_SEARCH 0
#define RULEXDB_UPDATE 1
#define RULEXDB_CREATE 2

/* Return codes */
#define RULEXDB_SUCCESS 0
#define RULEXDB_SPECIAL 1
#define RULEXDB_FAILURE -1
#define RULEXDB_EMALLOC -2
#define RULEXDB_EINVKEY -3
#define RULEXDB_EINVREC -4
#define RULEXDB_EPARM -5
#define RULEXDB_EACCESS -6

/* Search flags */
#define RULEXDB_EXCEPTIONS 1
#define RULEXDB_FORMS 2
#define RULEXDB_RULES 4

/* Data sets */
#define RULEXDB_DEFAULT 0
#define RULEXDB_EXCEPTION 1
#define RULEXDB_LEXBASE 2
#define RULEXDB_LEXCLASS 3
#define RULEXDB_RULE 4
#define RULEXDB_CORRECTOR 5
#define RULEXDB_EXCEPTION_RAW 6
#define RULEXDB_PREFIX 7


/* Data structures */

typedef struct /* Ruleset handler */
{
  DB *db; /* Associated database (dataset) */
  DB_ENV *env; /* Pointer to the database environment */
  const char *db_name; /* Dataset name */
  regex_t **pattern; /* Array of compiled patterns */
  char **replacement; /* Array of replacement strings */
  int nrules; /* Number of rules in the ruleset */
} RULEX_RULESET;

typedef struct /* Lexical database handler */
{
  RULEX_RULESET rules; /* General rules */
  RULEX_RULESET lexclasses; /* Lexical class defining rules */
  RULEX_RULESET prefixes; /* Word prefixes */
  RULEX_RULESET correctors; /* Correction rules */
  DB *lexicon_db; /* Dictionary of lexical bases */
  DB *exceptions_db; /* Dictionary of exceptions */
  DB_ENV *env; /* Pointer to the database environment */
  int mode; /* Access mode */
} RULEXDB;


/* Database access routines */

extern RULEXDB *rulexdb_open(const char *path, int mode);
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

extern void rulexdb_close(RULEXDB *rulexdb);
/*
 * Close lexical database and free all resources
 * allocated for it's sake.
 */

extern int rulexdb_subscribe_rule(RULEXDB *rulexdb, const char *src,
				int rule_type, unsigned int n);
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

extern char * rulexdb_fetch_rule(RULEXDB *rulexdb, int rule_type, int n);
/*
 * Extract specified rule from lexical database.
 *
 * This routine retrieves rule in it's text representation
 * and return pointer to it or NULL when failure. This pointer
 * remains valid only until next database operation.
 *
 * Arguments description:
 * rulexdb - points to the opened lexical database handler structure;
 * rule_type - specifies the ruleset
 *             (RULEXDB_LEXCLASS, RULEXDB_RULE, RULEXDB_PREFIX or RULEXDB_CORRECTOR);
 * n - rule number in the ruleset.
 */

extern int rulexdb_remove_rule(RULEXDB *rulexdb, int rule_type, int n);
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

extern int rulexdb_subscribe_item(RULEXDB *rulexdb, const char *key,
				const char * value,
				int item_type, int overwrite);
/*
 * Put new dictionary item into the lexical database.
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

extern int rulexdb_lexbase(RULEXDB *rulexdb, const char *s, char *t, int n);
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

extern int rulexdb_search(RULEXDB *rulexdb, const char * key, char *value, int flags);
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
 * as an implicit form and program tries to guess it's base
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
 */

extern int rulexdb_seq(RULEXDB *rulexdb, char *key, char *value,
		     int item_type, int mode);
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

extern int rulexdb_remove_item(RULEXDB *rulexdb, const char *key, int item_type);
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

extern int rulexdb_remove_this_item(RULEXDB *rulexdb, int item_type);
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

extern int rulexdb_classify(RULEXDB *rulexdb, const char *s);
/*
 * Test specified word whether it represents a lexical base.
 *
 * Returns 0 (RULEXDB_SUCCESS) when the word does not represent
 * a lexical base, RULEXDB_SPECIAL if it does, or an appropriate
 * error code when failure.
 */

extern int rulexdb_discard_dictionary(RULEXDB *rulexdb, int item_type);
/*
 * Discard the dictionary.
 *
 * This routine deletes all data from specified dictionary.
 * Returns number of deleted records or negative error code.
 * Item type specifies a dictionary
 * (RULEXDB_EXCEPTION or RULEXDB_LEXBASE).
 * RULEXDB_DEFAULT is not allowed here.
 */

extern int rulexdb_load_ruleset(RULEXDB *rulexdb, int rule_type);
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

extern int rulexdb_discard_ruleset(RULEXDB *rulexdb, int rule_type);
/*
 * Discard the ruleset.
 *
 * This routine deletes all data from specified ruleset.
 * Returns number of deleted records or negative error code.
 * Rule type specifies target ruleset
 * (RULEXDB_RULE, RULEXDB_LEXCLASS, RULEXDB_PREFIX or RULEXDB_CORRECTOR).
 */

extern const char *rulexdb_dataset_name(int item_type);
/*
 * Get pointer to the dataset name string.
 *
 * Item type here may specify a dictionary (RULEXDB_EXCEPTION
 * or RULEXDB_LEXBASE) as well as a ruleset (RULEXDB_RULES, RULEXDB_LEXCLASS,
 * RULEXDB_PREFIX or RULEXDB_CORRECTOR). The dictionary or ruleset must be
 * specified explicitly (RULEXDB_DEFAULT is not allowed here).
 *
 * For unknown item type returns NULL;
 */

END_C_DECLS

#endif
