/*-
  divsufsort.h -- header for cyclic version of divsufsort

  Copyright (c) 2012 Yuta Mori All Rights Reserved.

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation
  files (the "Software"), to deal in the Software without
  restriction, including without limitation the rights to use,
  copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following
  conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
  OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef DIVSUFSORT_H
#define DIVSUFSORT_H

#include <inttypes.h>


/*- Prototypes -*/

/**
 * Constructs the burrows-wheeler transformed string of a given string.
 * @param T[0..n-1] The input string. T[n] must exist and it's overwritten.
 * @param SA[0..n-1] The output string. SA[n] must exist.
 * @param n The length of the given string.
 * @return The primary index.
 */
int32_t
cyclic_divbwt(uint8_t *T, int32_t *SA, int32_t n);


#endif
