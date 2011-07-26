/* lacos_rbtree.h,v 1.1.1.1 2008/09/01 17:45:52 lacos Exp */

#ifndef LACOS_RBTREE_H
#  define LACOS_RBTREE_H

#  include <sys/types.h> /* size_t */

#  ifdef __cplusplus
extern "C" {
#  endif


struct lacos_rbtree_node;
/*
  Opaque data type for red-black binary tree nodes. To get its data pointer,
  cast a (struct lacos_rbtree_node *) object to (void **) and derefer it once.
  Empty trees equal to (struct lacos_rbtree_node *)0.

  This group of functions is generally not thread-safe (although it doesn't use
  any static data), because the non-read-only operations must exclude all
  operations on the same tree via external locking in a multi-threaded
  environment.

  If you want to alter the (your own) "key" field in an element, you mustn't do
  that in one step. It must involve one delete and one insert operation, in the
  order of your choice. Delete-insert is probably faster than insert-delete.
*/


/*
  The functions below all return and mainly take pointers to nodes, not
  elements (pointers to void). However, for simplicity of language, they are
  described as if they operated on pointers to void (elements).
*/


struct lacos_rbtree_node *
lacos_rbtree_find(struct lacos_rbtree_node *root, const void *key,
    int (*cmp)(const void *cmp_key, const void *cmp_data));
/*
  USAGE:
    Find an element in the tree.

  ARGUMENTS:
    root:
      Root of the tree.

    key:
      This specifies which element should be found. This doesn't need to be an
      element, it can be a standalone key too, if you write the "cmp" function
      accordingly.

    cmp:
      A function which should return an integer less than, equal to, or greater
      than zero if the "key" argument compares less than, equal to, or greater
      than the currently inspected element in the tree.

  RETURN VALUE:
    If the key is found, this function returns the element (actually the
    address of the containing node). If it is not found, 0 is returned.

  READ-ONLY OPERATION:
    Yes.
*/


struct lacos_rbtree_node *
lacos_rbtree_min(struct lacos_rbtree_node *root);
/*
  USAGE:
    Get the smallest element in the tree.

  ARGUMENTS:
    root:
      Root of the tree.

  RETURN VALUE:
    If the tree is empty ("root" is null), 0 is returned. Otherwise, the
    smallest element is returned.

  READ-ONLY OPERATION:
    Yes.
*/


struct lacos_rbtree_node *
lacos_rbtree_max(struct lacos_rbtree_node *root);
/*
  USAGE:
    Get the greatest element in the tree.

  ARGUMENTS:
    root:
      Root of the tree.

  RETURN VALUE:
    If the tree is empty ("root" is null), 0 is returned. Otherwise, the
    greatest element is returned.

  READ-ONLY OPERATION:
    Yes.
*/


struct lacos_rbtree_node *
lacos_rbtree_next(struct lacos_rbtree_node *current);
/*
  USAGE:
    Get the smallest element greater than "current".

  ARGUMENTS:
    current:
      An element.

  RETURN VALUE:
    If "current" is null or the last element in the tree, 0 is returned.
    Otherwise, the smallest element greater than "current" is returned.

  READ-ONLY OPERATION:
    Yes.
*/


struct lacos_rbtree_node *
lacos_rbtree_prev(struct lacos_rbtree_node *current);
/*
  USAGE:
    Get the greatest element smaller than "current".

  ARGUMENTS:
    current:
      An element.

  RETURN VALUE:
    If "current" is null or the first element in the tree, 0 is returned.
    Otherwise, the greatest element smaller than "current" is returned.

  READ-ONLY OPERATION:
    Yes.
*/


int
lacos_rbtree_insert(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node **new_node, void *new_data,
    int (*cmp)(const void *cmp_new_data, const void *cmp_data),
    void *(*alloc)(size_t size, void *alloc_ctl), void *alloc_ctl);
/*
  USAGE:
    Insert an element into the tree.

  ARGUMENTS:
    new_root:
      Address of the tree's root; "*new_root" is the root of the tree. Both
      input and output.

    new_node:
      Output only.

    new_data:
      The data to insert. This must not be a standalone key, this must be a
      full element. It will only be linked in, not copied.

    cmp:
      A function which should return an integer less than, equal to, or greater
      than zero if the "new_data" argument compares less than, equal to, or
      greater than the currently inspected element in the tree.

    alloc:
      A memory allocator function whose externally observable behavior is
      consistent with that of "malloc". It must work together with the
      "dealloc" function passed to "lacos_rbtree_delete".

    alloc_ctl:
      A pointer to a custom data type to supply the "alloc" function with
      optional auxiliary control information (arena etc).

  RETURN VALUE:
    If the insertion succeeds, 0 is returned, the new element is stored into
    "*new_node", and "*new_root" is updated to the new root of the tree.

    Otherwise, "*new_root" is not modified, and -1 is returned because of one
    of the following errors:

    - An element with a colliding key was found in the tree. In this case, the
      colliding element is stored into "*new_node".
    - There was not enough memory to allocate a new node object. In this case,
      0 is stored into "*new_node".

    Existing node pointers remain valid in any case.

  READ-ONLY OPERATION:
    No.
*/


void
lacos_rbtree_delete(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node *old_node, void **old_data,
    void (*dealloc)(void *ptr, void *alloc_ctl), void *alloc_ctl);
/*
  USAGE:
    Remove an element from the tree.

  ARGUMENTS:
    new_root:
      Address of the tree's root; "*new_root" is the root of the tree. Both
      input and output.

    old_node:
      The element to remove. This must be a valid node pointer into the tree.

    old_data:
      The deleted element (the data of the deleted node, which was specified by
      the "new_data" argument of the corresponding "lacos_rbtree_insert" call).
      You could get the data also through "*(void **)old_node" before calling
      this function. The data pointer of the node to delete is only accessed
      and stored into "*old_data" if "old_data" is not 0.

    dealloc:
      A memory deallocator function whose externally observable behavior is
      consistent with that of "free". It must work together with the "alloc"
      function passed to "lacos_rbtree_insert".

    alloc_ctl:
      A pointer to a custom data type to supply the "dealloc" function with
      optional auxiliary control information (arena etc).

    Existing node pointers different from "old_node" remain valid.

  READ-ONLY OPERATION:
    No.
*/


#  ifdef __cplusplus
}
#  endif

#endif
