/*
    Simple routines for working with the value_t data structure.

    Copyright (C) 2006,2007,2009 Tavis Ormandy <taviso@sdf.lonestar.org>
    Copyright (C) 2009           Eli Dupree <elidupree@charter.net>
    Copyright (C) 2009,2010      WANG Lu <coolwanglu@gmail.com>
    Copyright (C) 2015           Sebastian Parschauer <s.parschauer@gmx.de>

    This file is part of libscanmem.

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published
    by the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "value.h"
#include "scanroutines.h"
#include "scanmem.h"
#include "show_message.h"

void valtostr(const value_t *val, char *str, size_t n)
{
    char buf[128];
    int np = 0;
    int max_bytes = 0;
    bool print_as_unsigned = false;

#define FLAG_MACRO(bytes, string) (val->flags.u##bytes##b && val->flags.s##bytes##b) ? (string " ") : (val->flags.u##bytes##b) ? (string "u ") : (val->flags.s##bytes##b) ? (string "s ") : ""
    
    /* set the flags */
    np = snprintf(buf, sizeof(buf), "[%s%s%s%s%s%s]",
         FLAG_MACRO(64, "I64"),
         FLAG_MACRO(32, "I32"),
         FLAG_MACRO(16, "I16"),
         FLAG_MACRO(8,  "I8"),
         val->flags.f64b ? "F64 " : "",
         val->flags.f32b ? "F32 " : "");
    /* handle having no type at all */
    if (np <= 2) {
        show_debug("BUG: No type\n");
        goto err;
    }

         if (val->flags.u64b) { max_bytes = 8; print_as_unsigned =  true; }
    else if (val->flags.s64b) { max_bytes = 8; print_as_unsigned = false; }
    else if (val->flags.u32b) { max_bytes = 4; print_as_unsigned =  true; }
    else if (val->flags.s32b) { max_bytes = 4; print_as_unsigned = false; }
    else if (val->flags.u16b) { max_bytes = 2; print_as_unsigned =  true; }
    else if (val->flags.s16b) { max_bytes = 2; print_as_unsigned = false; }
    else if (val->flags.u8b ) { max_bytes = 1; print_as_unsigned =  true; }
    else if (val->flags.s8b ) { max_bytes = 1; print_as_unsigned = false; }

    /* find the right format, considering different integer size implementations */
         if (max_bytes == sizeof(long long)) np = snprintf(str, n, print_as_unsigned ? "%llu, %s" : "%lld, %s", print_as_unsigned ? get_ulonglong(val) : get_slonglong(val), buf);
    else if (max_bytes == sizeof(long))      np = snprintf(str, n, print_as_unsigned ? "%lu, %s"  : "%ld, %s" , print_as_unsigned ? get_ulong(val) : get_slong(val), buf);
    else if (max_bytes == sizeof(int))       np = snprintf(str, n, print_as_unsigned ? "%u, %s"   : "%d, %s"  , print_as_unsigned ? get_uint(val) : get_sint(val), buf);
    else if (max_bytes == sizeof(short))     np = snprintf(str, n, print_as_unsigned ? "%hu, %s"  : "%hd, %s" , print_as_unsigned ? get_ushort(val) : get_sshort(val), buf);
    else if (max_bytes == sizeof(char))      np = snprintf(str, n, print_as_unsigned ? "%hhu, %s" : "%hhd, %s", print_as_unsigned ? get_uchar(val) : get_schar(val), buf);
    else if (val->flags.f64b) np = snprintf(str, n, "%lg, %s", get_f64b(val), buf);
    else if (val->flags.f32b) np = snprintf(str, n, "%g, %s", get_f32b(val), buf);
    else {
        show_debug("BUG: No formatting found\n");
        goto err;
    }
    if (np <= 0 || np >= (n - 1))
        goto err;

    return;
err:
    /* always print a value and a type to not crash front-ends */
    strncpy(str, "unknown, [unknown]", n);
}

void valcpy(value_t * dst, const value_t * src)
{
    memcpy(dst, src, sizeof(value_t));
    return;
}

