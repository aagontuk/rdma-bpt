// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>
#include "bpt.h"

#define SERVER_IP    "10.10.1.1"
#define SERVER_PORT  "20079"
#define BUFFER_SIZE  8192
#define MAX_THREADS  16
#define BENCH_TIME 2

struct mem_info {
    uint64_t addr;
    uint32_t rkey;
};

struct client_context {
    int                 thread_id;
    struct rdma_cm_id  *id;
    struct ibv_pd      *pd;
    struct ibv_mr      *mr;
    char               *local_buf;
    struct rdma_event_channel *ec;
    struct mem_info mem_info;
    double cycles;
};

struct stats {
  uint64_t num_ops;
} __attribute__((aligned(64)));

struct stats stats[MAX_THREADS];

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

// read the 64-bit time-stamp counter
static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

double get_tsc_freq(int sleep_ms) {
    struct timespec t0, t1, req;
    uint64_t c0, c1;

    // get start wall-clock and TSC
    clock_gettime(CLOCK_MONOTONIC_RAW, &t0);
    c0 = rdtsc();

    // sleep for sleep_ms milliseconds
    req.tv_sec  = sleep_ms / 1000;
    req.tv_nsec = (sleep_ms % 1000) * 1000000;
    nanosleep(&req, NULL);

    // get end wall-clock and TSC
    clock_gettime(CLOCK_MONOTONIC_RAW, &t1);
    c1 = rdtsc();

    // compute elapsed seconds
    double dt = (t1.tv_sec  - t0.tv_sec)
              + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    // rate = cycles / second
    return (double)(c1 - c0) / dt;
}

void connect_client(struct client_context *ctx) {
    struct rdma_cm_event *event;
    struct rdma_conn_param conn_param = { 0 };

    // create ID & event channel
    printf("Creating event channel for thread %d\n", ctx->thread_id);
    ctx->ec = rdma_create_event_channel();
    if (!ctx->ec) die("rdma_create_event_channel");
    if (rdma_create_id(ctx->ec, &ctx->id, ctx, RDMA_PS_TCP))
        die("rdma_create_id");

    // resolve addr
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port   = htons(atoi(SERVER_PORT))
    };
    inet_pton(AF_INET, SERVER_IP, &srv.sin_addr);

    printf("Resolving address for thread %d\n", ctx->thread_id);
    if (rdma_resolve_addr(ctx->id, NULL,
                         (struct sockaddr*)&srv, 2000))
        die("rdma_resolve_addr");
    rdma_get_cm_event(ctx->ec, &event);
    rdma_ack_cm_event(event);

    // resolve route
    printf("Resolving route for thread %d\n", ctx->thread_id);
    if (rdma_resolve_route(ctx->id, 2000))
        die("rdma_resolve_route");
    rdma_get_cm_event(ctx->ec, &event);
    rdma_ack_cm_event(event);

    // alloc PD & MR
    ctx->pd = ibv_alloc_pd(ctx->id->verbs);
    if (!ctx->pd) die("ibv_alloc_pd");
    ctx->local_buf = malloc(BUFFER_SIZE);
    if (!ctx->local_buf) die("malloc");
    memset(ctx->local_buf, 0, BUFFER_SIZE);
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->local_buf, BUFFER_SIZE,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!ctx->mr) die("ibv_reg_mr");

    // create QP
    printf("Creating QP for thread %d\n", ctx->thread_id);
    struct ibv_qp_init_attr qp_attr = {
        .cap        = { .max_send_wr  = 8,
                        .max_recv_wr  = 8,
                        .max_send_sge = 2,
                        .max_recv_sge = 2 },
        .sq_sig_all = 1,
        .qp_type    = IBV_QPT_RC
    };

    if (rdma_create_qp(ctx->id, ctx->pd, &qp_attr))
        die("rdma_create_qp");

    // connect
    printf("Connecting for thread %d\n", ctx->thread_id);
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 7;
    if (rdma_connect(ctx->id, &conn_param))
        die("rdma_connect");
    
    rdma_get_cm_event(ctx->ec, &event);
    if (event->event != RDMA_CM_EVENT_ESTABLISHED)
        die("expected ESTABLISHED");

    printf("Thread %d connected\n", ctx->thread_id);

    printf("Thread %d private data len: %d\n",
           ctx->thread_id, event->param.conn.private_data_len);
    
    // pull down MR info
    // struct {
    //     uint64_t addr;
    //     uint32_t rkey;
    // } mem_info;
    
    // memcpy(&mem_info,
    //        event->param.conn.private_data,
    //        event->param.conn.private_data_len);

    memcpy(&ctx->mem_info,
           event->param.conn.private_data,
           sizeof(struct mem_info));
    
    rdma_ack_cm_event(event);
    printf("Thread %d got private data: addr=%lx, rkey=%x\n",
           ctx->thread_id, ctx->mem_info.addr, ctx->mem_info.rkey);
}

