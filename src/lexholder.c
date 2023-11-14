/* Rulex database holding utility. */

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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "lexdb.h"

/* Command line error codes */
#define CS_CONFLICT 1
#define NO_DB_FILE 2


#ifdef _WIN32
static const char *charset = "Russian_Russia.20866";
#else
static const char *charset = "ru_RU.koi8r";
#endif

static const char *usage =
"Lexical database holding utility.\n\n"

"This utility is designed for constructing, managing, testing and querying\n"
"lexical database providing pronunciation info for Russian words.\n\n"

"Usage:\t%s [options] <db_path>\n\n"

"When filling and updating the database, new records are read\n"
"from the standard input. When extracting data from the database\n"
"or testing operation, the result is printed to the standard\n"
"output. This behaviour can be changed by the \"-f\" switch.\n\n"

"All command line options described below are arranged\n"
"into several groups by its functionality.\n\n"

"The first group consists of options specifying an action to be done.\n"
"These options are mutually exclusive. We can do only one action\n"
"per invocation. If no action is specified, the program reads\n"
"its standard input (or a file specified by \"-f\" option)\n"
"and stores its content in the database. Here are the other actions:\n\n"

"-h -- Print this help (the only option not requiring the database path)\n"
"-l -- List database content\n"
"-t <dictionary_file> -- Test the database against specified dictionary\n"
"-c -- Clean the database (get rid of redundant records)\n"
"-s <key> -- Search specified key\n"
"-b <key> -- Retrieve basic forms (if any) for specified word\n"
"-d <key> -- Delete record for specified key\n"
"-D -- Discard the dataset\n\n"

"The next group of options is responsible for choosing the dataset.\n"
"These options are mutually exclusive and affect deletion, insertion\n"
"and listing operations. For listing and deletion the dataset must be\n"
"specified explicitly. If no one of these options is mentioned when\n"
"inserting new data, an appropriate dataset will be chosen according\n"
"to the input data. Only lexical data can be inserted in such a manner.\n"
"For rules target dataset must be specified explicitly.\n\n"

"-M -- Implicit Dictionary\n"
"-X -- Explicit dictionary\n"
"-G -- General rules\n"
"-L -- Lexical classification rules\n"
"-P -- Prefix detection rules\n"
"-C -- Correction rules\n\n"

"The next group contains options devoted to search mode specification.\n"
" These options affect search and test operation. By default (no options)\n"
"full search will be performed, otherwise only those stages specified\n"
"explicitly will be included in the search process.\n\n"

"-x -- Search in the explicit dictionary\n"
"-m -- Try to treat the word as an implicit form\n"
"-g -- Try to apply general rules\n\n"

"The last group contains several options affecting program behaviour\n"
"in general.\n\n"

"-f <file> -- Use specified file instead of standard input or output\n"
"-r -- Replace mode\n"
"-q -- Be more quiet than usual (don't print search results as well\n"
"      as warnings about duplicate records)\n"
"-v -- Be more verbose than usual (print final statistical information)\n\n";