/* dst.flags must be set beforehand */
void uservalue2value(value_t *dst, const uservalue_t *src)
{
    if(dst->flags.u8b) set_u8b(dst, get_u8b(src));
    if(dst->flags.s8b) set_s8b(dst, get_s8b(src));
    if(dst->flags.u16b) set_u16b(dst, get_u16b(src));
    if(dst->flags.s16b) set_s16b(dst, get_s16b(src));
    if(dst->flags.u32b) set_u32b(dst, get_u32b(src));
    if(dst->flags.s32b) set_s32b(dst, get_s32b(src));
    if(dst->flags.u64b) set_u64b(dst, get_u64b(src));
    if(dst->flags.s64b) set_s64b(dst, get_s64b(src));
    /* I guess integer and float cannot be matched together */
    if(dst->flags.f32b) set_f32b(dst, get_f32b(src));
    if(dst->flags.f64b) set_f64b(dst, get_f64b(src));
}

/* parse bytearray, it will allocate the arrays itself, then needs to be free'd by `free_uservalue()` */
bool parse_uservalue_bytearray(char *const *argv, unsigned argc, uservalue_t *val)
{
    int i,j;
    uint8_t *bytes_array = malloc(argc*sizeof(uint8_t));
    wildcard_t *wildcards_array = malloc(argc*sizeof(wildcard_t));

    if (bytes_array == NULL || wildcards_array == NULL)
    {
        show_error("memory allocation for bytearray failed.\n");
        goto err;
    }

    const char *cur_str;
    char *endptr;

    for(i = 0; i < argc; ++i)
    {
        /* get current string */
        cur_str = argv[i];
        /* test its length */
        for(j = 0; (j < 3) && (cur_str[j]); ++j) {}
        if (j != 2) /* length is not 2 */
            goto err;

        if (strcmp(cur_str, "??") == 0)
        {
            wildcards_array[i] = WILDCARD;
            bytes_array[i] = 0x00;
        }
        else
        {
            /* parse as hex integer */
            uint8_t cur_byte = (uint8_t)strtoul(cur_str, &endptr, 16);
            if (*endptr != '\0')
                goto err;

            wildcards_array[i] = FIXED;
            bytes_array[i] = cur_byte;
        }
    }

    /* everything is ok */
    val->bytearray_value = bytes_array;
    val->wildcard_value = wildcards_array;
    val->flags.length = argc;
    return true;

err:
    if (bytes_array) free(bytes_array);
    if (wildcards_array) free(wildcards_array);
    zero_uservalue(val);
    return false;
}

bool parse_uservalue_number(const char *nptr, uservalue_t * val)
{
    /* TODO multiple rounding method */
    if (parse_uservalue_int(nptr, val))
    {
        val->flags.f32b = val->flags.f64b = 1;
        val->float32_value = (float) val->int64_value;
        val->float64_value = (double) val->int64_value;   
        return true;
    }
    else if(parse_uservalue_float(nptr, val))
    {
        double num = val->float64_value;
        if (num >=        (double)(0) && num < (double)(1LL<< 8)) { val->flags.u8b  = 1; set_u8b(val, (uint8_t)num); }
        if (num >= (double)-(1LL<< 7) && num < (double)(1LL<< 7)) { val->flags.s8b  = 1; set_s8b(val, (int8_t)num); }
        if (num >=        (double)(0) && num < (double)(1LL<<16)) { val->flags.u16b = 1; set_u16b(val, (uint16_t)num); }
        if (num >= (double)-(1LL<<15) && num < (double)(1LL<<15)) { val->flags.s16b = 1; set_s16b(val, (int16_t)num); }
        if (num >=        (double)(0) && num < (double)(1LL<<32)) { val->flags.u32b = 1; set_u32b(val, (uint32_t)num); }
        if (num >= (double)-(1LL<<31) && num < (double)(1LL<<31)) { val->flags.s32b = 1; set_s32b(val, (int32_t)num); }
        if (           (double)(true) &&          (double)(true)) { val->flags.u64b = 1; set_u64b(val, (uint64_t)num); }
        if (           (double)(true) &&          (double)(true)) { val->flags.s64b = 1; set_s64b(val, (int64_t)num); }
        return true;
    }

    return false;
}

