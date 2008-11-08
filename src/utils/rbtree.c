/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#ifndef NDEBUG
#include <stdio.h>
#endif

#include <parserutils/errors.h>
#include <parserutils/functypes.h>

#include "utils/rbtree.h"

/* Compile-time configuration */

/* Define this to validate the tree structure after every insertion/deletion 
 * Note: this performs a complete tree traversal, so has a large performance 
 * impact */
#undef RBTREE_VALIDATE

/* Define this to use deleteMax() to destroy a tree. 
 * Uses deleteMin() if undefined. */
#undef USE_DELETEMAX

/**
 * Node in an RB tree
 */
typedef struct rbnode
{
	struct rbnode *left;		/**< Left subtree */
	struct rbnode *right;		/**< Right subtree */
	enum { RED = true, BLACK = false } colour;	/**< Colour of node */

	void *key;			/**< Node key */
	void *value;			/**< Node value */
} rbnode;

/**
 * RB tree object
 */
struct parserutils_rbtree
{
	rbnode *root;			/**< Root node of tree */

	parserutils_rbtree_cmp cmp;	/**< Key comparator */

	parserutils_alloc alloc;	/**< Memory (de)allocation function */
	void *pw;			/**< Pointer to client data */
};

static rbnode *rbtree_insert_internal(parserutils_rbtree *tree, 
		rbnode *current, void *key, void *value, void **oldvalue);
static rbnode *rbtree_delete_internal(parserutils_rbtree *tree, 
		rbnode *current, const void *key, void **intkey, void **value);
static inline rbnode *rbnode_create(parserutils_alloc alloc, void *pw, 
		void *key, void *value);
static inline void rbnode_destroy(parserutils_alloc alloc, void *pw, 
		rbnode *node);
static inline bool isRed(rbnode *node);
static inline void colourFlip(rbnode *node);
static inline rbnode *rotateLeft(rbnode *node);
static inline rbnode *rotateRight(rbnode *node);
static inline rbnode *moveRedLeft(rbnode *node);
static inline rbnode *moveRedRight(rbnode *node);
static inline rbnode *fixUp(rbnode *node);
#ifdef USE_DELETEMAX
static inline rbnode *deleteMax(parserutils_rbtree *tree, rbnode *node,
		parserutils_rbtree_del destructor, void *pw);
#else
static inline rbnode *deleteMin(parserutils_rbtree *tree, rbnode *node, 
		parserutils_rbtree_del destructor, void *pw);
#endif
static inline rbnode *findMin(rbnode *node);

#ifndef NDEBUG
#ifdef RBTREE_VALIDATE
static inline void verify_tree(rbnode *node);
static inline rbnode *verify_tree_internal(rbnode *node);
static inline void dump_tree_raw(rbnode *node, uint32_t depth);
#endif
static inline void dump_tree(rbnode *node, parserutils_rbtree_print print, 
		uint32_t depth);
#endif

/**
 * Create a RB tree
 *
 * \param cmp    Comparator routine for keys
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \param tree   Pointer to location to receive tree instance
 * \return PARSERUTILS_OK on success, 
 *         PARSERUTILS_BADPARM on bad parameters,
 *         PARSERUTILS_NOMEM on memory exhaustion
 */
parserutils_error parserutils_rbtree_create(parserutils_rbtree_cmp cmp, 
		parserutils_alloc alloc, void *pw, parserutils_rbtree **tree)
{
	parserutils_rbtree *t;

	if (cmp == NULL || alloc == NULL || tree == NULL)
		return PARSERUTILS_BADPARM;

	t = alloc(NULL, sizeof(parserutils_rbtree), pw);
	if (t == NULL)
		return PARSERUTILS_NOMEM;

	t->root = NULL;
	t->cmp = cmp;
	t->alloc = alloc;
	t->pw = pw;

	*tree = t;

	return PARSERUTILS_OK;
}

