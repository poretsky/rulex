/* Data coding routines */

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
static unsigned char alphabet[] = "ÁÂ×ÇÄÅ£ÖÚÉÊËÌÍÎÏÐÒÓÔÕÆÈÃÞÛÝßÙØÜÀÑ";

/* Statistical model for keys packing */
static SYMBOL letter[] =
  {
    { 0, 185 }, /* Á */
    { 185, 219 }, /* Â */
    { 219, 320 }, /* × */
    { 320, 354 }, /* Ç */
    { 354, 404 }, /* Ä */
    { 404, 580 }, /* Å */
    { 580, 582 }, /* £ */
    { 582, 598 }, /* Ö */
    { 598, 637 }, /* Ú */
    { 637, 797 }, /* É */
    { 797, 828 }, /* Ê */
    { 828, 900 }, /* Ë */
    { 900, 995 }, /* Ì */
    { 995, 1068 }, /* Í */
    { 1068, 1214 }, /* Î */
    { 1214, 1419 }, /* Ï */
    { 1419, 1488 }, /* Ð */
    { 1488, 1609 }, /* Ò */
    { 1609, 1724 }, /* Ó */
    { 1724, 1838 }, /* Ô */
    { 1838, 1900 }, /* Õ */
    { 1900, 1907 }, /* Æ */
    { 1907, 1929 }, /* È */
    { 1929, 1939 }, /* Ã */
    { 1939, 1965 }, /* Þ */
    { 1965, 1991 }, /* Û */
    { 1991, 2005 }, /* Ý */
    { 2005, 2006 }, /* ß */
    { 2006, 2053 }, /* Ù */
    { 2053, 2089 }, /* Ø */
    { 2089, 2091 }, /* Ü */
    { 2091, 2114 }, /* À */
    { 2114, 2162 }, /* Ñ */
    { 2162, 2390 } /* EOS */
  };
static unsigned short int scale = 2390;

int pack_key(char *s, char *t)
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
  for (i = 0; i < strlen(s) + 1; i++)
    {
      /* Get the next symbol and check it's validity */
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

int pack_data(char *s, char *t, char *r)
     /*
      * This routine packs data field for corresponding key.
      * It takes two strings s and t and forms a special string in r
      * describing transition from s to t.
      * This procedure destroys string t by the way.
      * Returns length of result string on success
      * or -1 when string t looks somewhat wrong.
      */
{
  int i, k, l = 0;
  char *w;

  /* Detect legal non-alphabetical characters */
  w = t;
  while ((i = strspn(w, alphabet)) != strlen(w))
    {
      switch (w[i])
	{
	  case '+':
	    if (!i) return -1;
	    if (!index("ÁÅ£ÉÏÕÙÜÀÑ", w[i - 1])) return -1;
	    r[l++] = MAJOR_STRESS | i;
	    break;
	  case '=':
	    if (!i) return -1;
	    if (!index("ÁÅ£ÉÏÕÙÜÀÑ", w[i - 1])) return -1;
	    r[l++] = MINOR_STRESS | i;
	    break;
	  case '-':
	    r[l++] = SPACE_BAR | i;
	    break;
	  default:
	    return -1;
	}
      w += i;
      (void)memmove(w, w + 1, strlen(w));
    }

  /* Detect letters replacing, inserting and removing */
  r[l] = 0;
  i = k = 0;
  while (strcmp(&s[i], &t[k]))
    {
      if (s[i] == t[k])
	{
	  i++;
	  k++;
	  ++r[l];
	}
      else if (s[i] && t[k])
	{
	  if ((strlen(&s[i]) > strlen(&t[k]))
	      && !strcmp(&s[strlen(s) - strlen(&t[k])], &t[k]))
	    {
	      if ((!r[l]) && (i || k))
		switch (r[l - 1] & ACTION_MASK)
		  {
		    case REMOVE_CHAR:
		      r[--l] += strlen(&s[i]) - strlen(&t[k]);
		      break;
		    case INSERT_CHAR:
		      r[--l] &= ~ACTION_MASK;
		      r[l] |= REPLACE_CHAR;
		      if (strlen(&s[++i]) != strlen(&t[k]))
			r[++l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&t[k]));
		      break;
		    default:
		      r[l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&t[k]));
		      break;
		  }
	      else r[++l] = REMOVE_CHAR | (strlen(&s[i]) - strlen(&t[k]));
	      r[++l] = 0;
	      i += strlen(&s[i]) - strlen(&t[k]);
	    }
	  else if (s[i] == t[k + 1])
	    {
	      if ((!r[l]) && (i || k))
		{
		  if (r[--l] == (REMOVE_CHAR | 1))
		    r[l] = REPLACE_CHAR;
		  else r[++l] = INSERT_CHAR;
		}
	      else r[++l] = INSERT_CHAR;
	      r[l] |= index(alphabet, t[k++]) - (char *)alphabet;
	      r[++l] = 0;
	    }
	  else if (s[i + 1] == t[k])
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
	      r[l] = (index(alphabet, t[k++]) - (char *)alphabet) | REPLACE_CHAR;
	      r[++l] = 0;
	      i++;
	    }
	}
      else if (t[k])
	{
	  if ((!r[l]) && (i || k))
	    {
	      if (r[--l] == (REMOVE_CHAR | 1))
		r[l] = REPLACE_CHAR;
	      else r[++l] = INSERT_CHAR;
	    }
	  else r[++l] = INSERT_CHAR;
	  r[l] |= index(alphabet, t[k++]) - (char *)alphabet;
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
  return l;
}
