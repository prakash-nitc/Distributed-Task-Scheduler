/*
 * ============================================================================
 *  protocol.h — Shared Definitions for Distributed Task Scheduler
 * ============================================================================
 *
 *  This header defines:
 *    1. Constants (port, buffer sizes, max workers)
 *    2. The Worker struct used by the Master
 *    3. The wire protocol helpers: send_message() and recv_message()
 *
 *  WIRE PROTOCOL (Length-Prefix Framing):
 *  ┌──────────────────┬─────────────────────────────┐
 *  │  4 bytes (uint32) │  N bytes (payload string)   │
 *  │  Message Length   │  Message Data               │
 *  └──────────────────┴─────────────────────────────┘
 *
 *  WHY? TCP is a byte-stream protocol — it has NO concept of "messages".
 *  Without framing, recv() might return half a message or two merged messages.
 *  The 4-byte length prefix tells the receiver exactly how many bytes to read.
 *
 *  Interview Tip: "How do you handle message boundaries in TCP?"
 *  Answer: Length-prefix framing, delimiter-based framing, or fixed-size messages.
 * ============================================================================
 */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* ── Constants ─────────────────────────────────────────────────────────── */

#define PORT         8080    /* TCP port the Master listens on             */
#define MAX_WORKERS  10      /* Maximum simultaneous worker connections    */
#define BUFFER_SIZE  4096    /* Read/write buffer size                     */

/* ── Worker Struct (used by Master) ────────────────────────────────────── */

/*
 * Each connected worker is tracked with:
 *   - fd:     the socket file descriptor for communication
 *   - ip:     human-readable IP address (for logging)
 *   - isBusy: load-balancing flag (true = currently executing a task)
 *
 * Interview Q: "How does your load balancer decide which worker to use?"
 * Answer: "I iterate the worker array and pick the first one where isBusy == 0.
 *          When the worker sends back a result, I reset isBusy to 0."
 */
typedef struct {
    int  fd;                       /* Socket file descriptor               */
    char ip[INET_ADDRSTRLEN];      /* Worker's IP address (e.g. 127.0.0.1) */
    int  isBusy;                   /* 0 = free, 1 = executing a task       */
} Worker;

/* ── Protocol Helpers ──────────────────────────────────────────────────── */

/*
 * send_message():  Send a length-prefixed message over a TCP socket.
 *
 *  Step 1: Compute the message length.
 *  Step 2: Convert to network byte order (big-endian) using htonl().
 *  Step 3: Send the 4-byte length header.
 *  Step 4: Send the actual message payload.
 *
 *  Returns: 0 on success, -1 on failure.
 *
 *  Interview Q: "Why do you use htonl()?"
 *  Answer: "Different machines may use different byte orders (endianness).
 *           htonl() converts a 32-bit integer from host byte order to
 *           network byte order (big-endian), ensuring both sides agree."
 */
static inline int send_message(int fd, const char *msg) {
    uint32_t len = strlen(msg);
    uint32_t net_len = htonl(len);    /* Convert to network byte order */

    /* Send the 4-byte length header */
    if (send(fd, &net_len, sizeof(net_len), 0) != sizeof(net_len)) {
        perror("send_message: failed to send length");
        return -1;
    }

    /* Send the message payload */
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t sent = send(fd, msg + total_sent, len - total_sent, 0);
        if (sent <= 0) {
            perror("send_message: failed to send payload");
            return -1;
        }
        total_sent += sent;
    }

    return 0;
}

/*
 * recv_message():  Receive a length-prefixed message from a TCP socket.
 *
 *  Step 1: Read the 4-byte length header.
 *  Step 2: Convert from network byte order to host byte order using ntohl().
 *  Step 3: Read exactly 'length' bytes of payload.
 *  Step 4: Null-terminate the buffer.
 *
 *  Returns: number of payload bytes received, or -1 on failure, or 0 on disconnect.
 *
 *  Interview Q: "What if recv() returns fewer bytes than expected?"
 *  Answer: "TCP can fragment data. I loop until I've received the full
 *           expected number of bytes. This is called a 'read-exactly' loop."
 */
static inline int recv_message(int fd, char *buf, int bufsize) {
    uint32_t net_len;

    /* Step 1: Read the 4-byte length header */
    ssize_t r = recv(fd, &net_len, sizeof(net_len), MSG_WAITALL);
    if (r <= 0) {
        return r;   /* 0 = disconnect, -1 = error */
    }

    /* Step 2: Convert to host byte order */
    uint32_t len = ntohl(net_len);

    /* Safety check: don't overflow our buffer */
    if (len >= (uint32_t)bufsize) {
        fprintf(stderr, "recv_message: message too large (%u bytes)\n", len);
        return -1;
    }

    /* Step 3: Read exactly 'len' bytes (read-exactly loop) */
    size_t total_read = 0;
    while (total_read < len) {
        ssize_t n = recv(fd, buf + total_read, len - total_read, 0);
        if (n <= 0) {
            return n;
        }
        total_read += n;
    }

    /* Step 4: Null-terminate */
    buf[len] = '\0';

    return (int)len;
}

#endif /* PROTOCOL_H */
