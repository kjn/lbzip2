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

#ifndef PQUEUE_H
#define PQUEUE_H

#include <sys/types.h>  /* size_t */


struct pqueue
{
  void **root;
  size_t size;
  size_t alloc;
  int (*cmp)(const void *lhs, const void *rhs);
};


void
pqueue_init(struct pqueue *pqueue,
    int (*cmp)(const void *lhs, const void *rhs));

void
pqueue_uninit(struct pqueue *pqueue);

#define pqueue_empty(pq) ((pq)->size == 0)

#define pqueue_peek(pq)  (*(pq)->root)

void
pqueue_insert(struct pqueue *pqueue, void *element);

void
pqueue_pop(struct pqueue *pqueue);


#endif
