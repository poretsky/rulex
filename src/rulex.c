/* Simple text markup utility */

/*
 * Copyright (C) 2012 Igor B. Poretsky <poretsky@mlbox.ru>
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


#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <locale.h>
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lexdb.h"


static const char *charset = "ru_RU.koi8r";

static const char symbols[] =
  {
    '+', '=',
    0xC1, 0xC2, 0xD7, /* а, б, в, */
    0xC7, 0xC4, 0xC5, /* г, д, е, */
    0xA3, 0xD6, 0xDA, /* ё, ж, з, */
    0xC9, 0xCA, 0xCB, /* и, й, к, */
    0xCC, 0xCD, 0xCE, /* л, м, н, */
    0xCF, 0xD0, 0xD2, /* о, п, р, */
    0xD3, 0xD4, 0xD5, /* с, т, у, */
    0xC6, 0xC8, 0xC3, /* ф, х, ц, */
    0xDE, 0xDB, 0xDD, /* ч, ш, щ, */
    0xDF, 0xD9, 0xD8, /* ъ, ы, ь, */
    0xDC, 0xC0, 0xD1, /* э, ю, я */
    0
  };

static const char *alphabet;

static void *xrealloc(void *p, unsigned int n)
{
  void *u = realloc(p, n);
  if (u) return u;
  else
    {
      perror("Memory allocation error");
      exit(EXIT_FAILURE);
    }
}

static void *xmalloc(unsigned int n)
{
  void *u = NULL;
  return xrealloc(u, n);
}

int main(int argc, char **argv)
{
  unsigned int size = 64;
  char *s, *text;
  FILE *slog = NULL;
  RULEXDB *db = NULL;

  if (argc < 2)
    {
      fprintf(stderr, "Usage:\n");
      fprintf(stderr, "%s <rulex_db> [<logfile>]\n", argv[0]);
      return EXIT_FAILURE;
    }

  alphabet = symbols + 2;
  if (!setlocale(LC_CTYPE, charset))
    {
      fprintf(stderr, "Cannot set %s locale.\n", charset);
      fprintf(stderr, "Probably you have to generate it by command:\n");
      fprintf(stderr, "localedef -f KOI8-R -i ru_RU ru_RU.KOI8-R\n");
      return EXIT_FAILURE;
    }

  db = rulexdb_open(argv[1], RULEXDB_SEARCH);
  if (!db)
    {
      perror(argv[1]);
      return EXIT_FAILURE;
    }

  if (argc > 2)
    {
      slog = fopen(argv[2], "a");
      if (!slog)
        perror(argv[2]);
    }

  /* doing text filtering in the loop */
  text = xmalloc(size);
  s = text;
  while (fgets(s, text + size - s, stdin))
    {
      s = text + strlen(text) - 1;
      if (*s == '\n')
	{
          char *stressed = xmalloc(strlen(text) << 1);
          char *t;
          unsigned int n;
          for (s = text; *s; s++)
            if (isupper(*s))
              *s = tolower(*s);
          s = text;
          t = stressed;
          while (*s)
            {
              if ((n = strcspn(s, symbols)))
                {
                  strncpy(t, s, n);
                  s += n;
                  t += n;
                }
              if ((n = strspn(s, symbols)))
                {
                  if ((n <= RULEXDB_MAX_KEY_SIZE) && (n <= strspn(s, alphabet)))
                    {
                      char *key = xmalloc(RULEXDB_BUFSIZE);
                      strncpy(key, s, n);
                      key[n] = 0;
                      if (rulexdb_search(db, key, t, 0)
                          == RULEXDB_SPECIAL)
                        if (slog) (void)fprintf(slog, "%s\n", key);
                      free(key);
                    }
                  else
                    {
                      strncpy(t, s, n);
                      t[n] = 0;
                    }
                  s += n;
                  t += strlen(t);
                }
            }
          *t = 0;
          puts(stressed);
          free(stressed);
	  s = text;
	}
      else
	{
	  text = xrealloc(text, size <<= 1);
	  s = text + strlen(text);
	}
    }

  rulexdb_close(db);
  if (slog)
    fclose(slog);
  free(text);

  return EXIT_SUCCESS;
}
