/**
 * Wumpus Home Directory Search.
 *
 * This executable can be used to perform per-user home directory indexing
 * and search. Just run and be happy.
 *
 * author: Stefan Buettcher
 * created: 2005-12-20
 * changed: 2006-09-19
 **/


#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../misc/all.h"


static char *wumpusDir;
static char *homeDir;

static pid_t inotifydPID = 0;
static pid_t transformPID = 0;
static pid_t wumpusPID = 0;
static pid_t httpdPID = 0;


/**
 * Reads from stdin until closed. For every line read, a line is printed to
 * stdout. The new line basically looks like this:
 *   @update \t OLDLINE
 **/
static void fschange2wumpus() {
	char line[16384];
	signal(SIGINT, SIG_IGN);
	while (fgets(line, sizeof(line), stdin) != NULL) {
		int len = strlen(line);
		if (len == 0)
			continue;
		printf("@update\t");
		while (line[len - 1] != '\n') {
			printf("%s", line);
			if (fgets(line, sizeof(line), stdin) == NULL)
				strcpy(line, "\n");
		}
		printf("%s", line);
		fflush(stdout);
	}
	exit(0);
} // end of fschange2wumpus()


static void startInotifyd() {
	char *exe = evaluateRelativePathName(wumpusDir, "inotifyd/inotifyd");
	signal(SIGINT, SIG_IGN);
	execl(exe, exe, "-dummy", "-w", homeDir, NULL);
	perror("startInotifyd()");
	exit(1);
} // end of startInotifyd()


static void startWumpus() {
	char *exe = evaluateRelativePathName(wumpusDir, "bin/wumpus");
	char *configFile = evaluateRelativePathName(wumpusDir, "wumpus.cfg");
	configFile = concatenateStringsAndFree(duplicateString("--config="), configFile);
	char *passwordFile = evaluateRelativePathName(wumpusDir, "wumpus.passwd");
	passwordFile = concatenateStringsAndFree(duplicateString("PASSWORD_FILE="), passwordFile);
	char *baseDir = concatenateStrings("BASE_DIRECTORY=", homeDir);
	char *databaseDir = evaluateRelativePathName(homeDir, ".wumpusdb");
	databaseDir = concatenateStringsAndFree(duplicateString("DIRECTORY="), databaseDir);
	int fd = open("/dev/null", O_RDWR);
	if (fd > 0)
		dup2(fd, fileno(stdout));
	signal(SIGINT, SIG_IGN);
	execl(exe, exe, configFile, passwordFile, baseDir, databaseDir,
			"MONITOR_FILESYSTEM=false", "FSCHANGE_FILE=/dev/null", NULL);
	perror("startWumpus()");
	exit(1);
} // end of startWumpus()


static void startHttpServer() {
	char *currentDir = (char*)malloc(65536);
	char *exe, *configFile, *wwwRoot;
	httpdPID = fork();
	switch (httpdPID) {
		case -1:
			perror("Unable to start HTTP server");
			exit(1);
		case 0:
			exe = "./http.pl";

			// chdir to HTTP server base directory
			chdir(wumpusDir);
			chdir("http/");
			if (getcwd(currentDir, sizeof(65535)) == NULL)
				currentDir = getcwd(NULL, 0);
			configFile = evaluateRelativePathName(currentDir, "../wumpus.cfg");
			configFile = concatenateStringsAndFree(duplicateString("--WumpusCFG="), configFile);
			wwwRoot = evaluateRelativePathName(currentDir, "www");
			wwwRoot = concatenateStringsAndFree(duplicateString("--HTTPRoot="), wwwRoot);
			signal(SIGINT, SIG_IGN);
			execl(exe, exe, configFile, wwwRoot, NULL);
			perror("startHttpServer()");
			exit(1);
	}
} // end of startHttpServer()


static void sigintHandler(int message) {
	if (inotifydPID == 0) {
		signal(SIGINT, sigintHandler);
	}
	else {
		kill(inotifydPID, SIGKILL);
		waitpid(inotifydPID, NULL, 0);
		inotifydPID = 0;
	}
} // end of sigintHandler(int)


/**
 * Starts two new processes: Wumpus and inotifyd. Creates a pipe that is used
 * to pump the inotifyd output into Wumpus's stdin.
 **/
static void startWumpusAndInotifyd() {
	// create pipe for inotifyd -> transformer communication
	int inotifydFDs[2];
	if (pipe(inotifydFDs) != 0) {
		perror("Unable to create inotifyd->Wumpus pipe");
		exit(1);
	}

	// create inotifyd process
	inotifydPID = fork();
	switch (inotifydPID) {
		case -1:
			perror("Unable to create inotifyd process");
			exit(1);
		case 0:
			close(inotifydFDs[0]);
			fclose(stdin);
			dup2(inotifydFDs[1], fileno(stdout));
			startInotifyd();
			exit(1);
	}

	// create pipe for transformer -> Wumpus communication
	int wumpusFDs[2];
	if (pipe(wumpusFDs) != 0) {
		perror("Unable to create inotifyd->Wumpus pipe");
		kill(inotifydPID, SIGKILL);
		exit(1);
	}

	// create transformer process
	transformPID = fork();
	switch (transformPID) {
		case -1:
			perror("Unable to create inotifyd process");
			kill(inotifydPID, SIGKILL);
			exit(1);
		case 0:
			close(wumpusFDs[0]);
			close(inotifydFDs[1]);
			dup2(inotifydFDs[0], fileno(stdin));
			dup2(wumpusFDs[1], fileno(stdout));
			fschange2wumpus();
			exit(0);
	}

	close(inotifydFDs[0]);
	close(inotifydFDs[1]);

	// create Wumpus process
	wumpusPID = fork();
	switch (wumpusPID) {
		case -1:
			perror("Unable to create Wumpus process");
			kill(inotifydPID, SIGKILL);
			waitpid(inotifydPID, NULL, 0);
			kill(transformPID, SIGKILL);
			waitpid(transformPID, NULL, 0);
			exit(1);
		case 0:
			close(wumpusFDs[1]);
			dup2(wumpusFDs[0], fileno(stdin));
			startWumpus();
			exit(1);
	}

	close(wumpusFDs[0]);
	close(wumpusFDs[1]);
} // end of startWumpusAndInotifyd()


int main(int argc, char **argv) {
	// Extract Wumpus base directory from path name of executable. We need this
	// to find the config file and all the executables.
	wumpusDir = duplicateString(argv[0]);
	int len = strlen(wumpusDir);
	if (len > 0) {
		while (wumpusDir[len - 1] != '/')
			wumpusDir[--len] = 0;
		if (endsWith(wumpusDir, "bin/"))
			wumpusDir = concatenateStringsAndFree(wumpusDir, duplicateString(".."));
	}
	homeDir = getenv("HOME");

	// Now, we need to do 4 things:
	//  1. start inotifyd
	//  2. start Wumpus
	//  3. start HTTP server
	//  4. make sure the output of inotifyd is fed into Wumpus

	startWumpusAndInotifyd();

	startHttpServer();

	// install new signal handler for SIGINT
	signal(SIGINT, sigintHandler);
	signal(SIGTERM, sigintHandler);
	while (inotifydPID != 0)
		sleep(1);
	fprintf(stderr, "Wumpus is shutting down. Please wait...\n");
	
	kill(httpdPID, SIGKILL);
	waitpid(httpdPID, NULL, 0);
	waitpid(transformPID, NULL, 0);
	waitpid(wumpusPID, NULL, 0);

	return 0;
} // end of main(int, char**)


