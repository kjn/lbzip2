/*-
  encode.c -- yambi internal header

  Copyright (C) 2011, 2012 Mikolaj Izdebski

  This file is part of lbzip2.

  lbzip2 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  lbzip2 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with lbzip2.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <arpa/inet.h> /* htonl() */
#include <assert.h>    /* assert() */
#include <inttypes.h>  /* uint32_t */

#include "yambi.h"


/* Type definitions first. */
typedef uint8_t  Byte;
typedef uint16_t Short;
typedef uint32_t Int;
typedef int32_t  SInt;
typedef uint64_t Long;
typedef int64_t  SLong;


/* These macros load (peek) or store (poke) 8, 16 or 32-bit values from/to
   memory pointed by p.  The pointer p must be properly aligned. The casts
   to void * are only to prevent compiler warnings about unaligned access.
*/
#define peekb(p)      (*(const Byte  *)(const void *)(p))
#define peeks(p) ntohs(*(const Short *)(const void *)(p))
#define peekl(p) ntohl(*(const Int   *)(const void *)(p))
#define pokeb(p,v) ((void)(*(Byte  *)(void *)(p) =      (v)))
#define pokes(p,v) ((void)(*(Short *)(void *)(p) = htons(v)))
#define pokel(p,v) ((void)(*(Int   *)(void *)(p) = htonl(v)))


/* Other generic macros. */
#define min(x,y) ((x) < (y) ? (x) : (y))
#define max(x,y) ((x) < (y) ? (y) : (x))


/* Check GCC version.  This not only works for GNU C, but also Clang
   and possibly others.  If a particular compiler defines __GNUC__
   but it's not GCC compatible then it's that compilers problem. */
#define GNUC_VERSION (10000 * (__GNUC__ + 0) + 100 * (__GNUC_MINOR__ + 0) + \
                      (__GNUC_PATCHLEVEL__ + 0))

/* Explicit static branch prediction to help compiler generating faster code.
*/
#if GNUC_VERSION >= 30004
# define likely(x)   __builtin_expect((x), 1)
# define unlikely(x) __builtin_expect((x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif


/* Minimal and maximal alphabet size used in prefix coding.
   We always have 2 RLE symbols, 0-255 MTF values and 1 EOF symbol. */
#define MIN_ALPHA_SIZE (2+0+1)
#define MAX_ALPHA_SIZE (2+255+1)

#define MIN_TREES 2
#define MAX_TREES 6
#define GROUP_SIZE 50
#define MIN_CODE_LENGTH 1  /* implied by MIN_ALPHA_SIZE > 1 */
#define MAX_CODE_LENGTH 20
#define MAX_BLOCK_SIZE 900000
#define MAX_GROUPS ((MAX_BLOCK_SIZE + GROUP_SIZE - 1) / GROUP_SIZE)


extern Int YB_crc_table[256];
