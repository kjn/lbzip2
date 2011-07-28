/*-
  Copyright (C) 2011 Mikolaj Izdebski.  All rights reserved.

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* The YB_VERSION macro serves as both include guardian and the library
   version number.  The low 8 bits form the minor version number and
   everything above that forms the major verson (for example YB_VERSION
   of 0x412 would mean version 4.18 of the library).
*/
#ifndef YB_VERSION
#define YB_VERSION 1

#include <stddef.h>


typedef struct YBibs_s YBibs_t;  /* IBS (input bit-stream) */
typedef struct YBobs_s YBobs_t;  /* OBS (output bit-stream) */
typedef struct YBdec_s YBdec_t;  /* decoder */
typedef struct YBenc_s YBenc_t;  /* encoder */
typedef unsigned long  YBcrc_t;  /* CRC32 */


#define YB_DEFAULT_SHALLOW 16u
#define YB_DEFAULT_PREFIX  16u
#define YB_HEADER_SIZE     4u
#define YB_TRAILER_SIZE    10u


#define YB_OK          0
#define YB_UNDERFLOW (-1)
#define YB_OVERFLOW  (-2)
#define YB_DONE      (-3)
#define YB_CANCELED  (-4)


#define YB_ERR_MAGIC    (-101)  /* invalid stream header magic */
#define YB_ERR_HEADER   (-102)  /* invalid block header magic */
#define YB_ERR_BITMAP   (-103)  /* empty source alphabet */
#define YB_ERR_TREES    (-104)  /* bad number of prefix trees */
#define YB_ERR_GROUPS   (-105)  /* no coding groups */
#define YB_ERR_SELECTOR (-106)  /* invalid selector */
#define YB_ERR_DELTA    (-107)  /* invalid delta code */
#define YB_ERR_PREFIX   (-108)  /* invalid prefix code */
#define YB_ERR_INCOMPLT (-109)  /* incomplete prefix code */
#define YB_ERR_EMPTY    (-110)  /* empty block */
#define YB_ERR_UNTERM   (-111)  /* unterminated block */
#define YB_ERR_RUNLEN   (-112)  /* missing run length */
#define YB_ERR_BLKCRC   (-113)  /* block CRC mismatch */
#define YB_ERR_STRMCRC  (-114)  /* stream CRC mismatch */


/* Allocate and initialise an YBibs_t structure, and return a pointer to it.
   If there is not enough memory, return NULL.
*/
YBibs_t *YBibs_init(void);


/* Allocate and initialise an YBdec_t structure, and return a pointer to it.
   If there is not enough memory, return NULL.
*/
YBdec_t *YBdec_init(void);


/* Decode a single block from the memory buffer.

   Return values:
   YB_OK if a block was successfully decoded
   YB_UNDERFLOW - all bytes were consumed and need more bytes to finish
   decoding the current block
   YB_DONE - if there are no more blocks in this ibs (no bytes are consumed
   in this case)
   otherwise - data format error
*/
int YBibs_retrieve(YBibs_t *ibs, YBdec_t *dec,
                   const void *buf, size_t *buf_sz);


/* return 0 on ok, < 0 on error */
int YBdec_work(YBdec_t *d);


/* return < -1 on error, -1 on underflow, >= 0 on successfull emit
   (retval is number of bytes remaining in buffer) */
int YBdec_emit(YBdec_t *d, void *buf, size_t *buf_sz);


void YBdec_join(YBdec_t *d);


/* Compare calculated and stored combined CRCs of this YBibs_t structure.
   It may not be called be called before all blocks related to this YBibs_t
   were joined. Return YB_OK if no error was detected, YB_CANCELED if any
   of joined blocks wasn't decompressed correctly, YB_CRC_ERROR if combined
   CRC is invalid.
 */
int YBibs_check(YBibs_t *ibs);


/* Destroy the YBdec_t structure and release any resources associated to it.
   It must be called after any blocks related to this dec were released.
   After this operation, the YBdec_t structure may be no longer used.
*/
void YBdec_destroy(YBdec_t *dec);


/* Destroy the YBibs_t structure and release any resources associated to it.
   It must be called after any blocks related to this ibs were released.
   After this operation, the YBibs_t structure may be no longer used.
*/
void YBibs_destroy(YBibs_t *ibs);





YBobs_t *YBobs_init(unsigned long max_block_size, void *buf);
YBenc_t *YBenc_init(unsigned long max_block_size,
                    unsigned shallow_factor,
                    unsigned prefix_factor);

/* -1 on underflow, >= 0 on successfull collect
   (retval is number of bytes remaining in buffer) */
int YBenc_collect(YBenc_t *e, const void *buf, size_t *buf_sz);

/* return compressed size */
size_t YBenc_work(YBenc_t *e, YBcrc_t *crc);

/* Encode data into the given buffer. */
void YBenc_transmit(YBenc_t *e, void *buf);

void YBobs_join(YBobs_t *obs, const YBcrc_t *crc);

/* Write stream trailer into given buffer.  Buffer must have enough space. */
void YBobs_finish(YBobs_t *obs, void *buf);

/* Destroy the YBenc_t structure and release any resources associated to it.
   It must be called after any blocks related to this enc were released.
   After this operation, the YBenc_t structure may be no longer used.
*/
void YBenc_destroy(YBenc_t *enc);

/* Destroy the YBobs_t structure and release any resources associated to it.
   It must be called after any blocks related to this obs were released.
   After this operation, the YBobs_t structure may be no longer used.
*/
void YBobs_destroy(YBobs_t *obs);


#endif /* YB_VERSION */


/* Local Variables: */
/* mode: C */
/* c-file-style: "bsd" */
/* c-basic-offset: 2 */
/* indent-tabs-mode: nil */
/* End: */
