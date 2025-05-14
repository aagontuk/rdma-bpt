#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bpt.h"

int main(void) {
    // Memory region for the B+ tree
    void *mem = malloc(10UL * 1024 * 1024 * 1024);
    if (!mem) { perror("malloc"); exit(1); }
    memset(mem, 0, 10UL*1024*1024*1024);

    struct bpt_state *state = malloc(sizeof(*state));
    if (!state) { perror("malloc"); exit(1); }
    memset(state, 0, sizeof(*state));
    bpt_init(state, mem);

    srand(3163);

    for (int i = 0; i < 10000000; i++) {
        uint64_t key = rand() % 10000000;
        bpt_insert(state, key);
    }

    // uint64_t data[] = {10, 20, 5, 6, 12, 30, 7, 17};
    // for (int i = 0; i < sizeof(data)/sizeof(*data); i++)
    //     bpt_insert(state, data[i]);
    // bpt_dump(state, state->root, 0);

    printf("search 6 → %s\n", bpt_search(state, 6) ? "found" : "not found");
    printf("search 15 → %s\n", bpt_search(state, 15)? "found" : "not found");
    printf("search 100000000 → %s\n", bpt_search(state, 100000000)? "found" : "not found");

    printf("num levels: %d\n", bpt_num_levels(state)); 

    return 0;
}
