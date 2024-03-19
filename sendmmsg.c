#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK_OR_ABORT(expr)                                                   \
  ({                                                                           \
    __typeof__((expr)) r = (expr);                                             \
    if (r < 0) {                                                               \
      fprintf(stderr, "%s:%d (%s): %s\n", __FILE__, __LINE__, __FUNCTION__,    \
              strerror(errno));                                                \
      abort();                                                                 \
    }                                                                          \
    r;                                                                         \
  })

enum {
  ITERATIONS = 10,
  ITERATION_MS = 1000,
  PACKET_SIZE = 1024,
  MSGVECS = 1024,
  ZEROCOPY = 0,
};

struct {
  _Atomic bool done;
  _Atomic size_t packets;
} G = {};

static void *stats_thread(void *) {
  size_t prev_packets = G.packets;

  for (size_t i = 0; i < ITERATIONS; i++) {
    poll(NULL, 0, ITERATION_MS);

    size_t delta = G.packets - prev_packets;
    printf("%zu pkt/s, %zu MiB/s\n", delta,
           (delta * PACKET_SIZE) / (1024 * 1024));
    prev_packets += delta;
  }

  G.done = true;

  pthread_exit(NULL);
}

int main(void) {
  char buf[PACKET_SIZE];

  int sock = CHECK_OR_ABORT(socket(AF_INET, SOCK_DGRAM, 0));

  if (ZEROCOPY) {
    CHECK_OR_ABORT(
        setsockopt(sock, SOL_SOCKET, SO_ZEROCOPY, &(int){1}, sizeof(int)));
  }

  // Bind to random port
  CHECK_OR_ABORT(bind(
      sock, (struct sockaddr *)&(struct sockaddr_in){.sin_family = AF_INET},
      sizeof(struct sockaddr_in)));

  struct sockaddr_in dest_addr = {
      .sin_family = AF_INET,
      .sin_port = htons(6666),
  };

  CHECK_OR_ABORT(inet_pton(AF_INET, "127.0.0.1", &dest_addr.sin_addr));

  pthread_t thread;
  CHECK_OR_ABORT(pthread_create(&thread, NULL, stats_thread, NULL));

  struct mmsghdr hdrs[MSGVECS] = {};
  struct iovec iovec = {
      .iov_base = buf,
      .iov_len = PACKET_SIZE,
  };

  for (size_t i = 0; i < MSGVECS; i++) {
    hdrs[i].msg_hdr = (struct msghdr){
        .msg_name = &dest_addr,
        .msg_namelen = sizeof(dest_addr),
        .msg_iov = &iovec,
        .msg_iovlen = 1,
    };
  }

  while (!G.done) {
    int n = CHECK_OR_ABORT(sendmmsg(sock, hdrs, MSGVECS, ZEROCOPY));

    if (n != MSGVECS) {
      fprintf(stderr, "Expected %d, got %d\n", MSGVECS, n);
      abort();
    }

    G.packets += MSGVECS;
  }

  pthread_join(thread, NULL);
  close(sock);
}