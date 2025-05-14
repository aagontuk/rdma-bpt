#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include "bpt.h"

Node *bpt_allocate_node(struct bpt_state *state) {
  Node *newNode = (Node *)((char *)state->mem + state->offset_free);
  state->offset_free += sizeof(Node);

  return newNode;
}

// allocate and initialize a new node
Node *bpt_create_node(struct bpt_state *state, uint8_t leaf) {
    Node *x = bpt_allocate_node(state);
    
    x->n     = 0;
    x->leaf  = leaf;
    x->next  = 0;
    
    return x;
}

// search for key in the B+ tree; returns 1 if found, 0 otherwise
int bpt_search(struct bpt_state *state, uint64_t key) {
    if (!state->root) return 0;
    Node *c = state->root;
    
    // descend to leaf
    while (!c->leaf) {
        int i = 0;
        // search for the first key greater than or equal to key
        while (i < (int)c->n && key >= c->keys[i]) i++;
        c = c->children[i];
    }
    
    // linear scan in leaf
    for (int i = 0; i < (int)c->n; i++)
        if (c->keys[i] == key) return 1;
    
    return 0;
}

// split child at index i
void bpt_split_child(struct bpt_state *state, Node *parent, int i, Node *child) {
    // create new node, same leaf status as child
    Node *newNode = bpt_create_node(state, child->leaf);
    
    // new node gets TREE_ORDER keys from child
    newNode->n = TREE_ORDER;
    
    for (int j = 0; j < TREE_ORDER; j++)
        newNode->keys[j] = child->keys[j + TREE_ORDER];
    
    if (!child->leaf) {
        // internal: move TREE_ORDER + 1 children
        for (int j = 0; j < TREE_ORDER + 1; j++)
            newNode->children[j] = child->children[j + TREE_ORDER];
    } else {
        // leaf: hook into leaf‐chain
        newNode->next    = child->next;
        child->next    = newNode;
    }

    // shrink child
    child->n = TREE_ORDER + 1;

    // make room in parent
    for (int j = parent->n; j > i; j--)
        parent->children[j + 1] = parent->children[j];
    
    // Index i contains old node, place new node in i + 1
    parent->children[i + 1] = newNode;
    
    for (int j = parent->n - 1; j >= i; j--)
        parent->keys[j+1] = parent->keys[j];
    
    // median key up into parent
    parent->keys[i] = child->keys[TREE_ORDER];
    parent->n++;
}

// insert key into non‐full node
void bpt_insert_non_full(struct bpt_state *state, Node *node, uint64_t key) {
    // index of the last key in the node
    int i = node->n - 1;
    if (node->leaf) {
        // shift to make room
        while (i >= 0 && key < node->keys[i]) {
            node->keys[i+1] = node->keys[i];
            i--;
        }
        node->keys[i+1] = key;
        node->n++;
    } else {
        // descend to correct child
        while (i >= 0 && key < node->keys[i]) i--;
        i++;
        
        Node *c = node->children[i];
        if (c->n == BPT_NUM_KEYS) {
            // child full → split
            bpt_split_child(state, node, i, c);
            // decide which of the two to descend into
            if (key > node->keys[i]) i++;
        }

        // recursively insert into child
        bpt_insert_non_full(state, node->children[i], key);
    }
}

// top‐level insert
void bpt_insert(struct bpt_state *state, uint64_t key) {
    // Node *root = state->root;
    if (!state->root) {
        state->root = bpt_create_node(state, 1);
        state->root->keys[0] = key;
        state->root->n       = 1;
        return;
    }
    if (state->root->n == BPT_NUM_KEYS) {
        // root is full → grow tree in height
        Node *s = bpt_create_node(state, 0);
        s->children[0] = state->root;
        bpt_split_child(state, s, 0, state->root);
        state->root = s;
        bpt_insert_non_full(state, s, key);
    } else {
        bpt_insert_non_full(state, state->root, key);
    }
}

Node *bpt_root(struct bpt_state *state) {
  return state->root;
}

// simple traversal for debugging: print the first key of each node by level
void bpt_dump(struct bpt_state *state, Node *r, int depth) {
    if (!r) return;
    printf("%*s[", depth*2, "");
    for (int i = 0; i < r->n; i++) {
        printf("%"PRIu64, r->keys[i]);
        if (i+1<r->n) printf(",");
    }
    printf("]\n");
    if (!r->leaf) {
        for (int i = 0; i <= r->n; i++)
            bpt_dump(state, r->children[i], depth+1);
    }
}

void bpt_init(struct bpt_state *state, void *mem) {
  state->mem = mem;
  state->offset_free = 0;
  state->root = NULL;
}

int bpt_num_levels(struct bpt_state *state) {
    if (!state->root) 
        return 0;

    int levels = 0;
    Node *node = state->root;
    // descend until a leaf
    while (1) {
        levels++;
        if (node->leaf)
            break;
        // follow first child
        node = node->children[0];
    }
    return levels;
}

