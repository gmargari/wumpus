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


#include <limits.h>

#include "api.h"

#define MAXINT INT_MAX
#define MININT INT_MIN

#define HEAD 2*sizeof(int)

#define SIZE(p)        ((int *)(p))[-1]
#define SET_SIZE(p, n) ((int *)(p))[-1] = n
#define CAPACITY(p)    ((int *)(p))[-2]

struct among
{   int s_size;     /* number of chars in string */
    symbol * s;       /* search string */
    int substring_i;/* index to longest matching substring */
    int result;     /* result of the lookup */
    int (* function)(struct SN_env *);
};

symbol * create_s(void);
void lose_s(symbol * p);

int in_grouping(struct SN_env * z, unsigned char * s, int min, int max);
int in_grouping_b(struct SN_env * z, unsigned char * s, int min, int max);
int out_grouping(struct SN_env * z, unsigned char * s, int min, int max);
int out_grouping_b(struct SN_env * z, unsigned char * s, int min, int max);

int in_range(struct SN_env * z, int min, int max);
int in_range_b(struct SN_env * z, int min, int max);
int out_range(struct SN_env * z, int min, int max);
int out_range_b(struct SN_env * z, int min, int max);

int eq_s(struct SN_env * z, int s_size, symbol * s);
int eq_s_b(struct SN_env * z, int s_size, symbol * s);
int eq_v(struct SN_env * z, symbol * p);
int eq_v_b(struct SN_env * z, symbol * p);

int find_among(struct SN_env * z, struct among * v, int v_size);
int find_among_b(struct SN_env * z, struct among * v, int v_size);

symbol * increase_size(symbol * p, int n);
int replace_s(struct SN_env * z, int c_bra, int c_ket, int s_size, const symbol * s);
void slice_from_s(struct SN_env * z, int s_size, symbol * s);
void slice_from_v(struct SN_env * z, symbol * p);
void slice_del(struct SN_env * z);

void insert_s(struct SN_env * z, int bra, int ket, int s_size, symbol * s);
void insert_v(struct SN_env * z, int bra, int ket, symbol * p);

symbol * slice_to(struct SN_env * z, symbol * p);
symbol * assign_to(struct SN_env * z, symbol * p);

void debug(struct SN_env * z, int number, int line_count);

