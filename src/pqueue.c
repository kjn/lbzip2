/*-
  Copyright (C) 2011 Mikolaj Izdebski

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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <assert.h>  /* assert() */
#include <stdlib.h>  /* free() */

#include "xalloc.h"  /* xmalloc() */

#include "pqueue.h"


/* Parent and left child indices. */
#define parent(i) (((i)-1)/2)
#define left(i)   ((i)*2+1)


void
pqueue_init(
  struct pqueue *pq,
  int (*cmp)(const void *lhs, const void *rhs))
{
  pq->size = 0;
  pq->alloc = 32;
  pq->root = xmalloc(pq->alloc * sizeof(void *));
  pq->cmp = cmp;
}


void
pqueue_uninit(
  struct pqueue *pq)
{
  assert(pqueue_empty(pq));

  free(pq->root);
}


void
pqueue_insert(
  struct pqueue *pq,
  void *el)
{
  size_t j;

  if (pq->size == pq->alloc) {
    pq->alloc <<= 1;
    pq->root = xrealloc(pq->root, pq->alloc * sizeof(void *));
  }

  j = pq->size++;

  while (j > 0 && pq->cmp(pq->root[parent(j)], el) > 0) {
    pq->root[j] = pq->root[parent(j)];
    j = parent(j);
  }

  pq->root[j] = el;
}


void
pqueue_pop(
  struct pqueue *pq)
{
  size_t j;
  void *el;

  assert(!pqueue_empty(pq));

  el = pq->root[--pq->size];

  j = 0;
  while (left(j) < pq->size) {
    size_t child = left(j);
    if (child+1 < pq->size && pq->cmp(pq->root[child+1], pq->root[child]) < 0)
      child++;
    if (pq->cmp(el, pq->root[child]) <= 0)
      break;
    pq->root[j] = pq->root[child];
    j = child;
  }

  pq->root[j] = el;
}
