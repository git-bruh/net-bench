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

  while (!G.done) {
    ssize_t bytes = CHECK_OR_ABORT(sendto(sock, buf, PACKET_SIZE, 0,
                                          (struct sockaddr *)&dest_addr,
                                          sizeof(dest_addr)));
    G.packets++;
  }

  pthread_join(thread, NULL);
  close(sock);
}