void disconnect_client(struct client_context *ctx) {
    rdma_disconnect(ctx->id);
    rdma_destroy_qp(ctx->id);
    ibv_dereg_mr(ctx->mr);
    ibv_dealloc_pd(ctx->pd);
    rdma_destroy_id(ctx->id);
    rdma_destroy_event_channel(ctx->ec);
    free(ctx->local_buf);
    free(ctx);
}

void *run_connection(void *arg) {
    struct client_context *ctx = (struct client_context *)arg;
    connect_client(ctx);

    // printf("Thread %d posting RDMA read\n", ctx->thread_id);
    // uint64_t start = rdtsc();
    // uint64_t diff = 0;
    struct ibv_send_wr rd_wr, *bad_wr = NULL;

    // while(diff < ctx->cycles) {
    uint64_t node_next = 0x7fe0ccc00905;
    uint64_t key = 6;
    // uint64_t key = 100000000;
    Node *c;
    int found = 0;
    
    struct ibv_sge sge = {
        .addr   = (uint64_t)ctx->local_buf,
        .length = (uint32_t) sizeof(Node),
        .lkey   = ctx->mr->lkey
    };

    // traverse the B+ tree until we reach a leaf node
    int req_num = 0;
    while(1) {
      bzero(ctx->local_buf, BUFFER_SIZE);
      bzero(&rd_wr, sizeof(rd_wr));
      uint64_t wr_id = ctx->thread_id * 10 + req_num++;
      
      rd_wr.wr_id      = wr_id;
      rd_wr.opcode      = IBV_WR_RDMA_READ;
      rd_wr.wr.rdma.remote_addr = node_next;
      rd_wr.wr.rdma.rkey        = ctx->mem_info.rkey;
      rd_wr.sg_list     = &sge;
      rd_wr.num_sge     = 1;
      rd_wr.send_flags  = IBV_SEND_SIGNALED;

      // printf("Thread %d posting RDMA read, wr_id=%lu, addr=0x%lx\n",
            // ctx->thread_id, wr_id, node_next);
      if (ibv_post_send(ctx->id->qp, &rd_wr, &bad_wr))
          die("ibv_post_send");
      
      struct ibv_wc wc;
      while(1) {
          ibv_poll_cq(ctx->id->qp->send_cq, 1, &wc);
          if (wc.opcode == IBV_WC_RDMA_READ && wc.status == IBV_WC_SUCCESS && wc.wr_id == wr_id)
              break;
          // printf("Thread %d polling CQ, wr_id=%lu, opcode=%d\n",
                // ctx->thread_id, wc.wr_id, wc.opcode);
      }
      
      if (wc.status != IBV_WC_SUCCESS)
          die("RDMA read failed");

      c = (Node *)ctx->local_buf;

      // printf("Thread %d read node at 0x%lx, n=%lu, leaf=%d, key0=%lu\n",
            // ctx->thread_id, node_next, c->n, c->leaf, c->keys[0]);
      
      if (c->leaf) {
        printf("Thread %d found leaf node at 0x%lx\n", ctx->thread_id, node_next);
        break;
      }
      
      // search for the first key greater than or equal to key
      int i = 0;
      while (i < c->n && key >= c->keys[i]) {
        i++;
      }
      
      node_next = (uint64_t)c->children[i];
      // printf("Thread %d next node: 0x%lx\n", ctx->thread_id, node_next);
      // printf("Thread %d root addr: 0x%lx, key0: %lu\n",
            // ctx->thread_id, node_next, c->keys[0]);
    }

    // linear search in the leaf node
    for (int i = 0; i < (int)c->n; i++) {
        if (c->keys[i] == key) {
            // printf("Thread %d found key %lu\n", ctx->thread_id, key);
            // found = 1;
            break;
        }
    }

    // if (!found) {
    //     printf("thread %d not found key %lu\n", ctx->thread_id, key);
    // }
    //
        // stats[ctx->thread_id].num_ops++;
        // diff = rdtsc() - start;
    // }
    
    // teardown
    disconnect_client(ctx);
    return NULL;
}

int main(int argc, char **argv) {
    int n = (argc>1 ? atoi(argv[1]) : 1);
    pthread_t th[n];

    double cycles = get_tsc_freq(1000) * BENCH_TIME;

    for (int i = 0; i < n; i++) {
        struct client_context *ctx = calloc(1, sizeof(*ctx));
        ctx->thread_id = i;
        ctx->cycles = cycles;
        if (pthread_create(&th[i], NULL, run_connection, ctx))
            die("pthread_create");
    }
    for (int i = 0; i < n; i++) {
        pthread_join(th[i], NULL);
        printf("Thread %d: %lu ops\n", i, stats[i].num_ops);
    }

    return 0;
}
