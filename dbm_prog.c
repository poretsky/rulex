#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#ifdef FBSD_DATABASE
#include <db.h>
#else
#include <ndbm.h>
#endif

#ifdef FBSD_DATABASE	/* this needs to be explicit  */
HASHINFO openinfo = {
        128,           /* bsize */
        8,             /* ffactor */
        155000,         /* nelem */
        2048 * 1024,   /* cachesize */
        NULL,          /* hash */
        0              /* lorder */
};
#endif

int main(int argc, char *argv[])
{
#ifdef FBSD_DATABASE
	DBT inKey, inVal;
	DB * db;
#else
	DBM * db;
	datum Key;
	datum Val;
#endif

	FILE *ifd;
	char line[256];
	char *iptr;
	char *optr;
        extern char *optarg;
        extern int optind;

	char c;
	int  ret, n = 0, i = 0;

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


#ifdef FBSD_DATABASE
        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_HASH, &openinfo);

	while(fgets(line,256,ifd)) {
		inKey.data = strtok(line," ");
		inKey.size = strlen(inKey.data)+1;
		inVal.data = strtok(NULL,"\n");
		inVal.size = strlen(inVal.data)+1;
		n++;
		ret = (db->put)(db,&inKey,&inVal,R_NOOVERWRITE);
		switch(ret) {
			case RET_SUCCESS:
				i++;
				break;
			case RET_SPECIAL:
				fprintf(stderr,"%s:%i: warning: Duplicate entry. Ignored.\n",iptr,n);
				break;
			default:
				(void)(db->close)(db);
				fprintf(stderr,"%s:%i: error: storing error\n",iptr,n);
				fprintf(stderr,"%d words processed.\n",i);
				exit(1);
		}
	}

	(void)(db->close)(db);
#else
        db = dbm_open(optr,O_RDWR|O_CREAT, 0000644);

	while(fgets(line,256,ifd)) {
		Key.dptr = strtok(line," ");
		Key.dsize = strlen(Key.dptr)+1;
		Val.dptr = strtok(NULL,"\n");
		Val.dsize = strlen(Val.dptr)+1;
		n++;
		ret = dbm_store(db,Key,Val,DBM_INSERT);
		switch(ret) {
			case 0:
				i++;
				break;
			case 1:
				fprintf(stderr,"%s:%i: warning: Duplicate entry. Ignored.\n",iptr,n);
				break;
			default:
				dbm_close(db);
				fprintf(stderr,"%s:%i: error: storing error\n",iptr,n);
				fprintf(stderr,"%d words processed.\n",i);
				exit(1);
		}
	}

	dbm_close(db);
#endif

	fprintf(stderr,"%d words processed.\n",i);

	return(0);
}
