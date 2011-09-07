/* lacos_rbtree.c,v 1.1.1.1 2008/09/01 17:45:52 lacos Exp */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "lacos_rbtree.h" /* struct lacos_rbtree_node */


struct lacos_rbtree_node {
  void *data;
  struct lacos_rbtree_node *parent,
      *left,
      *right;
  enum lacos_rbtree_color {
      LACOS_RBTREE_RED,
      LACOS_RBTREE_BLACK
  } color;
};


struct lacos_rbtree_node *
lacos_rbtree_find(struct lacos_rbtree_node *root, const void *key,
    int (*cmp)(const void *cmp_key, const void *cmp_data))
{
  int cmp_res;
  while (root && (cmp_res = (*cmp)(key, root->data)))
    root = cmp_res < 0 ? root->left : root->right;
  return root;
}


struct lacos_rbtree_node *
lacos_rbtree_min(struct lacos_rbtree_node *root)
{
  struct lacos_rbtree_node *tmp;

  if (!root)
    return 0;

  while ((tmp = root->left))
    root = tmp;

  return root;
}


struct lacos_rbtree_node *
lacos_rbtree_max(struct lacos_rbtree_node *root)
{
  struct lacos_rbtree_node *tmp;

  if (!root)
    return 0;

  while ((tmp = root->right))
    root = tmp;

  return root;
}


struct lacos_rbtree_node *
lacos_rbtree_next(struct lacos_rbtree_node *current)
{
  struct lacos_rbtree_node *tmp;

  if (!current)
    return 0;

  if ((tmp = current->right)) {
    while ((current = tmp->left))
      tmp = current;
    return (struct lacos_rbtree_node *)tmp;
  }

  while ((tmp = current->parent) && current == tmp->right)
    current = tmp;

  return tmp;
}


struct lacos_rbtree_node *
lacos_rbtree_prev(struct lacos_rbtree_node *current)
{
  struct lacos_rbtree_node *tmp;

  if (!current)
    return 0;

  if ((tmp = current->left)) {
    while ((current = tmp->right))
      tmp = current;
    return (struct lacos_rbtree_node *)tmp;
  }

  while ((tmp = current->parent) && current == tmp->left)
    current = tmp;

  return (struct lacos_rbtree_node *)tmp;
}


static void
lacos_rbtree_rotate_left(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node *rotation_root)
{
  struct lacos_rbtree_node
      *parent = rotation_root->parent,
      *rc = rotation_root->right,
      *rlc = rc->left;

  rotation_root->right = rlc;
  if (rlc)
    rlc->parent = rotation_root;
  rc->parent = parent;
  if (parent)
    if (rotation_root == parent->left)
      parent->left = rc;
    else
      parent->right = rc;
  else
    *new_root = rc;
  rc->left = rotation_root;
  rotation_root->parent = rc;
}


static void
lacos_rbtree_rotate_right(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node *rotation_root)
{
  struct lacos_rbtree_node
      *parent = rotation_root->parent,
      *lc = rotation_root->left,
      *lrc = lc->right;

  rotation_root->left = lrc;
  if (lrc)
    lrc->parent = rotation_root;
  lc->parent = parent;
  if (parent)
    if (rotation_root == parent->left)
      parent->left = lc;
    else
      parent->right = lc;
  else
    *new_root = lc;
  lc->right = rotation_root;
  rotation_root->parent = lc;
}


int
lacos_rbtree_insert(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node **new_node, void *new_data,
    int (*cmp)(const void *cmp_new_data, const void *cmp_data),
    void *(*alloc)(size_t size, void *alloc_ctl), void *alloc_ctl)
{
  int cmp_res;
  struct lacos_rbtree_node *tmp = *new_root, *parent = 0;

  while (tmp && (cmp_res = (*cmp)(new_data, tmp->data))) {
    parent = tmp;
    tmp = cmp_res < 0 ? tmp->left : tmp->right;
  }

  if (tmp) {
    *new_node = tmp;
    return -1;
  }

  if (!(*new_node = tmp = (*alloc)(sizeof(struct lacos_rbtree_node),
      alloc_ctl)))
    return -1;

  tmp->data = new_data;
  tmp->parent = parent;
  tmp->left = 0;
  tmp->right = 0;
  if (parent)
    if (cmp_res < 0)
      parent->left = tmp;
    else
      parent->right = tmp;
  else {
    *new_root = tmp;
    tmp->color = LACOS_RBTREE_BLACK;
    return 0;
  }

  {
    struct lacos_rbtree_node *root = *new_root, *grandparent, *uncle;

    tmp->color = LACOS_RBTREE_RED;

    while (tmp != root && LACOS_RBTREE_RED == parent->color) {
      grandparent = parent->parent;
      if (parent == grandparent->left) {
        uncle = grandparent->right;
        if (uncle && LACOS_RBTREE_RED == uncle->color) {
          parent->color = LACOS_RBTREE_BLACK;
          uncle->color = LACOS_RBTREE_BLACK;
          grandparent->color = LACOS_RBTREE_RED;
          tmp = grandparent;
          parent = tmp->parent;
        }
        else {
          if (tmp == parent->right) {
            tmp = parent;
            lacos_rbtree_rotate_left(&root, tmp);
            parent = tmp->parent;
            grandparent = parent->parent;
          }
          parent->color = LACOS_RBTREE_BLACK;
          grandparent->color = LACOS_RBTREE_RED;
          lacos_rbtree_rotate_right(&root, grandparent);
        }
      }
      else {
        uncle = grandparent->left;
        if (uncle && LACOS_RBTREE_RED == uncle->color) {
          parent->color = LACOS_RBTREE_BLACK;
          uncle->color = LACOS_RBTREE_BLACK;
          grandparent->color = LACOS_RBTREE_RED;
          tmp = grandparent;
          parent = tmp->parent;
        }
        else {
          if (tmp == parent->left) {
            tmp = parent;
            lacos_rbtree_rotate_right(&root, tmp);
            parent = tmp->parent;
            grandparent = parent->parent;
          }
          parent->color = LACOS_RBTREE_BLACK;
          grandparent->color = LACOS_RBTREE_RED;
          lacos_rbtree_rotate_left(&root, grandparent);
        }
      }
    }

    root->color = LACOS_RBTREE_BLACK;
    *new_root = root;
  }
  return 0;
}


