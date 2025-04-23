
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * The red-black tree code is based on the algorithm described in
 * the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
 */


static ngx_inline void ngx_rbtree_left_rotate(ngx_rbtree_node_t **root,
    ngx_rbtree_node_t *sentinel, ngx_rbtree_node_t *node);
static ngx_inline void ngx_rbtree_right_rotate(ngx_rbtree_node_t **root,
    ngx_rbtree_node_t *sentinel, ngx_rbtree_node_t *node);


void
ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node)
{
    ngx_rbtree_node_t  **root, *temp, *sentinel;

    /* a binary tree insert */

    root = &tree->root;
    sentinel = tree->sentinel;

    if (*root == sentinel) {
        node->parent = NULL;
        node->left = sentinel;
        node->right = sentinel;
        ngx_rbt_black(node);
        *root = node;

        return;
    }

    tree->insert(*root, node, sentinel);

    /* re-balance tree */

    while (node != *root && ngx_rbt_is_red(node->parent)) {

        if (node->parent == node->parent->parent->left) {
            temp = node->parent->parent->right;

            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(node->parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(node->parent->parent);
                node = node->parent->parent;

            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    ngx_rbtree_left_rotate(root, sentinel, node);
                }

                ngx_rbt_black(node->parent);
                ngx_rbt_red(node->parent->parent);
                ngx_rbtree_right_rotate(root, sentinel, node->parent->parent);
            }

        } else {
            temp = node->parent->parent->left;

            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(node->parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(node->parent->parent);
                node = node->parent->parent;

            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    ngx_rbtree_right_rotate(root, sentinel, node);
                }

                ngx_rbt_black(node->parent);
                ngx_rbt_red(node->parent->parent);
                ngx_rbtree_left_rotate(root, sentinel, node->parent->parent);
            }
        }
    }

    ngx_rbt_black(*root);
}


void
ngx_rbtree_insert_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel)
{
    // 定义一个指针p，用于遍历红黑树
    ngx_rbtree_node_t  **p;

    // 无限循环，直到找到插入位置
    for ( ;; ) {

        // 根据node的key值与temp的key值比较，决定向左子树还是右子树遍历
        p = (node->key < temp->key) ? &temp->left : &temp->right;

        // 如果p指向sentinel，说明找到了插入位置，跳出循环
        if (*p == sentinel) {
            break;
        }

        // 否则，继续向下遍历
        temp = *p;
    }

    // 将node插入到找到的位置
    *p = node;
    // 设置node的父节点为temp
    node->parent = temp;
    // 设置node的左子树和右子树都为sentinel
    node->left = sentinel;
    node->right = sentinel;
    // 将node节点设置为红色
    ngx_rbt_red(node);
}


void
ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t  **p;

    for ( ;; ) {

        /*
         * Timer values
         * 1) are spread in small range, usually several minutes,
         * 2) and overflow each 49 days, if milliseconds are stored in 32 bits.
         * The comparison takes into account that overflow.
         */

        /*  node->key < temp->key */

        // 根据node和temp的key值比较结果，选择插入到temp的左子树或右子树
        p = ((ngx_rbtree_key_int_t) (node->key - temp->key) < 0)
            ? &temp->left : &temp->right;

        // 如果找到合适的位置（即p指向sentinel），则跳出循环
        if (*p == sentinel) {
            break;
        }

        // 否则，继续在子树中查找
        temp = *p;
    }

    // 将node插入到找到的位置
    *p = node;
    // 设置node的父节点为temp
    node->parent = temp;
    // 初始化node的左右子树为sentinel
    node->left = sentinel;
    node->right = sentinel;
    // 将node设置为红色节点
    ngx_rbt_red(node);
}


