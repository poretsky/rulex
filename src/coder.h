/* This file is part of the rulexdb library.
 *
 * Lexical data coding routines.
 *
 * These routines provide transition between external and internal
 * data representation in the Rulex database.
 *
 * In the database key fields are packed by the arithmetic coding algorithm
 * based on the static statistical model. Value fields are represented
 * by differences relative to the corresponding keys.
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


#ifndef CODER_H
#define CODER_H

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
#define ACTION_MASK 0xc0
#define MAJOR_STRESS 0x80
#define MINOR_STRESS 0x40
#define SPACE_BAR 0xc0
#define REPLACE_CHAR 0xc0
#define INSERT_CHAR 0x80
#define REMOVE_CHAR 0x40

typedef struct /* Symbol description for statistical model */
{
  unsigned short int low;
  unsigned short int high;
} SYMBOL;


extern int rulexdb_pack_key(const char *s, char *t);
/*
 * This routine packs string pointed by s using arithmetic coding
 * and places result to the string pointed by t.
 * Returns packed data length on success or -1 if source string
 * contains invalid characters.
 */

extern int rulexdb_unpack_key(const char *key, unsigned int keylen,
		      char *t, unsigned int reslen);
/*
 * This routine unpacks given key and stores the resulting string
 * in memory pointed by t. The reslen holds the size of this area.
 *
 * Returns 0 on success, or -1 when allocated memory is insufficient
 * for result string.
 */


extern int rulexdb_pack_data(const char *s, const char *t, char *r);
/*
 * This routine packs data field for corresponding key.
 * It takes two strings s and t and forms a special string in r
 * describing transition from s to t.
 * Returns length of result string on success
 * or -1 when string t looks somewhat wrong.
 */

extern void rulexdb_unpack_data(char *s, const char *diffs, int diffs_size);
/*
 * This routine unpacks data field for corresponding key.
 * It takes the original key string pointer as it's first argument
 * and transforms it according to given diffs.
 */

END_C_DECLS

#endif
