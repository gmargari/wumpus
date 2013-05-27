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
 * This file redefines the assert macro so that the program can stop execution
 * whenever an assertion fails. After the program has stopped, we can use gdb
 * to attach to the program and see what is wrong.
 *
 * author: Stefan Buettcher
 * created: 2005-07-30
 * changed: 2005-07-30
 **/


#ifndef __MISC__ASSERT_H
#define __MISC__ASSERT_H


#include "../config/config.h"
#include "logging.h"


#if ASSERT_DEBUG

	#undef assert

	#define assert(EXPR) assert5(EXPR, __STRING(EXPR), __PRETTY_FUNCTION__, __FILE__, __LINE__)

	static void assert5(int expr, const char *exprString, const char *function, const char *file, int line) {
		if (!(expr)) {
			char dummy[1024];
			fprintf(stderr, "Assertion '%s' failed in %s (%s:%d).\n",
					exprString, function, file, line);
			fprintf(stderr, "Press RETURN to continue.\n");
			fgets(dummy, sizeof(dummy), stdin);
		}
	}

#else

	#include <assert.h>

#endif
			

#endif