int main(int argc, char *argv[])
{
  RULEXDB *db;

  char key[RULEXDB_BUFSIZE], value[RULEXDB_BUFSIZE];
  char *s = NULL, *d = NULL, *t = NULL, line[256];
  char *db_path = NULL, *srcf = NULL;

  int ret, n, k, i = 0;
  int invalid = 0, duplicate = 0;
  int verbose = 0, quiet = 0, rules_data = 0;
  int replace_mode = 0, dataset = RULEXDB_DEFAULT, search_mode = 0;

  if (!setlocale(LC_CTYPE, charset))
    {
      fprintf(stderr, "Cannot set %s locale.\n", charset);
      fprintf(stderr, "Probably you have to generate it by command:\n");
      fprintf(stderr, "localedef -f KOI8-R -i ru_RU ru_RU.KOI8-R\n");
      return EXIT_FAILURE;
    }

  /* Parse command line */
  if(argc==1)
    {
      (void)fprintf(stderr, usage, argv[0]);
      return EXIT_FAILURE;
    }
  ret = NO_DB_FILE;
  while((n = getopt(argc,argv,"f:b:s:d:t:CDGLMPXgmxclrqvh")) != -1)
    switch(n)
      {
	case 'X':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  dataset = RULEXDB_EXCEPTION;
	  break;
	case 'x':
	  search_mode |= RULEXDB_EXCEPTIONS;
	  break;
	case 'M':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  dataset = RULEXDB_LEXBASE;
	  break;
	case 'm':
	  search_mode |= RULEXDB_FORMS;
	  break;
	case 'G':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  rules_data = 1;
	  dataset = RULEXDB_RULE;
	  break;
	case 'g':
	  search_mode |= RULEXDB_RULES;
	  break;
	case 'L':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  rules_data = 1;
	  dataset = RULEXDB_LEXCLASS;
	  break;
	case 'P':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  rules_data = 1;
	  dataset = RULEXDB_PREFIX;
	  break;
	case 'C':
	  if (dataset != RULEXDB_DEFAULT)
	    ret = CS_CONFLICT;
	  rules_data = 1;
	  dataset = RULEXDB_CORRECTOR;
	  break;
	case 'f':
	  if (strcmp(optarg, "-"))
	    srcf = optarg;
	  break;
	case 'd':
	  if (d || s || t) ret = CS_CONFLICT;
	  else d = optarg;
	  break;
	case 'b':
	  if (d || s || t) ret = CS_CONFLICT;
	  s = key;
	  t = optarg;
	  break;
	case 's':
	  if (d || s || t) ret = CS_CONFLICT;
	  else s = optarg;
	  break;
	case 'l':
	  if (d || s || t) ret = CS_CONFLICT;
	  else s = line;
	  break;
	case 't':
	  if (d || s || t) ret = CS_CONFLICT;
	  else t = optarg;
	  break;
	case 'c':
	  if (d || s || t) ret = CS_CONFLICT;
	  else d = line;
	  break;
	case 'D':
	  if (d || s || t) ret = CS_CONFLICT;
	  else d = key;
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
	  (void)fprintf(stderr, usage, argv[0]);
	  return EXIT_SUCCESS;
	default:
	  (void)fprintf(stderr, usage, argv[0]);
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
		      "Ambiguous options in command line\n");
	return EXIT_FAILURE;
      case NO_DB_FILE:
	(void)fprintf(stderr, "DB file must be specified\n");
	return EXIT_FAILURE;
      default:
	break;
    }

  if (s || t) /* Database reading actions */
    {
      if (srcf)
	if (!freopen(srcf, "w", stdout))
	  {
	    (void)fprintf(stderr, "Cannot open %s for output\n", srcf);
	    return EXIT_FAILURE;
	  }
      db = rulexdb_open(db_path, RULEXDB_SEARCH);
      if (!db)
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}

      if (s == line) /* List full dataset content */
	{
	  if (dataset == RULEXDB_DEFAULT)
	    {
	      (void)fprintf(stderr, "Dataset must be specified explicitly\n");
	      rulexdb_close(db);
	      return EXIT_FAILURE;
	    }
	  n = 0;
	  ret = 0;
	  if (rules_data)
	    {
	      if (verbose)
		(void)fprintf(stderr, "Listing %s ruleset\n",
			      rulexdb_dataset_name(dataset));
	      for (n = 0; (s = rulexdb_fetch_rule(db, dataset, n + 1)); n++)
		(void)printf("%s\n", s);
	    }
	  else
	    {
	      if (verbose)
		(void)fprintf(stderr, "Listing %s dictionary\n",
			      rulexdb_dataset_name(dataset));
              if (RULEXDB_EXCEPTION == dataset)
                dataset = RULEXDB_EXCEPTION_RAW;
	      for (ret = rulexdb_seq(db, key, value, dataset, DB_FIRST);
		   ret == RULEXDB_SUCCESS;
		   ret = rulexdb_seq(db, key, value, dataset, DB_NEXT))
		{
		  (void)printf("%s %s\n", key, value);
		  n++;
		}
	    }
	  if (ret == RULEXDB_SPECIAL)
	    ret = 0;
	  if (ret)
	    (void)fprintf(stderr, "Database corruption\n");
	  if (!quiet)
	    {
	      (void)fprintf(stderr, "%d record", n);
	      if (n == 1)
		(void)fputs(" has", stderr);
	      else (void)fputs("s have", stderr);
	      (void)fputs(" been extracted from the database\n", stderr);
	    }
	}

      else if (s == key) /* Discover basic forms */
	{
	  ret = RULEXDB_SPECIAL;
	  for (k = 1; k > 0; k++)
	    {
	      k = rulexdb_lexbase(db, t, key, k);
	      if (k > 0)
		{
		  ret = RULEXDB_SUCCESS;
		  if (!quiet)
		    (void)printf("%d\t%s\n", k, key);
		}
	      else if (!k) k--;
	      else ret = k;
	    }
	}

      else if (s) /* Search record for specified key */
	{
	  if (rules_data)
	    {
	      k = atoi(s);
	      if (k)
		{
		  d = rulexdb_fetch_rule(db, dataset, k);
		  if (d)
		    {
		      (void)strcpy(value, d);
		      ret = RULEXDB_SUCCESS;
		    }
		  else ret = RULEXDB_SPECIAL;
		}
	      else ret = RULEXDB_EINVKEY;
	    }
	  else
	    {
	      for (k = 0; s[k]; k++)
		if (isupper(s[k]))
		  s[k] = tolower(s[k]);
	      ret = rulexdb_search(db, s, value, search_mode);
	    }
	  switch (ret)
	    {
	      case RULEXDB_SPECIAL:
		if (rules_data) quiet = 1;
	      case RULEXDB_SUCCESS:
		if (!quiet)
		  (void)printf("%s\n", value);
		break;
	      case RULEXDB_EINVKEY:
		(void)fprintf(stderr, "Invalid key: %s\n", s);
		break;
	      default:
		(void)fprintf(stderr, "DB search error\n");
		break;
	    }
	}

      else /* Test database */
	{
	  if (strcmp(t, "-"))
	    if (!freopen(t, "r", stdin))
	      {
		(void)fprintf(stderr, "Cannot open %s for input\n", t);
		return EXIT_FAILURE;
	      }
	  if (verbose)
	    (void)fputs("Testing the database\n", stderr);
	  for (n = 1, i = 0; fgets(line, 256, stdin); n++)
	    {
	      if (strlen(line) > RULEXDB_MAX_RECORD_SIZE)
		{
		  if (t)
		    (void)fprintf(stderr,
				  "%s:%i: warning: Record too long. Ignored.\n",
				  t, n);
		  else (void)fprintf(stderr,
				     "Ignored too long record: %s\n",
				     line);
		  invalid++;
		  continue;
		}
	      for (k = 0; line[k]; k++)
		if (isupper(line[k]))
		  line[k] = tolower(line[k]);
	      s = strtok(line, " ");
	      if (strlen(s) > RULEXDB_MAX_KEY_SIZE)
		{
		  if (t)
		    (void)fprintf(stderr,
				  "%s:%i: warning: Too long key. Ignored.\n",
				  t, n);
		  else (void)fprintf(stderr, "Ignored too long key: %s\n", s);
		  invalid++;
		  continue;
		}
	      (void)strcpy(key, s);
	      s = strtok(NULL, " \n");
	      ret = rulexdb_search(db, key, value, search_mode);
	      switch (ret)
		{
		  case RULEXDB_SUCCESS:
		  case RULEXDB_SPECIAL:
		    if (strcmp(s, value))
		      {
			(void)printf("%s %s\n", key, s);
			i++;
                        if (t && verbose)
                          (void)fprintf(stderr,
                                        "%s:%i: warning: Mismatch found.\n",
                                        t, n);
		      }
		    break;
		  case RULEXDB_EINVKEY:
		    if (t)
		      (void)fprintf(stderr,
				    "%s:%i: warning: Illegal symbols in key. Ignored.\n",
				    t, n);
		    else (void)fprintf(stderr,
				       "Ignored key containing illegal symbols: %s\n",
				       key);
		    invalid++;
		    break;
		  case RULEXDB_EINVREC:
		    if (t)
		      (void)fprintf(stderr,
				    "%s:%i: warning: Invalid record. Ignored.\n",
				    t, n);
		    else (void)fprintf(stderr,
				       "Ignored invalid record: %s %s\n",
				       key, s);
		    invalid++;
		    break;
		  default:
		    if (t)
		      (void)fprintf(stderr,
				    "%s:%i: error: data retrieving error\n",
				    t, n);
		    else (void)fprintf(stderr, "Data retrieving error\n");
		    invalid++;
		    break;
		}
	    }
	  if (!quiet)
	    {
              n -= invalid;
	      (void)fprintf(stderr, "%i word", --n);
	      if (n == 1)
		(void)fputs(" has", stderr);
	      else (void)fputs("s have", stderr);
	      (void)fputs(" been checked\n", stderr);
	      if (invalid)
		{
		  (void)fprintf(stderr, "%d invalid record", invalid);
		  if (invalid == 1) (void)fputs(" was", stderr);
		  else (void)fputs("s were", stderr);
		  (void)fputs(" ignored\n", stderr);
		}
	      if (i)
		{
		  (void)fprintf(stderr, "%d mismatch", i);
		  if (i != 1) (void)fputs("es", stderr);
		  (void)fputs(" found\n", stderr);
		}
	      else (void)fputs("No mismatches found\n", stderr);
	    }
	  ret = 0;
	}

      rulexdb_close(db);
      if (ret) return EXIT_FAILURE;
      else return EXIT_SUCCESS;
    }

  else if (d == line) /* Cleaning the database (get rid of redundant records) */
    {
      if (rules_data)
	{
	  (void)fprintf(stderr, "Invalid dataset specification\n");
	  return EXIT_FAILURE;
	}
      db = rulexdb_open(db_path, RULEXDB_UPDATE);
      if (!db)
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}
      if (verbose)
	(void)fputs("Cleaning the database\n", stderr);
      n = 0;
      search_mode = RULEXDB_FORMS | RULEXDB_RULES;
      if ((dataset == RULEXDB_DEFAULT) || (dataset == RULEXDB_LEXBASE))
        {
          regmatch_t match;
          (void)rulexdb_load_ruleset(db, RULEXDB_PREFIX);
          for (ret = rulexdb_seq(db, key, value, RULEXDB_LEXBASE, DB_FIRST);
               ret == RULEXDB_SUCCESS;
               ret = rulexdb_seq(db, key, value, RULEXDB_LEXBASE, DB_NEXT))
            if (rulexdb_classify(db, key) == RULEXDB_SUCCESS)
              {
                if (!rulexdb_remove_this_item(db, RULEXDB_LEXBASE))
                  n++;
              }
            else
              {
                for (i = 0; i < db->prefixes.nrules; i++)
                  if ((!regexec(db->prefixes.pattern[i], key, 1, &match, 0)) &&
                      (!match.rm_so) &&
                      (match.rm_eo < strlen(key)))
                    {
                      char stem[RULEXDB_BUFSIZE];
                      if (db->prefixes.replacement[i])
                        (void)strcpy(stem, db->prefixes.replacement[i]);
                      else stem[0] = 0;
                      k = strlen(stem);
                      (void)strcat(stem, key + match.rm_eo);
                      if (!rulexdb_retrieve_item(db, stem, line + match.rm_eo - k, RULEXDB_LEXBASE))
                        {
                          (void)strncpy(line, key, match.rm_eo);
                          if (!strcmp(line, value))
                            {
                              if (!rulexdb_remove_this_item(db, RULEXDB_LEXBASE))
                                n++;
                              break;
                            }
                        }
                    }
              }
        }
      if ((dataset == RULEXDB_DEFAULT) || (dataset == RULEXDB_EXCEPTION))
	for (ret = rulexdb_seq(db, key, value, RULEXDB_EXCEPTION, DB_FIRST);
	     ret == RULEXDB_SUCCESS;
	     ret = rulexdb_seq(db, key, value, RULEXDB_EXCEPTION, DB_NEXT))
	  {
	    (void)rulexdb_search(db, key, line, search_mode);
	    if (!strcmp(line, value))
	      if (!rulexdb_remove_this_item(db, RULEXDB_EXCEPTION))
		n++;
	  }
      if (!quiet)
	{
	  if (n)
	    (void)fprintf(stderr, "%i redundant record", n);
	  else (void)fputs("No redundant record", stderr);
	  if (n == 1)
	    (void)fputs(" has", stderr);
	  else (void)fputs("s have", stderr);
	  (void)fputs(" been ", stderr);
	  if (n)
	    (void)fputs("removed from the database", stderr);
	  else (void)fputs("encountered", stderr);
	  (void)fputc('\n', stderr);
	}
      rulexdb_close(db);
      return EXIT_SUCCESS;
    }

  else if (d) /* Discarding data */
    {
      if (dataset == RULEXDB_DEFAULT)
	{
	  (void)fprintf(stderr, "Dataset must be specified explicitly\n");
	  return EXIT_FAILURE;
	}
      db = rulexdb_open(db_path, RULEXDB_UPDATE);
      if (!db)
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}
      if (rules_data)
	{
	  if (d == key) /* Discard the ruleset */
	    {
	      if (verbose)
		(void)fprintf(stderr, "Discarding %s ruleset\n",
			      rulexdb_dataset_name(dataset));
	      k = rulexdb_discard_ruleset(db, dataset);
	      if ( k < 0) k = 0;
	    }
	  else /* Remove specific rule */
	    {
	      k = atoi(d);
	      if (k)
		ret = rulexdb_remove_rule(db, dataset, k);
	      else ret = RULEXDB_EINVKEY;
	    }
	}
      else if (d == key) /* Discard dictionary */
	{
	  if (verbose)
	    (void)fprintf(stderr, "Discarding %s dictionary\n",
			  rulexdb_dataset_name(dataset));
	  k = rulexdb_discard_dictionary(db, dataset);
	  if (k < 0) k = 0;
	}
      else /* Remove specific item from dictionary */
	{
	  for (k = 0; d[k]; k++)
	    if (isupper(d[k]))
	      d[k] = tolower(d[k]);
	  ret = rulexdb_remove_item(db, d, dataset);
	}
      if (d == key)
	{
	  ret = RULEXDB_SUCCESS;
	  if (!quiet)
	    {
	      (void)fprintf(stderr, "%d record", k);
	      if (k == 1)
		(void)fputs(" has", stderr);
	      else (void)fputs("s have", stderr);
	      (void)fputs(" been removed from the database\n", stderr);
	    }
	}
      if (ret == RULEXDB_EINVKEY)
	(void)fprintf(stderr, "Invalid key: %s\n", d);
      rulexdb_close(db);
      if (ret) return EXIT_FAILURE;
      else return EXIT_SUCCESS;
    }

  else /* Put new records into the database */
    {
      if (srcf)
	if (!freopen(srcf, "r", stdin))
	  {
	    (void)fprintf(stderr, "Cannot open %s for input\n", srcf);
	    return EXIT_FAILURE;
	  }
      db = rulexdb_open(db_path, RULEXDB_CREATE);
      if (!db)
	{
	  perror(db_path);
	  return EXIT_FAILURE;
	}
    }

  /* Collecting data */
  if (rules_data)
    {
      if (replace_mode)
	{
	  if (verbose)
	    (void)fprintf(stderr, "Replacing %s ruleset\n",
			  rulexdb_dataset_name(dataset));
	  (void)rulexdb_discard_ruleset(db, dataset);
	}
      else if (verbose)
	(void)fprintf(stderr, "Adding rules to %s ruleset\n",
		      rulexdb_dataset_name(dataset));
    }
  for (n = 1; fgets(line, 256, stdin); n++)
    {
      if (strlen(line) > RULEXDB_MAX_RECORD_SIZE)
	{
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%d: warning: Record too long. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr, "Ignored too long record: %s\n", line);
	  invalid++;
	  continue;
	}
      for (k = 0; line[k]; k++)
	if (isupper(line[k]))
	  line[k] = tolower(line[k]);
      if (!rules_data)
	{
	  s = strtok(line, " ");
	  if (strlen(s) > RULEXDB_MAX_KEY_SIZE)
	    {
	      if (srcf)
		(void)fprintf(stderr,
			      "%s:%d: warning: Too long key. Ignored.\n",
			      srcf, n);
	      else (void)fprintf(stderr, "Ignored too long key: %s\n", s);
	      invalid++;
	      continue;
	    }
	  t = strtok(NULL, "\n");
	  ret = rulexdb_subscribe_item(db, s, t,
				     dataset, replace_mode);
	}
      else ret = rulexdb_subscribe_rule(db, strtok(line, "\n"), dataset, 0);
      switch(ret)
	{
	  case RULEXDB_SUCCESS:
	    i++;
	    break;
	  case RULEXDB_SPECIAL:
	    if (replace_mode) i++;
	    if (!(quiet || rules_data))
	      {
		if (srcf)
		  {
		    (void)fprintf(stderr,
				  "%s:%d: warning: Duplicate entry. ",
				  srcf, n);
		    if (replace_mode)
		      (void)fprintf(stderr, "Replaced.\n");
		    else (void)fprintf(stderr, "Ignored.\n");
		  }
		else
		  {
		    if (replace_mode)
		      (void)fputs("Replaced", stderr);
		    else (void)fputs("Ignored", stderr);
		    (void)fprintf(stderr, " duplicate entry: %s %s\n", s, t);
		  }
	      }
	    duplicate++;
	    break;
	  case RULEXDB_EINVKEY:
	  if (srcf)
	    (void)fprintf(stderr,
			  "%s:%d: warning: Illegal symbols in key. Ignored.\n",
			  srcf, n);
	  else (void)fprintf(stderr,
			     "Ignored key containing illegal symbols: %s\n",
			     s);
	  invalid++;
	    break;
	  case RULEXDB_EINVREC:
	    if (srcf)
	      (void)fprintf(stderr,
			    "%s:%d: warning: Invalid record. Ignored.\n",
			    srcf, n);
	    else
	      {
		(void)fprintf(stderr, "Ignored invalid record: %s", s);
		if (!rules_data)
		  (void)fprintf(stderr, " %s", t);
		(void)fputc('\n', stderr);
	      }
	    invalid++;
	    break;
	  default:
	    rulexdb_close(db);
	    if (srcf)
	      (void)fprintf(stderr,
			    "%s:%d: error: data storing error\n",
			    srcf, n);
	    else (void)fprintf(stderr, "Data storing error\n");
	    return EXIT_FAILURE;
	}
    }
  n--;

  rulexdb_close(db);

  if (!quiet)
    {
      (void)fprintf(stderr, "Total: %d record", n);
      if (n != 1) (void)fputc('s', stderr);
      (void)fprintf(stderr, " processed.\n");
      if (invalid)
	(void)fprintf(stderr, "Invalid records: %d\n", invalid);
      if (duplicate)
	(void)fprintf(stderr, "Duplicates: %d\n", duplicate);
      (void)fprintf(stderr, "%d record", i);
      if (i == 1)
	(void)fputs(" has", stderr);
      else (void)fputs("s have", stderr);
      (void)fputs(" been put into the database\n", stderr);
    }

  return EXIT_SUCCESS;
}