void
ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node)
{
    ngx_uint_t           red;          // 用于标记节点颜色
    ngx_rbtree_node_t  **root, *sentinel, *subst, *temp, *w;

    /* a binary tree delete */

    root = &tree->root;                // 获取树的根节点指针
    sentinel = tree->sentinel;         // 获取树的哨兵节点

    // 如果要删除的节点是叶子节点或只有一个子节点
    if (node->left == sentinel) {
        temp = node->right;            // 将要删除节点的右子节点赋值给temp
        subst = node;                  // 将要删除节点赋值给subst

    } else if (node->right == sentinel) {
        temp = node->left;             // 将要删除节点的左子节点赋值给temp
        subst = node;                  // 将要删除节点赋值给subst

    } else {
        // 如果要删除的节点有两个子节点，找到右子树中的最小节点
        subst = ngx_rbtree_min(node->right, sentinel);
        temp = subst->right;           // 将最小节点的右子节点赋值给temp
    }

    // 如果要删除的节点是根节点
    if (subst == *root) {
        *root = temp;                  // 将根节点指向temp
        ngx_rbt_black(temp);           // 将temp节点染成黑色

        /* DEBUG stuff */
        node->left = NULL;
        node->right = NULL;
        node->parent = NULL;
        node->key = 0;

        return;
    }

    red = ngx_rbt_is_red(subst);       // 检查subst节点是否为红色

    // 根据subst在父节点中的位置，调整父节点的子节点指针
    if (subst == subst->parent->left) {
        subst->parent->left = temp;

    } else {
        subst->parent->right = temp;
    }

    // 如果subst就是要删除的节点
    if (subst == node) {

        temp->parent = subst->parent;

    } else {

        // 如果subst不是要删除的节点，进行节点替换
        if (subst->parent == node) {
            temp->parent = subst;

        } else {
            temp->parent = subst->parent;
        }

        subst->left = node->left;
        subst->right = node->right;
        subst->parent = node->parent;
        ngx_rbt_copy_color(subst, node); // 复制颜色

        // 更新根节点或父节点的子节点指针
        if (node == *root) {
            *root = subst;

        } else {
            if (node == node->parent->left) {
                node->parent->left = subst;
            } else {
                node->parent->right = subst;
            }
        }

        // 更新subst的子节点的父节点指针
        if (subst->left != sentinel) {
            subst->left->parent = subst;
        }

        if (subst->right != sentinel) {
            subst->right->parent = subst;
        }
    }

    /* DEBUG stuff */
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    node->key = 0;

    // 如果subst是红色节点，直接返回
    if (red) {
        return;
    }

    /* a delete fixup */

    // 进行红黑树删除后的调整
    while (temp != *root && ngx_rbt_is_black(temp)) {

        if (temp == temp->parent->left) {
            w = temp->parent->right;

            // 如果兄弟节点是红色
            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                w = temp->parent->right;
            }

            // 如果兄弟节点的两个子节点都是黑色
            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                // 如果兄弟节点的右子节点是黑色
                if (ngx_rbt_is_black(w->right)) {
                    ngx_rbt_black(w->left);
                    ngx_rbt_red(w);
                    ngx_rbtree_right_rotate(root, sentinel, w);
                    w = temp->parent->right;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->right);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                temp = *root;
            }

        } else {
            w = temp->parent->left;

            // 如果兄弟节点是红色
            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                w = temp->parent->left;
            }

            // 如果兄弟节点的两个子节点都是黑色
            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                // 如果兄弟节点的左子节点是黑色
                if (ngx_rbt_is_black(w->left)) {
                    ngx_rbt_black(w->right);
                    ngx_rbt_red(w);
                    ngx_rbtree_left_rotate(root, sentinel, w);
                    w = temp->parent->left;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->left);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        }
    }

    ngx_rbt_black(temp);               // 将temp节点染成黑色
}