void
lacos_rbtree_delete(struct lacos_rbtree_node **new_root,
    struct lacos_rbtree_node *old_node, void **old_data,
    void (*dealloc)(void *ptr, void *alloc_ctl), void *alloc_ctl)
{
  struct lacos_rbtree_node *child, *parent, *root = *new_root,
      *old_node_left = old_node->left,
      *old_node_right = old_node->right,
      *old_node_parent = old_node->parent;
  enum lacos_rbtree_color color_of_firstly_unlinked;

  if (old_data)
    *old_data = old_node->data;

  if (old_node_left && old_node_right) {
    struct lacos_rbtree_node
        *to_relink = old_node_right,
        *tmp = to_relink->left;

    if (tmp) {
      do {
        to_relink = tmp;
        tmp = tmp->left;
      } while (tmp);
      parent = to_relink->parent;
      child = to_relink->right;
      parent->left = child;
      if (child)
        child->parent = parent;
      to_relink->right = old_node_right;
      old_node_right->parent = to_relink;
    }
    else {
      parent = old_node_right;
      child = old_node_right->right;
    }

    to_relink->left = old_node_left;
    old_node_left->parent = to_relink;

    color_of_firstly_unlinked = to_relink->color;
    to_relink->color = old_node->color;

    to_relink->parent = old_node_parent;

    if (old_node_parent)
      if (old_node == old_node_parent->left)
        old_node_parent->left = to_relink;
      else
        old_node_parent->right = to_relink;
    else
      root = to_relink;
  }
  else {
    parent = old_node_parent;
    child = old_node_left ? old_node_left : old_node_right;
    color_of_firstly_unlinked = old_node->color;

    if (child)
      child->parent = parent;
    if (old_node_parent)
      if (old_node == old_node_parent->left)
        old_node_parent->left = child;
      else
        old_node_parent->right = child;
    else
      root = child;
  }

  (*dealloc)(old_node, alloc_ctl);

  if (LACOS_RBTREE_BLACK == color_of_firstly_unlinked) {
    struct lacos_rbtree_node *brother, *left_nephew, *right_nephew;
    int left_black, right_black;

    while (child != root && (!child || LACOS_RBTREE_BLACK == child->color))
      if (child == parent->left) {
        brother = parent->right;
        if (LACOS_RBTREE_RED == brother->color) {
          brother->color = LACOS_RBTREE_BLACK;
          parent->color = LACOS_RBTREE_RED;
          lacos_rbtree_rotate_left(&root, parent);
          brother = parent->right;
        }
        left_nephew = brother->left;
        right_nephew = brother->right;
        right_black = !right_nephew
            || LACOS_RBTREE_BLACK == right_nephew->color;
        if ((!left_nephew || LACOS_RBTREE_BLACK == left_nephew->color)
            && right_black) {
          brother->color = LACOS_RBTREE_RED;
          child = parent;
          parent = parent->parent;
        }
        else {
          if (right_black) {
            left_nephew->color = LACOS_RBTREE_BLACK;
            brother->color = LACOS_RBTREE_RED;
            lacos_rbtree_rotate_right(&root, brother);
            brother = parent->right;
            right_nephew = brother->right;
          }
          brother->color = parent->color;
          parent->color = LACOS_RBTREE_BLACK;
          right_nephew->color = LACOS_RBTREE_BLACK;
          lacos_rbtree_rotate_left(&root, parent);
          child = root;
          break;
        }
      }
      else {
        brother = parent->left;
        if (LACOS_RBTREE_RED == brother->color) {
          brother->color = LACOS_RBTREE_BLACK;
          parent->color = LACOS_RBTREE_RED;
          lacos_rbtree_rotate_right(&root, parent);
          brother = parent->left;
        }
        right_nephew = brother->right;
        left_nephew = brother->left;
        left_black = !left_nephew || LACOS_RBTREE_BLACK == left_nephew->color;
        if ((!right_nephew || LACOS_RBTREE_BLACK == right_nephew->color)
            && left_black) {
          brother->color = LACOS_RBTREE_RED;
          child = parent;
          parent = parent->parent;
        }
        else {
          if (left_black) {
            right_nephew->color = LACOS_RBTREE_BLACK;
            brother->color = LACOS_RBTREE_RED;
            lacos_rbtree_rotate_left(&root, brother);
            brother = parent->left;
            left_nephew = brother->left;
          }
          brother->color = parent->color;
          parent->color = LACOS_RBTREE_BLACK;
          left_nephew->color = LACOS_RBTREE_BLACK;
          lacos_rbtree_rotate_right(&root, parent);
          child = root;
          break;
        }
      }

    if (child)
      child->color = LACOS_RBTREE_BLACK;
  }

  *new_root = root;
}
