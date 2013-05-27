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

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main() {
	int result;
	result = sizeof(off_t);
	printf("sizeof(off_t) = %d\n", result);
	result = sizeof(mode_t);
	printf("sizeof(mode_t) = %d\n", result);
	result = sizeof(uid_t);
	printf("sizeof(uid_t) = %d\n", result);
	result = sizeof(gid_t);
	printf("sizeof(gid_t) = %d\n", result);
	return 0;
}


