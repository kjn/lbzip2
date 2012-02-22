/*-
  deque.h -- double-ended queue implementation header

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

#ifndef DEQUE_H
#define DEQUE_H

#include <sys/types.h>  /* size_t */


/* Deque - double-ended queue, implemented using circular array.
   It supports the following opertaions:

   checking queue size             : empty,   size  - O(1)
   accessing arbitrary element     : peek,    poke  - O(1)
   removing element from both ends : shift,   pop   - O(1)
   adding elements at both ends    : unshift, push  - O(n), amortised O(log n)
*/
struct deque
{
  void **root;   /* cyclic array of elements */
  size_t mask;   /* number of allocated `root' elements minus 1 */
  size_t left;   /* left index (inclusive) */
  size_t right;  /* right index (exclusive) */
};


/* Initialize given deque. */
void
deque_init(struct deque *deque);

/* Unitialize given deque. It must be empty. */
void
deque_uninit(struct deque *deque);

/* Return 1 iff given deque is empty. */
int
deque_empty(struct deque *deque);

/* Return the number of elements given deque contains. */
size_t
deque_size(struct deque *deque);

/* Return the element stored at given index in given deque. Index must be less
   than the number of elements in the deque. */
void *
deque_peek(struct deque *deque, size_t index);

/* Replace the element stored at given index in given deque. Index must be less
   than the number of elements in the deque. Return the replaced element. */
void *
deque_poke(struct deque *deque, size_t index, void *element);

/* Remove and return one element from the left side of given deque.
   It must not be empty. */
void *
deque_shift(struct deque *deque);

/* Add one element from the left side of given deque. */
void
deque_unshift(struct deque *deque, void *element);

/* Add one element from the right side of given deque. */
void
deque_push(struct deque *deque, void *element);

/* Remove and return one element from the right side of given deque.
   It must not be empty. */
void *
deque_pop(struct deque *deque);


#endif
