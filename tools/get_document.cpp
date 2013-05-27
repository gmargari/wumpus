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
 * This is a frontend program for Wumpus. It connects to a server running on
 * the given host at the given port and extracts the text associated with the
 * given document ID.
 *
 * Command-line parameters:
 *
 *   ./get_document HOSTNAME PORT DOCNO > OUTPUT_FILE
 *
 * author: Stefan Buettcher
 * created: 2006-09-15
 * changed: 2006-09-15
 **/


#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <resolv.h>
#include <string.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/types.h>
#include <unistd.h>


int serverConnection;
char hostName[1024];
int portNumber;
char docno[1024];
FILE *conn;


/** Prints general usage and terminates the program. **/
void printUsage() {
	fprintf(stderr, "Usage: ./get_document HOSTNAME PORT DOCNO > OUTPUT_FILE\n\n");
	exit(1);
} // end of printUsage()


/** Opens TCP connections to the Wumpus server specified by hostName and portNumber. **/
void connectToServer() {
	char dummy[1024];
	serverConnection = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_aton(hostName, &addr.sin_addr);
	addr.sin_port = htons(portNumber);
	if (connect(serverConnection, (sockaddr*)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "Unable to connect to server: %s\n", hostName);
		exit(1);
	}
	conn = fdopen(serverConnection, "a+");
	assert(conn != NULL);
	fgets(dummy, sizeof(dummy), conn);
} // end of connectToServer()


void closeServerConnection() {
	fclose(conn);
} // end of closeServerConnection()


int main(int argc, char **argv) {
	if (argc != 4)
		printUsage();
	strcpy(hostName, argv[1]);
	portNumber = atoi(argv[2]);
	strcpy(docno, argv[3]);
	connectToServer();
	fprintf(conn, "$DOCS>\"<docno>%s</docno>\"\n", docno);
	fflush(conn);
	char offsets[1024], dummy[1024];
	fgets(offsets, sizeof(offsets), conn);
	if (offsets[0] == '@') {
		fprintf(stderr, offsets);
		return 1;
	}
	fgets(dummy, sizeof(dummy), conn);
	assert(dummy[0] = '@');
	double start, end;
	int status = sscanf(offsets, "%lf%lf", &start, &end);
	fprintf(conn, "@get %.0lf %.0lf\n", start, end);
	fflush(conn);
	char line[1024 * 1024];
	while (fgets(line, sizeof(line), conn) != NULL) {
		if (line[0] == '@') {
			if (line[1] != '@')
				break;
			else
				printf("%s", &line[1]);
		}
		else
			printf("%s", line);
	}
	closeServerConnection();
	return 0;
} // end of main()


