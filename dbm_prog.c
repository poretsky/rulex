#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include "coder.h"

#if defined(FBSD_DATABASE)
#include <db.h>
#elif defined(BERKELEYDB)
#include <db_185.h>
#else
#include <ndbm.h>
#define RET_SUCCESS 0
#define RET_SPECIAL 1
#endif

#ifdef FBSD_DATABASE	/* this needs to be explicit  */
HASHINFO openinfo = {
        128,           /* bsize */
        8,             /* ffactor */
        400000,         /* nelem */
        2048 * 1024,   /* cachesize */
        NULL,          /* hash */
        0              /* lorder */
};
#endif

static void usage(char *name)
{
	(void)fprintf(stderr, "Usage: %s", name);
	(void)fprintf(stderr, "\t[-v] -o output_file_without_extension -i input_file\n");
	(void)fprintf(stderr, "Use \"-\" as an input file name for the standard input\n");
	(void)fprintf(stderr, "-v means to be verbose about redundant records\n");
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

	FILE *ifd;
	char *s, line[256], packed_key[256], packed_data[256];
	char *iptr;
	char *optr;
        extern char *optarg;
        extern int optind;

	char c;
	int  ret, key_size, data_size, n = 0, i = 0;
	int redundant = 0, invalid = 0, verbose = 0;
	int duplicate = 0;

	/* read in file name  */
	if(argc==1) {
                usage(argv[0]);
		return EXIT_FAILURE;
	}
	while((c = getopt(argc,argv,"i:o:v")) != -1) {
                switch(c) {
                        case 'i':
                                iptr = optarg;
                                break;
                        case 'o':
                                optr = optarg;
                                break;
			case 'v':
				verbose = 1;
				break;
			default:
				usage(argv[0]);
				return EXIT_FAILURE;
		}
	}

	if(!strcmp(iptr,"-")) {
		ifd = stdin;
        } else if ((ifd=fopen(iptr,"r")) == NULL) {
                (void)fprintf(stderr,"Can't open file %s\n",iptr);
                return EXIT_FAILURE;
        }

#if defined(BERKELEYDB)
        db = dbopen(NULL,O_RDWR|O_CREAT, 0000644, DB_BTREE, NULL);
	if (!db)
	  {
	    (void)fprintf(stderr, "Cannot initialize temporary database\n");
	    return EXIT_FAILURE;
	  }
#elif defined(FBSD_DATABASE)
        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_HASH, &openinfo);
	if (!db)
	  {
	    (void)fprintf(stderr, "Cannot initialize database %s\n", optr);
	    return EXIT_FAILURE;
	  }
#else
	db = dbm_open(optr,O_RDWR|O_CREAT, 0000644);
	if (!db)
	  {
	    (void)fprintf(stderr, "Cannot initialize database %s\n", optr);
	    return EXIT_FAILURE;
	  }
#endif

	while(fgets(line,256,ifd)) {
		n++;
		s = strtok(line," ");
		key_size = pack_key(s, packed_key);
		if (key_size < 0)
			{
				fprintf(stderr,
					"%s:%i: warning: Illegal symbols in key. Ignored.\n",
					iptr, n);
				invalid++;
				continue;
			}
		data_size = pack_data(s, strtok(NULL,"\n"), packed_data);
		if (data_size < 0)
			{
				fprintf(stderr,
					"%s:%i: warning: Invalid record. Ignored.\n",
					iptr, n);
				invalid++;
				continue;
			}
		if (!data_size)
			{
				if (verbose)
					fprintf(stderr,
						"%s:%i: warning: Redundant entry. Ignored.\n",
						iptr, n);
				redundant++;
				continue;
			}
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
		inKey.data = packed_key;
		inKey.size = key_size;
		inVal.data = packed_data;
		inVal.size = data_size;
		ret = (db->put)(db,&inKey,&inVal,R_NOOVERWRITE);
#else
		inKey.dptr = packed_key;
		inKey.dsize = key_size;
		inVal.dptr = packed_data;
		inVal.dsize = data_size;
		ret = dbm_store(db,inKey,inVal,DBM_INSERT);
#endif
		switch(ret) {
			case RET_SUCCESS:
				i++;
				break;
			case RET_SPECIAL:
				fprintf(stderr,
					"%s:%i: warning: Duplicate entry. Ignored.\n",
					iptr, n);
				duplicate++;
				break;
			default:
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
				(void)(db->close)(db);
#else
				dbm_close(db);
#endif
				fprintf(stderr,
					"%s:%i: error: storing error\n",
					iptr, n);
				return EXIT_FAILURE;
		}
	}

#if defined(FBSD_DATABASE)
	(void)(db->close)(db);
#elif defined(BERKELEYDB)
	tmpdb = db;
        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_BTREE, NULL);
	if (!db)
	  {
	    (void)fprintf(stderr, "Cannot initialize database %s\n", optr);
	    (void)(tmpdb->close)(tmpdb);
	    return EXIT_FAILURE;
	  }
	(void)fprintf(stderr, "Dumping the database\n");
	while(!tmpdb->seq(tmpdb, &inKey, &inVal, R_NEXT))
	  if ((db->put)(db,&inKey,&inVal,R_NOOVERWRITE))
	    {
	      (void)fprintf(stderr, "Database write error\n");
	      (void)(tmpdb->close)(tmpdb);
	      (void)(db->close)(db);
	      return EXIT_FAILURE;
	    }
	(void)(tmpdb->close)(tmpdb);
	(void)(db->close)(db);
#else
	dbm_close(db);
#endif

	(void)fprintf(stderr, "%d total records processed.\n", n);
	if (invalid)
	  (void)fprintf(stderr, "Invalid records: %d\n", invalid);
	if (duplicate)
	  (void)fprintf(stderr, "Duplicates: %d\n", duplicate);
	if (redundant)
	  (void)fprintf(stderr, "Redundant entries: %d\n", redundant);
	(void)fprintf(stderr,
		      "%d records have been put into the database\n", i);

	return EXIT_SUCCESS;
}