/**
 * Destroy an RB tree
 *
 * \param tree        The tree to destroy
 * \param destructor  Routine to be called with key/value pairs to destroy
 * \param pw          Pointer to client-specific private data
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_rbtree_destroy(parserutils_rbtree *tree,
		parserutils_rbtree_del destructor, void *pw)
{
	if (tree == NULL)
		return PARSERUTILS_BADPARM;

	while (tree->root != NULL) {
#ifndef USE_DELETEMAX
		tree->root = deleteMin(tree, tree->root, destructor, pw);
#else
		tree->root = deleteMax(tree, tree->root, destructor, pw);
#endif
		if (tree->root != NULL)
			tree->root->colour = BLACK;

#if !defined(NDEBUG) && defined(RBTREE_VALIDATE)
		verify_tree(tree->root);
#endif
	}

	tree->alloc(tree, 0, tree->pw);

	return PARSERUTILS_OK;
}

/**
 * Insert into a RB tree
 * 
 * \param tree   The tree to insert into
 * \param key    The key to insert (not copied)
 * \param value  The value to insert (not copied)
 * \param oldvalue  Pointer to location to receive previous value for key
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_rbtree_insert(parserutils_rbtree *tree, 
		void *key, void *value, void **oldvalue)
{
	rbnode *temp;

	if (tree == NULL || key == NULL || oldvalue == NULL)
		return PARSERUTILS_BADPARM;

	temp = rbtree_insert_internal(tree, tree->root, key, 
			value, oldvalue);
	if (temp == NULL)
		return PARSERUTILS_NOMEM;

	temp->colour = BLACK;
	tree->root = temp;

#if !defined(NDEBUG) && defined(RBTREE_VALIDATE)
	verify_tree(tree->root);
#endif

	return PARSERUTILS_OK;
}

/**
 * Find an entry in a RB tree
 *
 * \param tree   The tree to search in
 * \param key    The key to search for
 * \param value  Pointer to location to receive value for key
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_rbtree_find(parserutils_rbtree *tree, 
		const void *key, void **value)
{
	rbnode *node;

	if (tree == NULL || key == NULL || value == NULL)
		return PARSERUTILS_BADPARM;

	node = tree->root;
	*value = NULL;

	while (node != NULL) {
		int cmp = tree->cmp(key, node->key);

		if (cmp == 0) {
			*value = node->value;
			break;
		} else if (cmp < 0) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	return PARSERUTILS_OK;
}

/**
 * Delete an entry from a RB tree
 *
 * \param tree    The tree to delete from
 * \param key     The key to delete
 * \param intkey  Pointer to location to receive key
 * \param value   Pointer to location to receive value
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_rbtree_delete(parserutils_rbtree *tree, 
		const void *key, void **intkey, void **value)
{
	rbnode *temp;

	if (tree == NULL || key == NULL || intkey == NULL || value == NULL)
		return PARSERUTILS_BADPARM;

	if (tree->root == NULL) {
		*intkey = NULL;
		*value = NULL;
		return PARSERUTILS_OK;
	}

	temp = rbtree_delete_internal(tree, tree->root, key, intkey, value);
	if (temp != NULL)
		temp->colour = BLACK;
	tree->root = temp;

#if !defined(NDEBUG) && defined(RBTREE_VALIDATE)
	verify_tree(tree->root);
#endif

	return PARSERUTILS_OK;
}

/**
 * Helper function for tree insertion
 *
 * \param tree      Tree to insert into
 * \param current   The root of the current subtree
 * \param key       Key to insert (not copied)
 * \param value     Value to insert (not copied)
 * \param oldvalue  Pointer to location to receive old value for key
 * \return Pointer to new root of current subtree
 */
rbnode *rbtree_insert_internal(parserutils_rbtree *tree, rbnode *current, 
		void *key, void *value, void **oldvalue)
{
	if (current == NULL) {
		rbnode *node = rbnode_create(tree->alloc, tree->pw, key, value);
		if (node == NULL)
			return NULL;

		*oldvalue = NULL;

		return node;
	}

	/* Split 4-nodes */
	if (isRed(current->left) && isRed(current->right))
		colourFlip(current);

	int cmp = tree->cmp(key, current->key);

	if (cmp == 0) {
		*oldvalue = current->value;
		current->value = value;
	} else if (cmp < 0) {
		rbnode *temp = rbtree_insert_internal(tree, current->left, 
				key, value, oldvalue);
		if (temp == NULL)
			return NULL;
		current->left = temp;
	} else {
		rbnode *temp = rbtree_insert_internal(tree, current->right,
				key, value, oldvalue);
		if (temp == NULL)
			return NULL;
		current->right = temp;
	}

	/* Eliminate right-leaning 3-nodes by rotating left */
	if (isRed(current->right))
		current = rotateLeft(current);

	/* Balance 4-nodes by rotating right */
	if (isRed(current->left) && isRed(current->left->left))
		current = rotateRight(current);

	return current;
}

/**
 * Helper function for tree deletion
 * 
 * \param tree     Tree to delete from
 * \param current  Root node of current subtree
 * \param key      Key to delete
 * \param intkey   Pointer to location to receive key
 * \param value    Pointer to location to receive value
 * \return Pointer to new root node for current subtree
 */
