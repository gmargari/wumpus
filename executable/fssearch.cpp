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
 * author: Stefan Buettcher
 * created: 2005-05-14
 * changed: 2005-07-30
 **/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include "../query/query.h"
#include "../masterindex/masterindex.h"
#include "../misc/all.h"
#include "../misc/configurator.h"
#include "../misc/logging.h"


static void printHelp() {
	printf("Syntax: masterindex [--KEY=VALUE]\n\n");
	printf("KEY and VALUE can be arbitrary index configuration pairs. Give \"CONFIGURATION\"\n");
	printf("as KEY in order to process the configuration file given by VALUE.\n\n");
	exit(0);
} // end of printHelp()


static void processParameter(char *p) {
	if ((strcasecmp(p, "--help") == 0) || (strcasecmp(p, "-h") == 0))
		printHelp();
} // end of processParameter(char*)


static void printReadWriteStatistics() {
	long long r, w;
	char buffer[256];
	getReadWriteStatistics(&r, &w);
	sprintf(buffer, "bytes read: %lld, bytes written: %lld", r, w);
	log(LOG_DEBUG, "IO", buffer);
} // end of printReadWriteStatistics()


int main(int argc, char **argv) {
	initializeConfiguratorFromCommandLineParameters(argc, argv);
	log(LOG_DEBUG, "MasterIndex", "Starting application.");
	for (int i = 1; i < argc; i++)
		processParameter(argv[i]);
	char *workDir = "./";

	char dummy[MAX_CONFIG_VALUE_LENGTH];
	if (!getConfigurationValue("LOG_FILE", dummy)) {
		log(LOG_ERROR, "MasterIndex", "Configuration variable \"LOG_FILE\" not found. Make sure Wumpus finds your configuration file.");
		return 1;
	}

	MasterIndex *mi = new MasterIndex(workDir);
	if (!mi->startupOk) {
		log(LOG_ERROR, "MasterIndex",
				"Unable to start MasterIndex. Check whether all directory permissions and have been set correctly and whether an fschange-like notification service is running.");
		delete mi;
		return 1;
	}

	// set whole process group to low priority
	pid_t processGroup = getpgrp();
	setpriority(PRIO_PGRP, processGroup, +3);

	char line[1024];
	printf("Enter \"QUIT\" to stop the MasterIndex.\n");
	while (fgets(line, 1023, stdin) != NULL) {
		bool emptyLine = true;
		for (int i = 0; line[i] != 0; i++)
			if ((line[i] < 0) || (line[i] > ' '))
				emptyLine = false;
		if (emptyLine)
			continue;

		if (line[strlen(line) - 1] == '\n')
			line[strlen(line) - 1] = 0;
		if (strcasecmp(line, "QUIT") == 0)
			break;
		Query *q = new Query(mi, line, geteuid());
		if (q->parse()) {
			char l2[65536];
			while (q->getNextLine(l2))
			printf("%s\n", l2);
		}
		int status;
		q->getStatus(&status, line);
		printf("@%d-%s\n", status, line);
		delete q;
	}
	delete mi;
	printAllocations();
	return 0;
} // end of main()

