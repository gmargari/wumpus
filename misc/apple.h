// Special defitions to make Wumpus compile under MacOS X.

#ifndef __MISC__APPLE_H

#include <stdint.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>

#define O_LARGEFILE 0  // O_LARGEFILE is undefined in MacOS
#define O_DIRECT 0     // O_DIRECT is undefined in MacOS

#define MSG_DONTWAIT 0x80

// Memalign is not supported. Fortunately, O_DIRECT is not supported, either,
// so we do an ordinary malloc instead of memalign.
static inline int posix_memalign(void** memptr, size_t alignment, size_t size) {
	*memptr = malloc(size);
	return 0;
}


#endif
