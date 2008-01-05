#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef	struct {
	char site[32];
	char domain[32];
	unsigned short int online[31][24];
} DATAv1;

typedef	struct {
	char site[32];
	char domain[32];
	int online[31][24];
} DATA;

int	main(int argc, char	**argv)
{
	FILE * fp,*fp2;
	DATAv1 d1;
	DATA d2;
	int i,j;
	char oldfilename[256];
	
	if (argc !=	2) {
		printf("Usage: %s filename.\n",	argv[0]);
		return(-1);
	}
	
	snprintf(oldfilename, sizeof(oldfilename), "%s.old", argv[1]);
	if (rename(argv[1], oldfilename)) {
		printf("Can not rename data file.\n");
		return(-1);
	}

	if((fp = fopen(oldfilename,"r")) == NULL)
	{
		printf("faint\n");
		return(-1);
	}

	if((fp2	= fopen(argv[1],"w")) == NULL)
	{
		printf("faint\n");
		return(-1);
	}

	while( fread(&d1,sizeof(DATAv1),1,fp) ){
		memset(&d2,0,sizeof(DATA));
		memcpy(&d2, &d1, sizeof(DATAv1));
		for(i=0;i<31;i++) {
			for(j=0;j<24;j++) {
				if (d1.online[i][j] == 0xffff) //negative
					d2.online[i][j] = -1;
				else
					d2.online[i][j] = d1.online[i][j];
			}
		}
		
		fwrite(&d2,sizeof(DATA),1,fp2);
	}

	fclose(fp2);
	fclose(fp);
	return 0;
}
