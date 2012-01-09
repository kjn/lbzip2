/*
 * cyclic-divsufsort-lite.h for cyclic-divsufsort-lite
 * Copyright (c) 2012 Yuta Mori All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CYCLIC_DIVSUFSORT_LITE_H
#define _CYCLIC_DIVSUFSORT_LITE_H 1

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*- setting -*/
#define SS_INSERTIONSORT_THRESHOLD 8
#define SS_BLOCKSIZE 1024
#define UINT8_MAX 255
#define ALPHABET_SIZE (UINT8_MAX + 1)
//#define ENABLE_64BIT_INDEX


/*- Datatypes -*/
#ifndef SAUCHAR_T
#define SAUCHAR_T
typedef unsigned char sauchar_t;
#endif /* SAUCHAR_T */
#ifndef SAINT_T
#define SAINT_T
typedef int saint_t;
#endif /* SAINT_T */
#ifndef SAIDX_T
#define SAIDX_T
#ifdef ENABLE_64BIT_INDEX
typedef long long saidx_t;
//typedef __int64 saidx_t;
#else
typedef int saidx_t;
#endif
#endif /* SAIDX_T */


/*- Prototypes -*/

/**
 * Constructs the cyclic suffix array of a given string.
 * @param T[0..n-1] The input string.
 * @param SA[0..n-1] The output array of suffixes.
 * @param n The length of the given string.
 * @return 0 if no error occurred, -1 or -2 otherwise.
 */
saint_t
cyclic_divsufsort(const sauchar_t *T, saidx_t *SA, saidx_t n);

/**
 * Constructs the burrows-wheeler transformed string of a given string.
 * @param T[0..n-1] The input string.
 * @param U[0..n-1] The output string. (can be T)
 * @param A[0..n-1] The temporary array. (can be NULL)
 * @param n The length of the given string.
 * @return The primary index if no error occurred, -1 or -2 otherwise.
 */
saidx_t
cyclic_divbwt(const sauchar_t *T, sauchar_t *U, saidx_t *A, saidx_t n);


#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif /* _CYCLIC_DIVSUFSORT_LITE_H */