rbnode *rbtree_delete_internal(parserutils_rbtree *tree, rbnode *current, 
		const void *key, void **intkey, void **value)
{
	int cmp;

	if (current == NULL)
		return NULL;

	cmp = tree->cmp(key, current->key);

	if (cmp < 0) {
		if (current->left != NULL && !isRed(current->left) && 
				!isRed(current->left->left))
			current = moveRedLeft(current);
		current->left = rbtree_delete_internal(tree, current->left, 
				key, intkey, value);
	} else {
		if (isRed(current->left))
			current = rotateRight(current);

		if (cmp == 0 && current->right == NULL) {
			*intkey = current->key;
			*value = current->value;

			rbnode_destroy(tree->alloc, tree->pw, current);
			return NULL;
		}

		if (current->right != NULL && !isRed(current->right) && 
				!isRed(current->right->left))
			current = moveRedRight(current);

		/* Must recalculate here, as current may have changed */
		if (tree->cmp(key, current->key) == 0) {
			*intkey = current->key;
			*value = current->value;

			rbnode *successor = findMin(current->right);

			current->value = successor->value;
			current->key = successor->key;

			current->right = deleteMin(tree, current->right, 
					NULL, NULL);
		} else {
			current->right = rbtree_delete_internal(tree,
					current->right, key, intkey, value);
		}
	}

	return fixUp(current);
}

/**
 * Create a RB node
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \param key    Key for node (not copied)
 * \param value  Value for node (not copied)
 * \return Pointer to node, or NULL on memory exhaustion
 */
rbnode *rbnode_create(parserutils_alloc alloc, void *pw, void *key, void *value)
{
	rbnode *node = alloc(NULL, sizeof(rbnode), pw);
	if (node == NULL)
		return NULL;

	node->left = NULL;
	node->right = NULL;
	node->colour = RED;
	node->key = key;
	node->value = value;

	return node;
}

/**
 * Destroy a RB node
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \param node   The node to destroy
 */
void rbnode_destroy(parserutils_alloc alloc, void *pw, rbnode *node)
{
	alloc(node, 0, pw);
}

/**
 * Determine if a given node is red
 *
 * \param node  The node to consider
 * \return True if node is red, false otherwise
 */
bool isRed(rbnode *node)
{
	if (node == NULL)
		return false;

	return node->colour == RED;
}

/**
 * Flip the colours for a node
 *
 * \param node  The node to consider
 */
void colourFlip(rbnode *node)
{
	node->colour = !node->colour;
	if (node->left != NULL)
		node->left->colour = !node->left->colour;
	if (node->right != NULL)
		node->right->colour = !node->right->colour;
}

/**
 * Rotate a node left
 *
 * \param node  The node to rotate
 * \return The new root of the subtree
 */
rbnode *rotateLeft(rbnode *node)
{
	rbnode *r = node->right;

	node->right = r->left;
	r->left = node;
	r->colour = node->colour;
	node->colour = RED;

	return r;
}

/**
 * Rotate a node right
 *
 * \param node  The node to rotate
 * \return The new root of the subtree
 */
rbnode *rotateRight(rbnode *node)
{
	rbnode *l = node->left;

	node->left = l->right;
	l->right = node;
	l->colour = node->colour;
	node->colour = RED;

	return l;
}

/**
 * Move a red node left
 *
 * \param node  Root of subtree to manipulate
 * \return New root of subtree
 */
rbnode *moveRedLeft(rbnode *node)
{
	colourFlip(node);

	if (node->right != NULL && isRed(node->right->left)) {
		node->right = rotateRight(node->right);
		node = rotateLeft(node);
		colourFlip(node);

		/* Maintain left-leaning requirement for 3-nodes after
		 * we've just split a 4-node */
		if (node->right != NULL && isRed(node->right->right)) {
			node->right = rotateLeft(node->right);
		}
	}

	return node;
}

/**
 * Move a red node right
 *
 * \param node  Root of subtree to manipulate
 * \return New root of subtree
 */
rbnode *moveRedRight(rbnode *node)
{
	colourFlip(node);

	if (node->left != NULL && isRed(node->left->left)) {
		node = rotateRight(node);
		colourFlip(node);
	}

	return node;
}

/**
 * Fix right-leaning red links
 *
 * \param node  Root of subtree to manipulate
 * \return New root of subtree
 */
rbnode *fixUp(rbnode *node)
{
	if (node->right != NULL && isRed(node->right))
		node = rotateLeft(node);

	if (node->left != NULL && isRed(node->left)) {
		/* Two red links in a row aren't permitted */
		if (node->left->left != NULL && isRed(node->left->left)) {
			node = rotateRight(node);
		} else if (node->left->right != NULL && 
				isRed(node->left->right)) {
			node->left = rotateLeft(node->left);
			/* Force left child colour to black (it can only have 
			 * been red before). We can't use colourFlip() here, as 
			 * it will potentially break the right-hand subtree. */
			node->left->colour = BLACK;
		}
	}

	if (isRed(node->left) && isRed(node->right))
		colourFlip(node);

	return node;
}

