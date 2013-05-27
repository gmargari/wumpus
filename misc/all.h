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


#ifndef __MISC__ALL_H
#define __MISC__ALL_H


#include <inttypes.h>
#include <sys/types.h>
#ifndef __SOLARIS__
#ifndef __APPLE__
#include <error.h>
#endif
#endif
#ifdef __APPLE__
#include "apple.h"
#endif
#include "assert.h"
#include "comparator.h"
#include "compression.h"
#include "configurator.h"
#include "execute.h"
#include "global.h"
#include "io.h"
#include "language.h"
#include "lockable.h"
#include "logging.h"
#include "macros.h"
#include "stringtokenizer.h"
#include "utils.h"


#endif // __MISC__ALL_H


#include "alloc.h"


