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


#ifndef __MISC__MACROS_H
#define __MISC__MACROS_H


#include <math.h>


// define some macros depending on the existence of some platform-specific macros (sigh...)

#ifndef isNAN
	#ifdef isnan
		#define isNAN(x) isnan(x)
	#else
		#include <nan.h>
		#define isNAN(x) ((IsNANorINF(x)) && (!IsINF(x)))
	#endif
#endif

#ifndef isINF
	#ifdef isinf
		#define isINF(x) isinf(x)
	#else
		#include <nan.h>
		#define isINF(x) ((IsNANorINF(x)) && (IsINF(x)))
	#endif
#endif

#ifndef LROUND
	#define LROUND(x) ((long)(x + 0.5))
#endif

#ifndef __STRING
	#define __STRING(x) #x
#endif

#ifdef __SOLARIS__
	// If building for Solaris, we assume that we will run on a SUN, which will
	// give us a SIGBUS for every unaligned memory access. Thus, we will have to
	// pad some data in the on-disk index for everything to be word-aligned.
	#define INDEX_MUST_BE_WORD_ALIGNED 1
#else
	#define INDEX_MUST_BE_WORD_ALIGNED 0
#endif


#endif // __MISC__MACROS_H


