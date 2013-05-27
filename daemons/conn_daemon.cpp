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
 * Implementation of the ConnDaemon class.
 *
 * author: Stefan Buettcher
 * created: 2004-11-26
 * changed: 2005-11-30
 **/


#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "conn_daemon.h"
#include "multitext_connection.h"
#include "../misc/all.h"
#include "../misc/logging.h"
#include "../misc/configurator.h"


const char * ConnDaemon::LOG_ID = "ConnDaemon";


ConnDaemon::ConnDaemon(Index *index, int listenPort) {
	this->index = index;
	this->listenPort = listenPort;
	getConfigurationInt("MAX_TCP_CONNECTIONS", &MAX_TCP_CONNECTIONS, DEFAULT_MAX_TCP_CONNECTIONS);
	if (MAX_TCP_CONNECTIONS < 0)
		MAX_TCP_CONNECTIONS = 1;
	if (MAX_TCP_CONNECTIONS > 32)
		MAX_TCP_CONNECTIONS = 32;
	for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
		activeConnections[i] = NULL;
	activeConnectionCount = 0;
	listenSocket = -1;
	init();
	status = STATUS_CREATED;
} // end of ConnDaemon(Index*, int)


ConnDaemon::~ConnDaemon() {
	if (!stopped())
		stop();
	struct protoent *protocolIdentifier;
	protocolIdentifier = getprotobyname("tcp");
	int stopper = socket(PF_INET, SOCK_STREAM, protocolIdentifier->p_proto);
	struct sockaddr_in remoteAddress;
	memset(&remoteAddress, 0, sizeof(remoteAddress));
	remoteAddress.sin_family = AF_INET;
	remoteAddress.sin_port = htons(listenPort);
	remoteAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
	connect(stopper, (struct sockaddr*)&remoteAddress, sizeof(remoteAddress));
	shutdown(stopper, SHUT_RDWR);
	close(stopper);
	while (!stopped()) {
		waitMilliSeconds(10);
	}
} // end of ~ConnDaemon()


void ConnDaemon::init() {
	// create socket for protocol type TCP
	struct protoent *protocolIdentifier;
	protocolIdentifier = getprotobyname("tcp");
	listenSocket = socket(PF_INET, SOCK_STREAM, protocolIdentifier->p_proto);
	if (listenSocket < 0) {
		log(LOG_ERROR, LOG_ID, "TCP server: Unable to create listen socket.");
		perror(NULL);
		return;
	}

	// make sure we can reuse the port without having to wait after we kill the program
	int one = 1;
	setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));

	// bind socket to port number / any address
	struct sockaddr_in listenAddress;
	listenAddress.sin_family = AF_INET;
	listenAddress.sin_port = htons(listenPort);
	listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listenSocket, (struct sockaddr*)&listenAddress, sizeof(listenAddress)) < 0) {
		log(LOG_ERROR, LOG_ID, "TCP server: Unable to bind to given port number.");
		perror(NULL);
		close(listenSocket);
		listenSocket = -1;
		return;
	}

	// initialize the listen queue to 4
	if (listen(listenSocket, 8) < 0) {
		log(LOG_ERROR, LOG_ID, "TCP Server: Call to listen unsuccessful.");
		perror(NULL);
		close(listenSocket);
		listenSocket = -1;
		return;
	}

} // end of init()


bool ConnDaemon::addActiveConnection(ClientConnection *cc) {
	bool mustReleaseLock = getLock();
	activeConnectionCount = 0;
	for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
		if (activeConnections[i] != NULL) {
			if (activeConnections[i]->stopped()) {
				delete activeConnections[i];
				activeConnections[i] = NULL;
			}
		}
		if (activeConnections[i] != NULL)
			activeConnectionCount++;
	}
	if (activeConnectionCount >= MAX_TCP_CONNECTIONS) {
		if (mustReleaseLock)
			releaseLock();
		return false;
	}
	activeConnectionCount++;
	for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
		if (activeConnections[i] == NULL) {
			activeConnections[i] = cc;
			break;
		}
	if (mustReleaseLock)
		releaseLock();
	return true;
} // end of addActiveConnection(ClientConnection*)


