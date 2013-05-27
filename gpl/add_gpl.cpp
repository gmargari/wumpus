/**
 * Copyright (C) 2007 Stefan Buettcher. All rights reserved.
 * This is free software with ABSOLUTELY NO WARRANTY.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA
 **/

/**
 * Adds the GPL header to the files given as command-line parameters.
 *
 * author: Stefan Buettcher
 * created: 2005-05-25
 * changed: 2005-05-25
 **/


#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define TEMP_FILE "./add_gpl.temp"

static char TEMPLATE_FILE[1024];


static bool containsGPL(char *fileName) {
	bool fsfSeen = false;
	bool gplSeen = false;
	char line[1024];
	int lineCnt = 0;
	FILE *f = fopen(fileName, "r");
	assert(f != NULL);
	while (fgets(line, 1023, f) != NULL) {
		if (strstr(line, "GNU General Public License") != NULL)
			gplSeen = true;
		if (strstr(line, "Free Software Foundation") != NULL)
			fsfSeen = true;
		if (++lineCnt > 20)
			break;
	}
	if ((fsfSeen) && (gplSeen))
		return true;
	else
		return false;
} // end of containsGPL(char*)


static void processFile(char *fileName) {
	if (containsGPL(fileName)) {
		printf("Skipping file %s\n", fileName);
		return;
	}
	else
		printf("Processing file %s\n", fileName);

	FILE *inputFile = fopen(fileName, "r");
	FILE *outputFile = fopen(TEMP_FILE, "w");
	FILE *templateFile = fopen(TEMPLATE_FILE, "r");

	char line[1024];
	while (fgets(line, 1023, templateFile) != NULL)
		fprintf(outputFile, "%s", line);
	fclose(templateFile);
	fprintf(outputFile, "\n");

	while (fgets(line, 1023, inputFile) != NULL)
		fprintf(outputFile, "%s", line);
	fclose(inputFile);

	fclose(outputFile);
	int status = rename(TEMP_FILE, fileName);
	if (status != 0)
		perror(fileName);
} // end of processFile(char*)


int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "Usage:  add_gpl HEADER_FILE SOURCE_FILE_1 .. SOURCE_FILE_N\n");
		return 1;
	}
	strcpy(TEMPLATE_FILE, argv[1]);
	for (int i = 2; i < argc; i++)
		processFile(argv[i]);
	return 0;
} // end of main(int, char**)


