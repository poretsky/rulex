/* Headers and definitions for data coding */

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

extern int pack_key(char *s, char *t);
/*
 * This routine packs string pointed by s using arithmetic coding
 * and places result to the string pointed by t.
 * Returns packed data length on success or -1 if source string
 * contains invalid characters.
 */

extern int pack_data(char *s, char *t, char *r);
/*
 * This routine packs data field for corresponding key.
 * It takes two strings s and t and forms a special string in r
 * describing transition from s to t.
 * This procedure destroys string t by the way.
 * Returns length of result string on success
 * or -1 when string t looks somewhat wrong.
 */
