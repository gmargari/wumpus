#include <arpa/inet.h>
#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <resolv.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <netdb.h>


static const int PORT_NUMBER = 1234;


void child() {
	printf("Child: sleeping for 1 second\n");
	sleep(1);
	int s = socket(PF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_aton("127.0.0.1", &addr.sin_addr);
	addr.sin_port = htons(PORT_NUMBER);
	if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
		perror("Client");
		exit(1);
	}
	printf("Child: connection established\n");
	printf("Child: sleeping for 1 second\n");
	sleep(1);
	if (shutdown(s, SHUT_RDWR) != 0)
		perror("Child");
//	printf("Child: shutdown\n");
//	printf("Child: sleeping for 10 seconds\n");
//	sleep(10);
	if (close(s) != 0)
		perror("Child");
	printf("Child: connection closed\n");
	printf("Child: sleeping for 10 seconds\n");
	sleep(10);
	exit(0);
}


static void pollThisFD(int fd) {
	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLOUT;
	printf("Parent: poll returns %d\n", poll(&pfd, 1, -1));
	if (pfd.revents & POLLIN)
		printf("  POLLIN\n");
	if (pfd.revents & POLLOUT)
		printf("  POLLOUT\n");
	if (pfd.revents & POLLERR)
		printf("  POLLERR\n");
	if (pfd.revents & POLLHUP)
		printf("  POLLHUP\n");
	printf("  (revent mask: %d)\n", (int)pfd.revents);
}


void parent() {
	struct protoent *protocolIdentifier;
	protocolIdentifier = getprotobyname("tcp");
	int listenSocket = socket(PF_INET, SOCK_STREAM, protocolIdentifier->p_proto);
	if (listenSocket < 0) {
		perror("Parent");
		exit(1);
	}
	int one = 1;
	setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	struct sockaddr_in listenAddress;
	listenAddress.sin_family = AF_INET;
	listenAddress.sin_port = htons(PORT_NUMBER);
	listenAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(listenSocket, (struct sockaddr*)&listenAddress, sizeof(listenAddress)) < 0) {
		perror("Parent");
		exit(1);
	}
	if (listen(listenSocket, 8) < 0) {
		perror("Parent");
		close(listenSocket);
		exit(1);
	}

	int s;
	struct sockaddr connectionAddress;
	socklen_t addressLength = sizeof(sockaddr);
	printf("Parent: waiting for incoming connections\n");
	while ((s = accept(listenSocket, &connectionAddress, &addressLength)) >= 0) {
		printf("Parent: connection accepted: %d\n", s);
		printf("Parent: sleeping for 3 seconds\n");
		sleep(3);
		printf("----------------------------------------\n");
		printf("sequence: poll, write, poll\n");
		pollThisFD(s);
		int writeResult = write(s, &s, sizeof(s));
		printf("Parent: write returns %d\n", writeResult);
		pollThisFD(s);
		close(s);
	}
}


int main() {
	pid_t pid = fork();
	switch (pid) {
		case -1:
			return 1;
		case 0:
			child();
			return 0;
		default:
			parent();
			return 0;
	}
}


