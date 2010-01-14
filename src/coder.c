/* coder.c
 *
 * Lexical data coding routines
 *
 * These routines provide transition between external and internal
 * data representation in the lexical database.
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


#include <stdlib.h>
#include <string.h>
#include "coder.h"

#define turnout(bit) \
{ \
  if (bit) \
    t[l] |= mask; \
  mask >>= 1; \
  if (!mask) \
    { \
      mask = 0x80; \
      t[++l] = 0; \
    } \
}

/* List of valid letters */
static unsigned char alphabet[] = "�����ţ��������������������������";

/* Statistical model for keys packing */
static SYMBOL letter[] =
  {
    { 0, 185 }, /* � */
    { 185, 219 }, /* � */
    { 219, 320 }, /* � */
    { 320, 354 }, /* � */
    { 354, 404 }, /* � */
    { 404, 580 }, /* � */
    { 580, 582 }, /* � */
    { 582, 598 }, /* � */
    { 598, 637 }, /* � */
    { 637, 797 }, /* � */
    { 797, 828 }, /* � */
    { 828, 900 }, /* � */
    { 900, 995 }, /* � */
    { 995, 1068 }, /* � */
    { 1068, 1214 }, /* � */
    { 1214, 1419 }, /* � */
    { 1419, 1488 }, /* � */
    { 1488, 1609 }, /* � */
    { 1609, 1724 }, /* � */
    { 1724, 1838 }, /* � */
    { 1838, 1900 }, /* � */
    { 1900, 1907 }, /* � */
    { 1907, 1929 }, /* � */
    { 1929, 1939 }, /* � */
    { 1939, 1965 }, /* � */
    { 1965, 1991 }, /* � */
    { 1991, 2005 }, /* � */
    { 2005, 2006 }, /* � */
    { 2006, 2053 }, /* � */
    { 2053, 2089 }, /* � */
    { 2089, 2091 }, /* � */
    { 2091, 2114 }, /* � */
    { 2114, 2162 }, /* � */
    { 2162, 2390 } /* EOS */
  };
static unsigned short int scale = 2390;

static int validate_pair(char prev, char next)
     /*
      * This routine checks validity of the letter pairs in words,
      * Returns 0 on valid pair or -1 otherwise.
      */
{
  if (next && index("��", next))
    if (index("+-=�ţ����������", prev))
      return -1;
  return 0;
}


int pack_key(const char *s, char *t)
     /*
      * This routine packs string pointed by s using arithmetic coding
      * and places result to the string pointed by t.
      * Returns packed data length on success or -1 if source string
      * contains invalid characters.
      */
{
  long int range, underflow_bits = 0;
  unsigned short int low = 0, high = 0xffff, mask = 0x80;
  char table[256];
  int i, j, l = 0;

  /* Initialize symbol table */
  for (i = 0; i < 256; i++)
    table[i] = -1;
  for (i = 0; i < strlen(alphabet) + 1; i++)
    table[alphabet[i]] = (char)i;

  /* Packing data */
  t[0] = 0;
  for (i = 0; i <= strlen(s); i++)
    {
      /* Get the next symbol and check it's validity */
      if (i)
	{
	  if (validate_pair(s[i - 1], s[i]))
	    return -1;
	}
      else if (index("���", s[i]))
	return -1;
      j = (int)table[s[i] & 0xff];
      if (j < 0)
	return -1;

      /* Rescale high and low for the new symbol */
      range = (long int)(high - low) + 1;
      high = low + (unsigned short int)(range * letter[j].high / scale - 1);
      low += (unsigned short int)(range * letter[j].low / scale);

      /* Turn out new bits for packed data */
      while (1)
	{
	  if ((high & 0x8000) == (low & 0x8000))
	    {
	      turnout(high & 0x8000);
	      while (underflow_bits > 0)
		{
		  turnout(~high & 0x8000);
		  underflow_bits--;
		}
	    }
	  else if ((low & 0x4000) && !(high & 0x4000))
	    {
	      underflow_bits++;
	      low &= 0x3fff;
	      high |= 0x4000;
	    }
	  else break;
	  low <<= 1;
	  high <<= 1;
	  high |= 1;
	}
    }

  /* Flush packed data */
  turnout(low & 0x4000);
  underflow_bits++;
  while (underflow_bits-- > 0)
    turnout(~low & 0x4000);
  if (mask != 0x80)
    l++;
  return l;
}

