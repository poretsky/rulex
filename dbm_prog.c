#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>

#ifdef BERKELEYDB4
/* Some earlier versions might have this header too */
#include <db3/db_185.h>
#else
#include <db.h>
#endif

int main(int argc, char *argv[])
{
	DBT inKey, inVal;
	DB * db;

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

        db = dbopen(optr,O_RDWR|O_CREAT, 0000644, DB_HASH, NULL);

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

	fprintf(stderr,"%d words processed.\n",i);

	return(0);
}
