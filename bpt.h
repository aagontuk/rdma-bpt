#ifndef __BPT_H__
#define __BPT_H__

#include <inttypes.h>

#define TREE_ORDER   20
#define BPT_NUM_KEYS     (TREE_ORDER * 2)
#define BPT_NUM_OFFSETS  (TREE_ORDER * 2 + 1)

typedef struct __attribute__((packed)) Node {
    uint64_t   keys[BPT_NUM_KEYS];        // stored keys
    struct Node*   children[BPT_NUM_OFFSETS]; // offset to child nodes
    uint64_t   n;                     // current key count
    uint8_t    leaf;                  // 1 if leaf
    struct Node*   next;                  // offset to sibling for leaves
} Node;

struct bpt_state {
  void *mem;
  uint64_t offset_free;
  Node *root;
};

void bpt_init(struct bpt_state *state, void *mem);
void bpt_insert(struct bpt_state *state, uint64_t key);
void bpt_dump(struct bpt_state *state, Node *r, int depth);
int bpt_num_levels(struct bpt_state *state);
int bpt_search(struct bpt_state *state, uint64_t key);

#endif