int unpack_key(const char *key, unsigned int keylen, char *t, unsigned int reslen)
     /*
      * This routine unpacks given key and stores the resulting string
      * in memory pointed by t. The reslen holds the size of this area.
      *
      * Returns 0 on success, or -1 when allocated memory is insufficient
      * for result string.
      */
{
  long int range;
  unsigned short int code, count, low = 0, high = 0xffff, mask = 0x80;
  int i, k, l;

  l = 0;
  if (keylen)
    {
      /* Get initial portion of packed data */
      code = key[0];
      code <<= 8;
      k = 1;
      if (keylen > 1)
	code += (unsigned char)key[k++];
      while (l < reslen) /* Decoding loop */
	{
	  /* Calculate current count */
	  range = (long int)(high - low) + 1;
	  count = (short int)((((long int)(code - low) + 1) * scale - 1)
			      / range);
	  /* Find the corresponding symbol */
	  for (i = strlen(alphabet) + 1; i > 0; i--)
	    if (count >= letter[i].low)
	      break;
	  if (!alphabet[i]) /* End of string */
	    break; /* Finish decoding process */
	  t[l++] = alphabet[i];
	  /* Calculate new low and high values */
	  high = low + (unsigned short int)((range * letter[i].high)
					    / scale - 1);
	  low += (unsigned short int)((range * letter[i].low) / scale);
	  /* Get next bits of code */
	  while (1)
	    {
	      if ((high ^ low) & 0x8000)
		{
		  if ((low & 0x4000) && !(high & 0x4000))
		    {
		      code ^= 0x4000;
		      low &= 0x3fff;
		      high |= 0x4000;
		    }
		  else break;
		}
	      low <<= 1;
	      high <<= 1;
	      high |= 1;
	      code <<= 1;
	      if (k < keylen)
		{
		  if (key[k] & mask) code++;
		  mask >>= 1;
		  if (!mask)
		    {
		      k++;
		      mask = 0x80;
		    }
		}
	    }
	}
    }

  /* Finalization */
  if (l < reslen)
    {
      t[l] = 0;
      return 0;
    }
  else return -1;
}


int pack_data(const char *s, const char *t, char *r)
     /*
      * This routine packs data field for corresponding key.
      * It takes two strings s and t and forms a special string in r
      * describing transition from s to t.
      * Returns length of result string on success
      * or -1 when string t looks somewhat wrong.
      */
{
  int rc, i, k, l = 0;
  char *d, *w;

  /* Detect some illegal sequences in t */
  if (index("�-�", t[0]))
    return -1;
  for (i = 1; i < strlen(t); i++)
    if (validate_pair(t[i - 1], t[i]))
      return -1;

  /* Make a local copy of t for further manipulations */
  d = strdup(t);
  if (!d)
    return -1;

  /* Detect legal non-alphabetical characters */
  rc = 0;
  w = d;
  while ((i = strspn(w, alphabet)) != strlen(w))
    {
      switch (w[i])
	{
	  case '+':
	    if (i && index("�ţ�������", w[i - 1]))
	      r[l++] = MAJOR_STRESS | i;
	    else rc = 1;
	    break;
	  case '=':
	    if (i && index("�ţ�������", w[i - 1]))
	      r[l++] = MINOR_STRESS | i;
	    else rc =1;
	    break;
	  case '-':
	    r[l++] = SPACE_BAR | i;
	    break;
	  default:
	    rc = 1;
	    break;
	}
      if (rc)
	{
	  free(d);
	  return -1;
	}
      w += i;
      (void)memmove(w, w + 1, strlen(w));
    }

  /* Detect letters replacing, inserting and removing */
  r[l] = 0;
  i = k = 0;
  while (strcmp(&s[i], &d[k]))
    {
      if (s[i] == d[k])
	{
	  i++;
	  k++;
	  ++r[l];
	}
      else if (s[i] && d[k])
	{
	  if ((strlen(&s[i]) > strlen(&d[k]))
	      && !strcmp(&s[strlen(s) - strlen(&d[k])], &d[k]))
	    {
	      if ((!r[l]) && (i || k))
		switch (r[l - 1] & ACTION_MASK)
		  {
		    case REMOVE_CHAR:
		      r[--l] += strlen(&s[i]) - strlen(&d[k]);
		      break;
		    case INSERT_CHAR:
		      r[--l] &= ~ACTION_MASK;
		      r[l] |= REPLACE_CHAR;
		      if (strlen(&s[++i]) != strlen(&d[k]))
			r[++l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&d[k]));
		      break;
		    default:
		      r[l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&d[k]));
		      break;
		  }
	      else r[++l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&d[k]));
	      r[++l] = 0;
	      i += strlen(&s[i]) - strlen(&d[k]);
	    }
	  else if (s[i] == d[k + 1])
	    {
	      if ((!r[l]) && (i || k))
		{
		  if (r[--l] == (REMOVE_CHAR | 1))
		    r[l] = REPLACE_CHAR;
		  else r[++l] = INSERT_CHAR;
		}
	      else r[++l] = INSERT_CHAR;
	      r[l] |= index(alphabet, d[k++]) - (char *)alphabet;
	      r[++l] = 0;
	    }
	  else if (s[i + 1] == d[k])
	    {
	      if ((!r[l]) && (i || k))
		switch (r[l - 1] & ACTION_MASK)
		  {
		    case REMOVE_CHAR:
		      ++r[--l];
		      break;
		    case INSERT_CHAR:
		      r[--l] &= ~ACTION_MASK;
		      r[l] |= REPLACE_CHAR;
		      break;
		    default:
		      r[l] = REMOVE_CHAR | 1;
		      break;
		  }
	      else r[++l] = REMOVE_CHAR | 1;
	      r[++l] = 0;
	      i++;
	    }
	  else
	    {
	      if (r[l] || !(i || k)) l++;
	      r[l] = (index(alphabet, d[k++]) - (char *)alphabet) | REPLACE_CHAR;
	      r[++l] = 0;
	      i++;
	    }
	}
      else if (d[k])
	{
	  if ((!r[l]) && (i || k))
	    {
	      if (r[--l] == (REMOVE_CHAR | 1))
		r[l] = REPLACE_CHAR;
	      else r[++l] = INSERT_CHAR;
	    }
	  else r[++l] = INSERT_CHAR;
	  r[l] |= index(alphabet, d[k++]) - (char *)alphabet;
	  r[++l] = 0;
	}
      else if (s[i])
	{
	  if ((!r[l]) && (i || k))
	    switch (r[l - 1] & ACTION_MASK)
	      {
		case REMOVE_CHAR:
		  ++r[--l];
		  break;
		case INSERT_CHAR:
		  r[--l] &= ~ACTION_MASK;
		  r[l] |= REPLACE_CHAR;
		  break;
		default:
		  r[l] = REMOVE_CHAR | 1;
		  break;
	      }
	  else r[++l] = REMOVE_CHAR | 1;
	  r[++l] = 0;
	  i++;
	}
      else break;
    }

  free(d);
  return l;
}

