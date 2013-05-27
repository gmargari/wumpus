#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


int main(int argc, char **argv) {
	if (argc != 3) {
		fprintf(stderr, "Usage:  get_document2 BASEDIR DOCID > DOCUMENT_DATA\n\n");
		return 1;
	}
	char *BASEDIR = argv[1];
	char fileName[1024];
	sprintf(fileName, "%s/%s", BASEDIR, argv[2]);
	strcpy(strrchr(fileName, '-'), ".txt");
	strrchr(fileName, '-')[0] = '/';
	FILE *f = fopen(fileName, "r");
	assert(f != NULL);
	char *slash = strrchr(argv[2], '-');
	int off = atoi(&slash[1]);
	int status = fseek(f, off, SEEK_SET);
	assert(status == 0);
	char line[1024 * 1024];
	while (fgets(line, sizeof(line), f) != NULL) {
		printf("%s", line);
		if (strncmp(line, "</DOC>", 6) == 0)
			break;
	}
	fclose(f);
	return 0;
}