// 定义一个静态内联函数，用于对红黑树进行左旋操作
static ngx_inline void
ngx_rbtree_left_rotate(ngx_rbtree_node_t **root, ngx_rbtree_node_t *sentinel,
    ngx_rbtree_node_t *node)
{
    // 定义一个临时指针变量，用于存储当前节点的右子节点
    ngx_rbtree_node_t  *temp;

    // 将当前节点的右子节点赋值给临时变量
    temp = node->right;
    // 将当前节点的右子节点设置为临时变量的左子节点
    node->right = temp->left;

    // 如果临时变量的左子节点不是哨兵节点，则将其父节点设置为当前节点
    if (temp->left != sentinel) {
        temp->left->parent = node;
    }

    // 将临时变量的父节点设置为当前节点的父节点
    temp->parent = node->parent;

    // 如果当前节点是根节点，则将根节点设置为临时变量
    if (node == *root) {
        *root = temp;

    // 如果当前节点是其父节点的左子节点，则将父节点的左子节点设置为临时变量
    } else if (node == node->parent->left) {
        node->parent->left = temp;

    // 否则，将父节点的右子节点设置为临时变量
    } else {
        node->parent->right = temp;
    }

    // 将临时变量的左子节点设置为当前节点
    temp->left = node;
    // 将当前节点的父节点设置为临时变量
    node->parent = temp;
}


// 定义一个静态内联函数，用于在红黑树中进行右旋操作
static ngx_inline void
ngx_rbtree_right_rotate(ngx_rbtree_node_t **root, ngx_rbtree_node_t *sentinel,
    ngx_rbtree_node_t *node)
{
    // 定义一个临时指针变量temp，用于辅助旋转操作
    ngx_rbtree_node_t  *temp;

    // 将temp指向当前节点node的左子节点
    temp = node->left;
    // 将当前节点node的左子节点指向temp的右子节点
    node->left = temp->right;

    // 如果temp的右子节点不是哨兵节点（即不为空）
    if (temp->right != sentinel) {
        // 将temp的右子节点的父节点指向当前节点node
        temp->right->parent = node;
    }

    // 将temp的父节点指向当前节点node的父节点
    temp->parent = node->parent;

    // 如果当前节点node是根节点
    if (node == *root) {
        // 将根节点指向temp
        *root = temp;

    // 如果当前节点node是其父节点的右子节点
    } else if (node == node->parent->right) {
        // 将node的父节点的右子节点指向temp
        node->parent->right = temp;

    // 如果当前节点node是其父节点的左子节点
    } else {
        // 将node的父节点的左子节点指向temp
        node->parent->left = temp;
    }

    // 将temp的右子节点指向当前节点node
    temp->right = node;
    // 将当前节点node的父节点指向temp
    node->parent = temp;
}


// 定义函数 ngx_rbtree_next，用于在红黑树中找到指定节点的下一个节点
ngx_rbtree_node_t *
ngx_rbtree_next(ngx_rbtree_t *tree, ngx_rbtree_node_t *node)
{
    // 定义指针变量 root 指向树的根节点，sentinel 指向树的哨兵节点，parent 用于存储当前节点的父节点
    ngx_rbtree_node_t  *root, *sentinel, *parent;

    // 获取树的哨兵节点
    sentinel = tree->sentinel;

    // 如果当前节点的右子节点不是哨兵节点，说明当前节点有右子树
    if (node->right != sentinel) {
        // 返回右子树中的最小节点，即当前节点的下一个节点
        return ngx_rbtree_min(node->right, sentinel);
    }

    // 获取树的根节点
    root = tree->root;

    // 无限循环，直到找到下一个节点或返回 NULL
    for ( ;; ) {
        // 获取当前节点的父节点
        parent = node->parent;

        // 如果当前节点是根节点，说明没有下一个节点，返回 NULL
        if (node == root) {
            return NULL;
        }

        // 如果当前节点是其父节点的左子节点，说明当前节点是其父节点的下一个节点
        if (node == parent->left) {
            return parent;
        }

        // 否则，将当前节点更新为其父节点，继续向上查找
        node = parent;
    }
}
