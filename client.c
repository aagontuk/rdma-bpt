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
#define BUFFER_SIZE  1024

struct client_context {
    int                 thread_id;
    struct rdma_cm_id  *id;
    struct ibv_pd      *pd;
    struct ibv_mr      *mr;
    char               *local_buf;
    struct rdma_event_channel *ec;
};

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void *run_connection(void *arg) {
    struct client_context *ctx = arg;
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
    ctx->mr = ibv_reg_mr(ctx->pd, ctx->local_buf, BUFFER_SIZE,
                         IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
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
    struct {
        uint64_t addr;
        uint32_t rkey;
    } mem_info;
    
    // memcpy(&mem_info,
    //        event->param.conn.private_data,
    //        event->param.conn.private_data_len);

    memcpy(&mem_info,
           event->param.conn.private_data,
           sizeof(mem_info));
    
    rdma_ack_cm_event(event);
    printf("Thread %d got private data: addr=%lx, rkey=%x\n",
           ctx->thread_id, mem_info.addr, mem_info.rkey);

    uint64_t root_addr = 0x7faad7d61905;
    // uint64_t key = 6;
    uint64_t key = 100000000;
    Node *c;
    int found = 0;

    printf("Thread %d posting RDMA read\n", ctx->thread_id);
    
    // traverse the B+ tree until we reach a leaf node
    while(1) {
      // post an RDMA READ
      struct ibv_sge sge = {
          .addr   = (uintptr_t)ctx->local_buf,
          .length = (uint32_t) sizeof(Node),
          .lkey   = ctx->mr->lkey
      };
      struct ibv_send_wr rd_wr = {
          .opcode      = IBV_WR_RDMA_READ,
          .wr.rdma =
              { .remote_addr = root_addr,
                .rkey        = mem_info.rkey },
          .sg_list     = &sge,
          .num_sge     = 1,
          .send_flags  = IBV_SEND_SIGNALED
      }, *bad_wr = NULL;
      
      if (ibv_post_send(ctx->id->qp, &rd_wr, &bad_wr))
          die("ibv_post_send");
      
      // wait for completion
      printf("Thread %d waiting for completion\n", ctx->thread_id);
      
      struct ibv_wc wc;
      do {
          ibv_poll_cq(ctx->id->qp->send_cq, 1, &wc);
      } while (wc.status == IBV_WC_SUCCESS && wc.opcode != IBV_WC_RDMA_READ);
      if (wc.status != IBV_WC_SUCCESS)
          die("RDMA read failed");

      c = (Node *)ctx->local_buf;
      if (c->leaf)
        break;
      
      // search for the first key greater than or equal to key
      int i = 0;
      while (i < (int)c->n && key >= c->keys[i]) i++;
      root_addr = (uint64_t)c->children[i];

      // printf("Thread %d root key0: %lu\n",
      //       ctx->thread_id, c->keys[0]);
    }

    // linear search in the leaf node
    for (int i = 0; i < (int)c->n; i++) {
        if (c->keys[i] == key) {
            printf("Thread %d found key %lu\n", ctx->thread_id, key);
            found = 1;
            break;
        }
    }

    if (!found) {
      printf("Thread %d not found key %lu\n", ctx->thread_id, key);
    }

    // teardown
    rdma_disconnect(ctx->id);
    rdma_destroy_qp(ctx->id);
    ibv_dereg_mr(ctx->mr);
    ibv_dealloc_pd(ctx->pd);
    rdma_destroy_id(ctx->id);
    rdma_destroy_event_channel(ctx->ec);
    free(ctx->local_buf);
    free(ctx);
    return NULL;
}

int main(int argc, char **argv) {
    int n = (argc>1 ? atoi(argv[1]) : 1);
    pthread_t th[n];

    for (int i = 0; i < n; i++) {
        struct client_context *ctx = calloc(1, sizeof(*ctx));
        ctx->thread_id = i;
        if (pthread_create(&th[i], NULL, run_connection, ctx))
            die("pthread_create");
    }
    for (int i = 0; i < n; i++)
        pthread_join(th[i], NULL);

    return 0;
}
