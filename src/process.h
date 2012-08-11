/*-
  process.h -- priority scheduling header

  Copyright (C) 2012 Mikolaj Izdebski

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

struct task {
  const char *name;
  bool (*ready)(void);
  void (*run)(void);
};

struct process {
  const struct task *tasks;
  void (*init)(void);
  void (*uninit)(void);
  bool (*finished)(void);
  void (*on_block)(void *buffer, size_t size);
  void (*on_written)(void *buffer);
};

struct position {
  uint64_t major;
  uint64_t minor;
};

#define pqueue(T)                               \
  {                                             \
    T *restrict root;                           \
    unsigned size;                              \
  }

#define deque(T)                                \
  {                                             \
    T *restrict root;                           \
    unsigned size;                              \
    unsigned modulus;                           \
    unsigned head;                              \
  }


extern bool eof;
extern unsigned work_units;
extern unsigned in_slots;
extern unsigned out_slots;
extern unsigned total_work_units;
extern unsigned total_in_slots;
extern unsigned total_out_slots;
extern size_t in_granul;
extern size_t out_granul;

extern const struct process compression;
extern const struct process expansion;


#define pos_eq(a,b) ((a).major == (b).major &&  \
                     (a).minor == (b).minor)

#define pos_lt(a,b) ((a).major < (b).major ||                           \
                     ((a).major == (b).major && (a).minor < (b).minor))

#define pos_le(a,b) (!pos_lt(b,a))

#define pqueue_init(q,n) ((q).root = xmalloc((n) * sizeof(*(q).root)),  \
                          (void)((q).size = 0))

#define pqueue_uninit(q) (assert(empty(q)), free((q).root))

#define peek(q) (*(q).root)

#define enqueue(q,e) ((q).root[(q).size] = (e),         \
                      up_heap((q).root, (q).size++))

#define dequeue(q) (down_heap((q).root, --(q).size),    \
                    (q).root[(q).size])

#define deque_init(q,n) ((q).root = xmalloc((n) * sizeof(*(q).root)),   \
                         (q).modulus = (n),                             \
                         (void)((q).head = (q).size = 0))

#define deque_uninit(q) (assert(empty(q)), free((q).root))

#define size(q) (+(q).size)

#define empty(q) (!(q).size)

#define dq_get(q,i) (assert((i) < (q).size),                            \
                     (q).root[min((q).head + (i) + 1,                   \
                                  (q).head + (i) + 1 - (q).modulus)])

#define dq_set(q,i,e) (assert((i) < (q).size),                          \
                       (q).root[min((q).head + (i) + 1,                 \
                                    (q).head + (i) + 1 - (q).modulus)] = (e))

#define shift(q) (assert(!empty(q)),                                    \
                  (q).size--,                                           \
                  (q).head = min((q).head + 1u,                         \
                                 (q).head + 1u - (q).modulus),          \
                  (q).root[(q).head])

#define unshift(q,e) (assert((q).size < (q).modulus),                   \
                      (q).size++,                                       \
                      (q).root[(q).head] = (e),                         \
                      (q).head = min((q).head - 1,                      \
                                     (q).head - 1 + (q).modulus),       \
                      (void)0)

#define push(q,e) (assert((q).size < (q).modulus),                      \
                   (q).size++,                                          \
                   (q).root[min((q).head + (q).size,                    \
                                (q).head + (q).size - (q).modulus)] = (e))

#define pop(q) (assert(!empty(q)),                                      \
                (q).size--,                                             \
                (q).root[min((q).head + (q).size + 1,                   \
                             (q).head + (q).size + 1 - (q).modulus)])


void sched_lock(void);
void sched_unlock(void);

void source_close(void);
void source_release_buffer(void *buffer);

void sink_write_buffer(void *buffer, size_t size, size_t weight);

void xread(void *vbuf, size_t *vacant);
void xwrite(const void *vbuf, size_t size);

void up_heap(void *root, unsigned size);
void down_heap(void *root, unsigned size);