void ConnDaemon::killAllActiveConnections() {
	bool mustReleaseLock = getLock();
	for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
		if (activeConnections[i] != NULL)
			activeConnections[i]->closeSocket();
	for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
		if (activeConnections[i] != NULL) {
			while (!activeConnections[i]->stopped()) {
				releaseLock();
				waitMilliSeconds(20);
				getLock();
			}
			delete activeConnections[i];
			activeConnections[i] = NULL;
		}
	}
	if (mustReleaseLock)
		releaseLock();
} // end of killAllActiveConnections()


void ConnDaemon::run() {
	char buffer[256];

	// consult configuration file in order to see whether the query protocol
	// is Wumpus (default) or MultiText
	char queryProtocol[MAX_CONFIG_VALUE_LENGTH];
	bool connectionTypeIsMultiText = false;
	if (getConfigurationValue("QUERY_PROTOCOL", queryProtocol))
		if (strcasecmp(queryProtocol, "MultiText") == 0)
			connectionTypeIsMultiText = true;

	while (!stopRequested()) {
		struct sockaddr connectionAddress;
		socklen_t addressLength = sizeof(sockaddr);
		int clientConnection = accept(listenSocket, &connectionAddress, &addressLength);
		if (clientConnection < 0) {
			waitMilliSeconds(200);
			continue;
		}
		else if (stopRequested()) {
			close(clientConnection);
			break;
		}
		else {
			int one = 1;
			setsockopt(clientConnection, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(int));
			sockaddr_in *remoteAddress = (sockaddr_in*)&connectionAddress;
			int remotePort = ntohs(remoteAddress->sin_port);
			unsigned int sa = ntohl(remoteAddress->sin_addr.s_addr);
			char address1[32], address2[32];
			sprintf(address1, "%d.%d.%d.%d",
					sa / (65536 * 256), (sa / 65536) % 256, (sa / 256) % 256, sa % 256);
			sprintf(address2, "%03d.%03d.%03d.%03d",
					sa / (65536 * 256), (sa / 65536) % 256, (sa / 256) % 256, sa % 256);
			bool acceptConnection = false;
			char **allowed = getConfigurationArray("TCP_ALLOWED");
			if (allowed != NULL) {
				for (int i = 0; allowed[i] != NULL; i++) {
					if ((matchesPattern(address1, allowed[i])) || (matchesPattern(address2, allowed[i])))
						acceptConnection = true;
					free(allowed[i]);
				}
				free(allowed);
			}
			if (acceptConnection) {
				ClientConnection *cc;

				if (connectionTypeIsMultiText) {
					// open a MultiText session (for backwards compatibility)
					cc = new MultiTextConnection(index, clientConnection, Index::NOBODY);
				}
				else if (remotePort < 1024) {
					// only root can do this; so, we assume we are dealing with root here
					cc = new ClientConnection(index, clientConnection, 0);
				}
				else {
					// open a standard session with default user NOBODY
					cc = new ClientConnection(index, clientConnection, Index::NOBODY);
				}

				if (!addActiveConnection(cc)) {
					strcpy(buffer, "@1-Too many open sessions.\n");
					forced_write(clientConnection, buffer, strlen(buffer));
					shutdown(clientConnection, SHUT_RDWR);
					close(clientConnection);
					delete cc;
				}
				else {
					if (!connectionTypeIsMultiText) {
						strcpy(buffer, "@0-Connected.\n");
						forced_write(clientConnection, buffer, strlen(buffer));
					}
					cc->start();
				}
			}
			else {
				// if the remote host is not localhost, then we refuse to talk to him
				close(clientConnection);
			}
		}
	}
	close(listenSocket);
	killAllActiveConnections();
	if (getLock()) {
		status = STATUS_TERMINATED;
		releaseLock();
	}
	else
		status = STATUS_TERMINATED;
} // end of run()