bool parse_uservalue_int(const char *nptr, uservalue_t * val)
{
    int64_t num;
    char *endptr;

    assert(nptr != NULL);
    assert(val != NULL);

    zero_uservalue(val);

    /* skip past any whitespace */
    while (isspace(*nptr))
        ++nptr;

    /* now parse it using strtoul */
    errno = 0;
    num = strtoll(nptr, &endptr, 0);
    if ((errno != 0) || (*endptr != '\0'))
        return false;

    /* determine correct flags */
    if (num >=        (0) && num < (1LL<< 8)) { val->flags.u8b  = 1; set_u8b(val, (uint8_t)num); }
    if (num >= -(1LL<< 7) && num < (1LL<< 7)) { val->flags.s8b  = 1; set_s8b(val, (int8_t)num); }
    if (num >=        (0) && num < (1LL<<16)) { val->flags.u16b = 1; set_u16b(val, (uint16_t)num); }
    if (num >= -(1LL<<15) && num < (1LL<<15)) { val->flags.s16b = 1; set_s16b(val, (int16_t)num); }
    if (num >=        (0) && num < (1LL<<32)) { val->flags.u32b = 1; set_u32b(val, (uint32_t)num); }
    if (num >= -(1LL<<31) && num < (1LL<<31)) { val->flags.s32b = 1; set_s32b(val, (int32_t)num); }
    if (           (true) &&          (true)) { val->flags.u64b = 1; set_u64b(val, (uint64_t)num); }
    if (           (true) &&          (true)) { val->flags.s64b = 1; set_s64b(val, (int64_t)num); }

    return true;
}

bool parse_uservalue_float(const char *nptr, uservalue_t * val)
{
    double num;
    char *endptr;
    assert(nptr);
    assert(val);

    zero_uservalue(val);
    while (isspace(*nptr))
        ++nptr;

    errno = 0;
    num = strtod(nptr, &endptr);
    if ((errno != 0) || (*endptr != '\0'))
        return false;
    
    /* I'm not sure how to distinguish between float and double, but I guess it's not necessary here */
    val->flags.f32b = val->flags.f64b = 1;
    val->float32_value = (float) num;
    val->float64_value =  num;   
    return true;
}

void free_uservalue(uservalue_t *uval)
{
    /* bytearray arrays are dynamically allocated and have to be freed, strings are not */
    if (uval->bytearray_value)
        free((void*)uval->bytearray_value);
    if (uval->wildcard_value)
        free((void*)uval->wildcard_value);
}

int flags_to_max_width_in_bytes(match_flags flags)
{
    switch(sm_globals.options.scan_data_type)
    {
        case BYTEARRAY:
        case STRING:
            return flags.length;
            break;
        default: /* numbers */
                 if (flags.u64b || flags.s64b || flags.f64b) return 8;
            else if (flags.u32b || flags.s32b || flags.f32b) return 4;
            else if (flags.u16b || flags.s16b              ) return 2;
            else if (flags.u8b  || flags.s8b               ) return 1;
            else    /* it can't be a variable of any size */ return 0;
            break;
    }
}

int val_max_width_in_bytes(const value_t *val)
{
	return flags_to_max_width_in_bytes(val->flags);
}

#define DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(type, typename, signedness_letter) \
type get_##signedness_letter##typename (const value_t* val) \
{ \
	     if (sizeof(type) <= 1) return (type)get_##signedness_letter##8b(val); \
	else if (sizeof(type) <= 2) return (type)get_##signedness_letter##16b(val); \
	else if (sizeof(type) <= 4) return (type)get_##signedness_letter##32b(val); \
	else if (sizeof(type) <= 8) return (type)get_##signedness_letter##64b(val); \
	else assert(false); \
}
#define DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(type, typename) \
	DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(unsigned type, typename, u) \
	DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTION(signed type, typename, s)

DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(char, char)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(short, short)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(int, int)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long, long)
DEFINE_GET_BY_SYSTEM_DEPENDENT_TYPE_FUNCTIONS(long long, longlong)
