#include <assert.h>    /* assert() */
#include <inttypes.h>  /* uint32_t */

#include "yambi.h"


/* Memoty allocation stuff, provided by lbzip2. */
void *xalloc(size_t size);
extern void (*freef)(void *ptr);
#define xfree(x) ((*freef)(x))


typedef uint8_t  Byte;
typedef uint16_t Short;
typedef uint32_t Int;
typedef int32_t  SInt;
typedef uint64_t Long;
typedef int64_t  SLong;


/* Explicit static branch prediction to help compiler generating faster code.
   This not only works for GNU C, but also Clang and possibly others. */
#ifdef __GNUC__
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
