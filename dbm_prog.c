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

	/* read in file name  */
	if(argc==1) {
        (void)fprintf(stderr, "Usage: %s",argv[0]);
        (void)fprintf(stderr,"\t-o output_file_without_extention -i input_file\n");
                exit(2);
	}
	while((c = getopt(argc,argv,"i:o:")) != -1) {
                switch(c) {
                        case 'i':
                                iptr = optarg;
                                break;
                        case 'o':
                                optr = optarg;
                                break;
			default:
				(void)fprintf(stderr, "Usage: %s",argv[0]);
				(void)fprintf(stderr,"\t-o output_file_without_extention -i input_file\n");
				exit(2);
		}
	}

	if(!strcmp(iptr,"-")) {
		ifd = stdin;
        } else if ((ifd=fopen(iptr,"r")) == NULL) {
                (void)fprintf(stderr,"Can't open file %s\n",iptr);
                exit(1);
        }

#if defined(BERKELEYDB)
        db = dbopen(NULL,O_RDWR|O_CREAT, 0000644, DB_BTREE, NULL);
#elif defined(FBSD_DATABASE)
        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_HASH, &openinfo);
#else
	db = dbm_open(optr,O_RDWR|O_CREAT, 0000644);
#endif

	while(fgets(line,256,ifd)) {
		n++;
		s = strtok(line," ");
		key_size = pack_key(s, packed_key);
		if (key_size < 0)
			{
				fprintf(stderr,"%s:%i: warning: Illegal symbols in key. Ignored.\n",iptr,n);
				continue;
			}
		data_size = pack_data(s, strtok(NULL,"\n"), packed_data);
		if (data_size < 0)
			{
				fprintf(stderr,"%s:%i: warning: Invalid record. Ignored.\n",iptr,n);
				continue;
			}
		if (!data_size)
			{
				fprintf(stderr,"%s:%i: warning: Redundant record. Ignored.\n",iptr,n);
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
				fprintf(stderr,"%s:%i: warning: Duplicate entry. Ignored.\n",iptr,n);
				break;
			default:
#if defined(FBSD_DATABASE) || defined(BERKELEYDB)
				(void)(db->close)(db);
#else
				dbm_close(db);
#endif
				fprintf(stderr,"%s:%i: error: storing error\n",iptr,n);
				fprintf(stderr,"%d words processed.\n",i);
				exit(1);
		}
	}

#if defined(FBSD_DATABASE)
	(void)(db->close)(db);
#elif defined(BERKELEYDB)
	tmpdb = db;
        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_BTREE, NULL);
	while(!tmpdb->seq(tmpdb, &inKey, &inVal, R_NEXT)) {
		ret = (db->put)(db,&inKey,&inVal,R_NOOVERWRITE);
	}
	(void)(tmpdb->close)(tmpdb);
	(void)(db->close)(db);
#else
	dbm_close(db);
#endif

	fprintf(stderr,"%d words processed.\n",i);

	return(0);
}
