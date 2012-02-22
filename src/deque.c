/*-
  deque.c -- double-ended queue implementation

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>  /* assert() */
#include <stdlib.h>  /* free() */
#include <string.h>  /* memcpy() */

#include "xalloc.h"  /* xmalloc() */

#include "deque.h"   /* struct deque */


#define INITIAL_SIZE 32


void
deque_init(
  struct deque *q)
{
  q->mask = INITIAL_SIZE - 1;
  q->left = q->right = 0;
  q->root = xmalloc(INITIAL_SIZE * sizeof(void *));
}


void
deque_uninit(
  struct deque *q)
{
  assert(deque_empty(q));

  free(q->root);
}


int
deque_empty(
  struct deque *q)
{
  return q->left == q->right;
}


size_t
deque_size(
  struct deque *q)
{
  return (q->right - q->left) & q->mask;
}


void *
deque_peek(
  struct deque *q,
  size_t i)
{
  assert(i < deque_size(q));

  return q->root[(q->left + i) & q->mask];
}


void *
deque_poke(
  struct deque *q,
  size_t i,
  void *el)
{
  void *old;

  assert(i < deque_size(q));

  i = (q->left + i) & q->mask;
  old = q->root[i];
  q->root[i] = el;

  return old;
}


static void
deque_expand(
  struct deque *q)
{
  void **from;
  size_t count, size;

  if (q->left != q->right)
    return;

  size = q->mask + 1;
  q->mask = (q->mask << 1) + 1;
  q->root = xrealloc(q->root, (q->mask + 1) * sizeof(void *));

  if (q->left < size >> 1) {
    from = q->root;
    count = q->left;
    q->right += size;
  } else {
    from = q->root + q->left;
    count = size - q->left;
    q->left += size;
  }

  memcpy(from + size, from, count * sizeof(void *));
}


void
deque_unshift(
  struct deque *q,
  void *el)
{
  q->left = (q->left - 1) & q->mask;
  q->root[q->left] = el;
  deque_expand(q);
}


void *
deque_shift(
  struct deque *q)
{
  void *el;

  assert(!deque_empty(q));

  el = q->root[q->left];
  q->left = (q->left + 1) & q->mask;

  return el;
}


void
deque_push(
  struct deque *q,
  void *el)
{
  q->root[q->right] = el;
  q->right = (q->right + 1) & q->mask;
  deque_expand(q);
}


void *
deque_pop(
  struct deque *q)
{
  assert(!deque_empty(q));

  q->right = (q->right - 1) & q->mask;
  return q->root[q->right];
}
