/**
 * author: Stefan Buettcher
 * created: 2006-08-28
 * changed: 2006-08-28
 **/


#ifndef __MISC__GLOBAL_H
#define __MISC__GLOBAL_H


class GlobalVariables {

public:

	/**
	 * This variable keeps track of fork operations performed. It is increased
	 * in the child process after a fork takes place. The value of forkCount can
	 * be used in the file I/O subsystem to detect whether a file descriptor needs
	 * to be reopened in order to avoid sharing it with other processes.
	 **/
	static int forkCount;

}; // end of class GlobalVariables


#endif