#ifdef USE_DELETEMAX
/**
 * Delete the right-most node in a tree
 *
 * \param tree        The tree instance
 * \param node        Root of subtree to manipulate
 * \param destructor  Destructor routine, or NULL
 * \param pw          Pointer to client-specific private data
 * \return New root of subtree
 */
rbnode *deleteMax(parserutils_rbtree *tree, rbnode *node,
		parserutils_rbtree_del destructor, void *pw)
{
	if (isRed(node->left))
		node = rotateRight(node);

	if (node->right == NULL) {
		if (destructor != NULL)
			destructor(node->key, node->value, pw);
		rbnode_destroy(tree->alloc, tree->pw, node);
		return NULL;
	}

	if (!isRed(node->right) && !isRed(node->right->left))
		node = moveRedRight(node);

	assert(isRed(node->right) || isRed(node->right->left) ||
			isRed(node->right->right));

	node->right = deleteMax(tree, node->right, destructor, pw);

	return fixUp(node);
}
#else
/**
 * Delete the left-most node in a tree
 *
 * \param tree        The tree instance
 * \param node        Root of subtree to manipulate
 * \param destructor  Destructor routine, or NULL
 * \param pw          Pointer to client-specific private data
 * \return New root of subtree
 */
rbnode *deleteMin(parserutils_rbtree *tree, rbnode *node, 
		parserutils_rbtree_del destructor, void *pw)
{
	if (node->left == NULL) {
		if (destructor != NULL)
			destructor(node->key, node->value, pw);
		rbnode_destroy(tree->alloc, tree->pw, node);
		return NULL;
	}

	if (!isRed(node->left) && !isRed(node->left->left))
		node = moveRedLeft(node);

	assert(isRed(node->left) || isRed(node->left->left));

	node->left = deleteMin(tree, node->left, destructor, pw);

	return fixUp(node);
}
#endif

/**
 * Find the left-most node in a tree
 *
 * \param node  Root of the subtree to consider
 * \return Left-most node
 */
rbnode *findMin(rbnode *node)
{
	while (node->left != NULL)
		node = node->left;

	return node;
}

#ifndef NDEBUG
#ifdef RBTREE_VALIDATE
/**
 * Verify the correctness of a tree
 *
 * \param node  Root of tree to verify
 */
void verify_tree(rbnode *node)
{
	rbnode *invalid;

	if ((invalid = verify_tree_internal(node)) != NULL) {
		dump_tree_raw(node, 0);
		printf("Invalid node: %p\n", (void *) invalid);
		assert(0 && "RB invariant violated");
	}
}

/**
 * Verify the structure of an RB tree
 * 
 * \param node  Root node of tree to validate
 * \return Pointer to invalid node, or NULL
 */
rbnode *verify_tree_internal(rbnode *node)
{
	rbnode *invalid;

	if (node == NULL)
		return NULL;

	if (isRed(node) && (isRed(node->left) || isRed(node->right))) {
		return node;
	}

	if (isRed(node->right) && !isRed(node->left)) {
		return node;
	}

	if (isRed(node->left) && isRed(node->left->left)) {
		return node;
	}

	if (isRed(node->right) && isRed(node->right->right)) {
		return node;
	}

	if ((invalid = verify_tree_internal(node->left)) != NULL)
		return invalid;

	if ((invalid = verify_tree_internal(node->right)) != NULL)
		return invalid;

	return NULL;
}

/**
 * Dump a tree to stdout in raw format
 *
 * \param node   Root of tree to dump
 * \param depth  Current node depth
 */
void dump_tree_raw(rbnode *node, uint32_t depth)
{
	if (node->right)
		dump_tree_raw(node->right, depth + 1);

	for (uint32_t i = 0; i < depth; i++)
		putchar(' ');

	printf("%p %p %p [%c]\n", (void *) node, node->key, node->value,
			isRed(node) ? 'r' : 'b');

	if (node->left)
		dump_tree_raw(node->left, depth + 1);
}
#endif

/**
 * Recursively print out a pre-order traversal of a RB tree
 *
 * \param node   Root node of subtree
 * \param print  Print routine
 * \param depth  Current depth in tree
 */
void dump_tree(rbnode *node, parserutils_rbtree_print print, uint32_t depth)
{
	if (node->right)
		dump_tree(node->right, print, depth + 1);

	print(node->key, node->value, depth);

	if (node->left)
		dump_tree(node->left, print, depth + 1);
}

/**
 * Print out a RB tree
 *
 * \param tree   The tree to print
 * \param print  Print routine
 */
void parserutils_rbtree_dump(parserutils_rbtree *tree, 
		parserutils_rbtree_print print)
{
	if (tree == NULL || tree->root == NULL)
		return;

	dump_tree(tree->root, print, 0);
}
#endif