void unpack_data(char *s, const char *diffs, int diffs_size)
     /*
      * This routine unpacks data field for corresponding key.
      * It takes the original key string pointer as it's first argument
      * and transforms it according to given diffs.
      */
{
  int i, k, l;

  if (!diffs_size) return;

  /*
   * The transition description may consist of two parts
   * and we have to apply it in the reverse order.
   */

  /* At first let's locate the second part */
  l = diffs_size;
  for (i = 0; i < diffs_size; i++)
    if (!(diffs[i] & ACTION_MASK))
      {
	l = i;
	break;
      }

  /* Applying the second part if any */
  k = 0;
  for (i = l; i < diffs_size; i++)
    switch (diffs[i] & ACTION_MASK)
      { /* letters replacing, inserting and removing */
	case REPLACE_CHAR:
	  s[k++] = alphabet[(unsigned char)diffs[i] & ~ACTION_MASK];
	  break;
	case INSERT_CHAR:
	  (void)memmove(&s[k + 1], &s[k], strlen(&s[k]) + 1);
	  s[k++] = alphabet[(unsigned char)diffs[i] & ~ACTION_MASK];
	  break;
	case REMOVE_CHAR:
	  (void)memmove(&s[k], &s[k + ((unsigned char)diffs[i] & ~ACTION_MASK)],
			strlen(&s[k + ((unsigned char)diffs[i] & ~ACTION_MASK)]) + 1);
	  break;
	default:
	  k += (unsigned char)diffs[i];
	  break;
      }

  /* Now applying the first part of diffs if any */
  k = 0;
  for (i = 0; i < l; i++)
    {
      /* Stress marking */
      k += (unsigned char)diffs[i] & ~ACTION_MASK;
      (void)memmove(&s[k + 1], &s[k], strlen(&s[k]) + 1);
      switch (diffs[i] & ACTION_MASK)
	{
	  case MAJOR_STRESS:
	    s[k++] = '+';
	    break;
	  case MINOR_STRESS:
	    s[k++] = '=';
	    break;
	  case SPACE_BAR:
	    s[k++] = '-';
	    break;
	  default:
	    s[k++] = ' ';
	    break;
	}
    }

  /* That's all */
  return;
}