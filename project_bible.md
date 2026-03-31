# 📘 Distributed Task Scheduler — Project Bible (Interview Preparation)

> **Purpose of this document**: A complete reference covering every concept, system call, design decision, and potential interview question related to this project. Read this cover-to-cover before your interview.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architecture & Data Flow](#2-architecture--data-flow)
3. [The Wire Protocol (Length-Prefix Framing)](#3-the-wire-protocol-length-prefix-framing)
4. [The Networking Layer — Socket Programming](#4-the-networking-layer--socket-programming)
5. [I/O Multiplexing with select()](#5-io-multiplexing-with-select)
6. [Process Creation — fork()](#6-process-creation--fork)
7. [Process Execution — The exec() Family](#7-process-execution--the-exec-family)
8. [File Descriptor Redirection — dup2()](#8-file-descriptor-redirection--dup2)
9. [Inter-Process Communication — pipe()](#9-inter-process-communication--pipe)
10. [Waiting for Children — waitpid()](#10-waiting-for-children--waitpid)
11. [Load Balancing](#11-load-balancing)
12. [Code Walkthrough](#12-code-walkthrough)
13. [Error Handling & Edge Cases](#13-error-handling--edge-cases)
14. [How to Build and Run](#14-how-to-build-and-run)
15. [Potential Interview Questions (35+ Q&A)](#15-potential-interview-questions-35-qa)
16. [How to Extend This Project](#16-how-to-extend-this-project)
17. [Glossary](#17-glossary)

---

## 1. Project Overview

### What is this project?

A **Distributed Task Scheduler** built in C using the **Master-Worker model**. The system distributes shell commands across multiple Worker processes that can run on the same machine or across a network.

### What does it demonstrate?

| Concept | Where it's used |
|---|---|
| TCP Socket Programming | Master (server) and Worker (client) communication |
| Process Creation (fork) | Worker creates child processes to execute commands |
| Process Execution (exec) | Child process runs the shell command |
| I/O Redirection (dup2) | Captures command output by redirecting stdout to a pipe |
| Inter-Process Communication (pipe) | Sends command output from child to parent |
| Load Balancing | Master assigns tasks to the first available worker |
| I/O Multiplexing (select) | Master monitors multiple connections without threads |

### The Components

```
┌─────────────────────────────────────────────────────────────────┐
│                         MASTER NODE                             │
│                                                                 │
│  ┌──────────────┐  ┌────────────────┐  ┌─────────────────────┐  │
│  │ TCP Server   │  │ Worker         │  │ Load Balancer       │  │
│  │ Socket       │  │ Registry       │  │ (First-Available)   │  │
│  │              │  │ [fd, ip, busy] │  │                     │  │
│  └──────────────┘  └────────────────┘  └─────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ select() Event Loop                                      │   │
│  │  Monitors: stdin + server socket + all worker sockets    │   │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
          │                                   │
          │ TCP Connection                    │ TCP Connection
          ▼                                   ▼
┌─────────────────────┐          ┌─────────────────────┐
│    WORKER NODE #1    │          │    WORKER NODE #2    │
│                      │          │                      │
│ ┌──────────────────┐ │          │ ┌──────────────────┐ │
│ │ connect()        │ │          │ │ connect()        │ │
│ │ recv command     │ │          │ │ recv command     │ │
│ │ fork() → child   │ │          │ │ fork() → child   │ │
│ │   dup2() stdout  │ │          │ │   dup2() stdout  │ │
│ │   execvp() cmd   │ │          │ │   execvp() cmd   │ │
│ │ waitpid()        │ │          │ │ waitpid()        │ │
│ │ send result      │ │          │ │ send result      │ │
│ └──────────────────┘ │          │ └──────────────────┘ │
└─────────────────────┘          └─────────────────────┘
```

---

## 2. Architecture & Data Flow

### End-to-End Flow of a Task

```
STEP 1: Operator types "uptime" in the Master's terminal
         │
STEP 2: Master's select() detects stdin is readable
         │
STEP 3: Master reads "uptime" from stdin
         │
STEP 4: Load Balancer finds Worker #0 (isBusy == false)
         │
STEP 5: Master sends: [4 bytes: length=6] + [6 bytes: "uptime"]
         │  (Length-prefix protocol over TCP)
         │
STEP 6: Worker #0 receives the message
         │
STEP 7: Worker calls pipe() to create a data channel
         │
STEP 8: Worker calls fork() to create a child process
         │
STEP 9: CHILD: dup2() redirects stdout/stderr → pipe
         │      execvp("uptime", ["uptime", NULL])
         │      → Child IS NOW the 'uptime' program
         │      → Prints " 10:23:45 up 5 days, ..." into the pipe
         │
STEP 10: PARENT (Worker): reads output from pipe
          │                waitpid() to reap child + get exit code
          │
STEP 11: Worker sends result back to Master via TCP
          │
STEP 12: Master's select() detects worker fd is readable
          │
STEP 13: Master reads the result, prints it, marks Worker #0 as free
```

---

## 3. The Wire Protocol (Length-Prefix Framing)

### The Problem

TCP is a **byte-stream** protocol. Unlike UDP (which preserves message boundaries), TCP treats all data as a continuous stream of bytes. This means:

- `send("Hello")` followed by `send("World")` might arrive as `"HelloWorld"` in a single `recv()`.
- A single `send("Hello World")` might arrive as `"Hello"` and `" World"` in two separate `recv()` calls.

### The Solution: Length-Prefix Framing

Before every message, we send a **4-byte integer** representing the message length:

```
┌──────────────────┬─────────────────────────────┐
│ 4 bytes (uint32)  │  N bytes (payload)          │
│ Message Length    │  Message Data               │
│ (network order)  │  (UTF-8 string)             │
└──────────────────┴─────────────────────────────┘

Example: Sending "uptime"
  [0x00 0x00 0x00 0x06] [0x75 0x70 0x74 0x69 0x6D 0x65]
  ---- length = 6 ----  ---------- "uptime" -----------
```

### Why network byte order (Big-Endian)?

Different CPUs store multi-byte integers differently:
- **Big-Endian**: Most significant byte first (0x00 0x00 0x00 0x06) — used by network protocols
- **Little-Endian**: Least significant byte first (0x06 0x00 0x00 0x00) — used by x86 Intel/AMD CPUs

We use `htonl()` (Host TO Network Long) to convert before sending and `ntohl()` (Network TO Host Long) when receiving. This ensures Master and Worker can communicate even if running on different CPU architectures.

### Implementation (in protocol.h)

```c
// Sending
uint32_t len = strlen(msg);
uint32_t net_len = htonl(len);       // Convert to big-endian
send(fd, &net_len, 4, 0);           // Send 4-byte length
send(fd, msg, len, 0);              // Send payload

// Receiving
uint32_t net_len;
recv(fd, &net_len, 4, MSG_WAITALL); // Read 4-byte length
uint32_t len = ntohl(net_len);       // Convert to host order
recv(fd, buf, len, 0);              // Read exactly 'len' bytes
buf[len] = '\0';                     // Null-terminate
```

### Interview Q&A

**Q: "Why not just use a delimiter like newline (\\n) to separate messages?"**
A: Delimiter-based framing works for text protocols (like HTTP/1.1 headers), but has drawbacks:
- The delimiter character cannot appear in the payload (or needs escaping).
- You have to scan every byte for the delimiter.
- Length-prefix is O(1) to determine message length; delimiter scanning is O(n).

**Q: "What if the length field itself gets corrupted or is a huge number?"**
A: We add a safety check: if `len >= BUFFER_SIZE`, we reject the message. In production, you'd also add checksums (CRC) and a maximum message size limit.

---

## 4. The Networking Layer — Socket Programming

### What is a Socket?

A socket is an **endpoint for communication**. Think of it as a mailbox:
- You create it (`socket()`).
- You assign it an address (`bind()`).
- You use it to send/receive data.

In UNIX, **everything is a file**. A socket is represented as a **file descriptor** (an integer), just like files, pipes, and stdin/stdout.

### The Berkeley Sockets API

This is the standard API for network programming, defined in the POSIX standard.

#### Server Side (Master)

```
socket()  ──────► Create a TCP socket (returns fd)
    │
bind()    ──────► Attach to IP address + port number
    │
listen()  ──────► Mark as passive (ready to accept)
    │
accept()  ──────► Block until a client connects
    │               Returns a NEW fd for that client
    │
recv()/send() ──► Communicate with the specific client
    │
close()   ──────► Terminate the connection
```

#### Client Side (Worker)

```
socket()  ──────► Create a TCP socket (returns fd)
    │
connect() ──────► Connect to server's IP:port
    │               (triggers TCP 3-way handshake)
    │
recv()/send() ──► Communicate with the server
    │
close()   ──────► Terminate the connection
```

### Key System Calls Explained

#### socket(AF_INET, SOCK_STREAM, 0)

```c
int fd = socket(AF_INET, SOCK_STREAM, 0);
```

| Parameter | Meaning |
|---|---|
| AF_INET | IPv4 address family |
| SOCK_STREAM | TCP (reliable, ordered, connection-oriented) |
| 0 | Default protocol for the given type (TCP for SOCK_STREAM) |

**Returns**: A file descriptor (integer ≥ 0) on success, -1 on error.

#### bind(fd, address, length)

Assigns a local address (IP + port) to the socket.

```c
struct sockaddr_in addr;
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;     // Listen on ALL interfaces
addr.sin_port        = htons(8080);    // Port 8080 in network byte order

bind(fd, (struct sockaddr *)&addr, sizeof(addr));
```

**`INADDR_ANY`** means "bind to all available network interfaces" — so the server accepts connections on any of its IP addresses (127.0.0.1, 192.168.x.x, etc.).

#### listen(fd, backlog)

Marks the socket as **passive** (it can now accept connections).

The `backlog` parameter (we use 5) specifies how many pending connections the kernel queues before rejecting new ones. This is NOT the max number of total connections — it's the queue for connections that haven't been `accept()`ed yet.

#### accept(fd, ...)

**Blocks** until a client calls `connect()`. Returns a **brand new** file descriptor dedicated to that specific client connection.

**Critical distinction**:
- The **listening socket** keeps accepting new connections (like a receptionist).
- Each `accept()` returns a **new socket** for the established connection (like a dedicated phone line).

#### connect(fd, address, length)

Client-side: initiates a connection to the server. This triggers the **TCP 3-Way Handshake**:

```
Client              Server
  │── SYN ────────────►│   "I want to connect"
  │◄── SYN-ACK ────── │   "OK, I acknowledge"
  │── ACK ────────────►│   "Connection established"
```

### setsockopt(SO_REUSEADDR) — Why?

Without this, if the Master crashes and restarts quickly, `bind()` fails with "Address already in use." This happens because the OS keeps the port in `TIME_WAIT` state (a TCP mechanism to ensure all in-flight packets are delivered). `SO_REUSEADDR` allows binding to a port in `TIME_WAIT`.

### TCP vs UDP — Interview Comparison

| Feature | TCP (SOCK_STREAM) | UDP (SOCK_DGRAM) |
|---|---|---|
| Connection | Connection-oriented (3-way handshake) | Connectionless |
| Reliability | Guaranteed delivery (ACKs + retransmission) | No guarantees |
| Ordering | In-order delivery | No ordering |
| Message boundaries | NO (byte stream) | YES (datagrams) |
| Speed | Slower (overhead) | Faster |
| Use case | File transfer, HTTP, our scheduler | DNS, video streaming, gaming |

**Why TCP for this project?** We CANNOT afford to lose a task. TCP guarantees every command reaches the Worker and every result reaches the Master.

---

## 5. I/O Multiplexing with select()

### The Problem

The Master needs to monitor **multiple things at once**:
1. Stdin — Is the operator typing a new command?
2. Server socket — Is a new Worker trying to connect?
3. Worker sockets — Has any Worker sent back a result?

Without multiplexing, we'd need **blocking reads** on each, which means we can't respond to the others.

### The Solution: select()

`select()` monitors multiple file descriptors and **blocks until at least one is ready** for I/O.

```c
fd_set read_fds;                    // A bitmask of fds to watch
FD_ZERO(&read_fds);                // Clear the set
FD_SET(STDIN_FILENO, &read_fds);   // Add stdin (fd 0)
FD_SET(server_fd, &read_fds);      // Add listening socket
FD_SET(worker_fd, &read_fds);      // Add a worker socket

// Block until something is ready
select(max_fd + 1, &read_fds, NULL, NULL, NULL);

// Check what's ready
if (FD_ISSET(server_fd, &read_fds)) {
    // A new worker is connecting!
}
if (FD_ISSET(STDIN_FILENO, &read_fds)) {
    // Operator typed a new command!
}
```

### How select() Works Internally

1. You build an `fd_set` (a bitmap — each bit represents one fd).
2. You call `select()` — the kernel monitors those fds.
3. When any fd has data available, `select()` returns and **modifies** the fd_set to contain only the ready fds.
4. You check each fd with `FD_ISSET()`.

### Why select() Over Threads?

| Approach | Pros | Cons |
|---|---|---|
| **select()** | No thread sync issues, simple control flow, portable | O(n) scan of fd_set, FD_SETSIZE limit (1024) |
| **Threads** | True parallelism, simpler per-connection code | Race conditions, mutex overhead, complex debugging |
| **epoll()** (Linux) | O(1) event notification, scales to 100k+ fds | Linux-only, more complex API |

For our small number of connections, `select()` is perfect and avoids the complexity of multi-threading.

### The max_fd + 1 Parameter

`select()` takes `nfds` as its first argument: the highest-numbered fd **plus one**. This tells the kernel how many bits in the fd_set to check. We track this by updating `max_fd` every time we add an fd.

---

## 6. Process Creation — fork()

### What fork() Does

`fork()` creates a **new process** by duplicating the calling process.

```c
pid_t pid = fork();
// After this line, there are TWO processes running:
//   Parent: pid > 0 (the child's process ID)
//   Child:  pid == 0
```

### What Gets Copied?

| Copied | NOT Copied |
|---|---|
| Code (text segment) | Process ID (child gets a new PID) |
| Data segment | Parent PID (child's PPID = parent's PID) |
| Heap | Locks held by threads |
| Stack | Pending signals |
| File descriptors (including sockets!) | |
| Environment variables | |

### Copy-On-Write (COW) — Interview Favorite!

**Q: "Doesn't copying everything make fork() slow?"**

**A:** No! Modern kernels use **Copy-On-Write (COW)**:
1. After `fork()`, parent and child **share** the same physical memory pages.
2. Both pages are marked as **read-only** in the page table.
3. When either process tries to **write** to a page, a **page fault** occurs.
4. The kernel then creates a **copy** of just that one page.
5. This means: if the child immediately calls `exec()`, almost NO copying happens!

```
Before fork():
  Parent: [Page A] [Page B] [Page C]

After fork() (COW):
  Parent: [Page A] [Page B] [Page C]   ← shared, read-only
  Child:      ↑        ↑        ↑

After child writes to Page B:
  Parent: [Page A] [Page B ] [Page C]   ← original
  Child:      ↑    [Page B'] ← copy       ↑
```

### Why We Use fork() in This Project

The Worker needs to execute arbitrary shell commands. We can't just call the command directly because:
1. `execvp()` **replaces** the current process. Without `fork()`, the Worker itself would become the command and stop being a Worker!
2. `fork()` creates a **disposable child** that can be replaced by `execvp()` while the parent Worker continues running.

### Zombie Processes

**Q: "What is a zombie process?"**

A **zombie** is a process that has terminated but whose parent hasn't read its exit status via `waitpid()`. It takes up an entry in the process table but consumes no CPU or memory (except the table slot).

```
[Worker] ─── fork() ───► [Child: running "uptime"]
                                    │
                              child finishes
                                    │
                              [Child: ZOMBIE]  ← until waitpid()
                                    │
              ◄── waitpid() ───────┘
                              [Child: REMOVED from process table]
```

**Fix:** Always call `waitpid()` after `fork()`. That's exactly what we do.

### Orphan Processes

If the **parent** dies before the child, the child becomes an **orphan**. The init process (PID 1) adopts it and will call `waitpid()` when it terminates.

---

## 7. Process Execution — The exec() Family

### What exec() Does

`exec()` **replaces** the current process's memory with a new program. The PID stays the same. After a successful `exec()`, the old code is **gone forever** — `exec()` never returns on success.

```c
execvp("ls", argv);
// If we reach this line, execvp FAILED
perror("execvp failed");
```

### The exec() Variants — Interview Table

| Function | Arguments | PATH search? | Environment? |
|---|---|---|---|
| `execl()` | list: `execl("/bin/ls", "ls", "-l", NULL)` | No (need full path) | Inherited |
| `execlp()` | list | **Yes** | Inherited |
| `execle()` | list + envp | No | Custom |
| `execv()` | array: `execv("/bin/ls", argv)` | No | Inherited |
| **`execvp()`** | **array** | **Yes** | **Inherited** |
| `execve()` | array + envp | No | Custom |

**Memory aid:**
- `l` = **l**ist (args passed individually)
- `v` = **v**ector (args passed as an array)
- `p` = searches **P**ATH
- `e` = custom **e**nvironment

### Why execvp() in Our Project?

1. **`v` (vector)**: We tokenize the command string into an argv array, so we need the vector variant.
2. **`p` (PATH search)**: Users type `"uptime"`, not `"/usr/bin/uptime"`. The `p` variant searches the system PATH for the binary.

### What Happens Internally

```
Before execvp("ls", argv):
  ┌─────────────────────────┐
  │ Process PID=1234        │
  │ Code: worker.c code     │
  │ Data: worker's variables│
  │ Stack: worker's stack   │
  └─────────────────────────┘

After execvp("ls", argv):
  ┌─────────────────────────┐
  │ Process PID=1234  ← same PID! │
  │ Code: /usr/bin/ls code  │
  │ Data: ls's variables    │
  │ Stack: ls's stack       │
  └─────────────────────────┘
```

### _exit() vs exit()

In the child process, if `execvp()` fails, we use `_exit(127)` instead of `exit(0)`:

- `exit()` flushes stdio buffers (shared with the parent), which could corrupt output.
- `_exit()` terminates immediately without flushing buffers.
- Exit code 127 is the convention for "command not found" (same as bash).

---

## 8. File Descriptor Redirection — dup2()

### The File Descriptor Table

Every process has a **file descriptor table** — an array where each index (0, 1, 2, ...) points to a kernel file object:

```
FD Table (default):
  fd 0 → stdin  (keyboard)
  fd 1 → stdout (terminal)
  fd 2 → stderr (terminal)
  fd 3 → (our socket to Master)
  fd 4 → pipe_fd[0] (pipe read end)
  fd 5 → pipe_fd[1] (pipe write end)
```

### What dup2() Does

`dup2(old_fd, new_fd)`:
1. Closes `new_fd` if it's open.
2. Makes `new_fd` point to the **same kernel file object** as `old_fd`.

```c
dup2(pipe_fd[1], STDOUT_FILENO);   // STDOUT_FILENO = 1
```

**After this call:**

```
FD Table:
  fd 0 → stdin  (keyboard)
  fd 1 → pipe_fd[1] (pipe write end)  ← CHANGED!
  fd 2 → stderr (terminal)
```

Now, **anything the child process prints to stdout goes into the pipe** instead of the terminal!

### Why This Matters for Our Project

When the Worker executes `uptime` via `execvp()`, the `uptime` program thinks it's writing to the terminal (stdout = fd 1). But we've **redirected** fd 1 to our pipe. So the output flows:

```
uptime → writes to fd 1 → BUT fd 1 is now the pipe → Parent reads from pipe
```

This is the same mechanism that the shell uses for `|` (pipes):
```bash
ls -la | grep ".c"
# The shell does: fork, dup2(pipe, stdout) in ls, dup2(pipe, stdin) in grep
```

---

## 9. Inter-Process Communication — pipe()

### What pipe() Creates

`pipe()` creates a **unidirectional data channel**:

```c
int pipe_fd[2];
pipe(pipe_fd);
// pipe_fd[0] = READ end  (data comes OUT of this end)
// pipe_fd[1] = WRITE end (data goes INTO this end)
```

Think of it as a literal pipe:
```
WRITE end ──────► [  pipe buffer (kernel)  ] ──────► READ end
pipe_fd[1]                                          pipe_fd[0]
```

### Using Pipes with fork()

After `fork()`, **both** the parent and child have copies of the pipe file descriptors. The key rule: **close the ends you don't use**.

```
Before fork():
  Worker: pipe_fd[0]=read, pipe_fd[1]=write

After fork():
  Parent (Worker): pipe_fd[0]=read  ← KEEP THIS
                   pipe_fd[1]=write ← CLOSE THIS

  Child (Command): pipe_fd[0]=read  ← CLOSE THIS
                   pipe_fd[1]=write ← REDIRECT stdout TO THIS, THEN CLOSE
```

### Why Close Unused Ends?

1. **EOF detection**: The read end gets EOF (returns 0) only when ALL write ends are closed. If the parent keeps the write end open, read() will never return EOF.
2. **Resource management**: File descriptors are a limited resource.

### Pipe Buffer Size

The kernel maintains a buffer for each pipe (typically 64KB on Linux). If the buffer is full, `write()` blocks. If the buffer is empty, `read()` blocks. This naturally synchronizes the producer and consumer.

---

## 10. Waiting for Children — waitpid()

### What waitpid() Does

```c
int status;
pid_t result = waitpid(pid, &status, 0);
```

`waitpid(pid, &status, 0)`:
- **Blocks** until child `pid` terminates.
- Stores the exit information in `status`.
- **Reaps** the zombie process (removes it from the process table).

### Decoding the Status

The `status` integer is encoded with bit manipulation. Use macros to decode:

```c
if (WIFEXITED(status)) {
    // Child exited normally (called exit() or returned from main)
    int code = WEXITSTATUS(status);   // 0 = success, non-zero = error
    printf("Exit code: %d\n", code);
}

if (WIFSIGNALED(status)) {
    // Child was killed by a signal (e.g., SIGKILL, SIGSEGV)
    int sig = WTERMSIG(status);
    printf("Killed by signal: %d\n", sig);
}
```

### waitpid() Options

| Flag | Behavior |
|---|---|
| `0` | Block until child terminates (what we use) |
| `WNOHANG` | Return immediately if no child has exited (non-blocking) |
| `WUNTRACED` | Also report stopped children (e.g., by SIGSTOP) |

---

## 11. Load Balancing

### Our Algorithm: First-Available

```c
void assign_task(const char *command) {
    for (int i = 0; i < worker_count; i++) {
        if (!workers[i].isBusy) {
            send_message(workers[i].fd, command);
            workers[i].isBusy = 1;
            return;
        }
    }
    printf("All workers busy!\n");
}
```

**Time Complexity**: O(n) where n = number of workers.

### How It Works

1. When a task arrives, scan the worker array left to right.
2. Find the **first** worker where `isBusy == 0`.
3. Send the task, mark as busy (`isBusy = 1`).
4. When the worker sends back a result, mark as free (`isBusy = 0`).

### Comparison with Other Strategies

| Strategy | How it works | Pros | Cons |
|---|---|---|---|
| **First-Available** (ours) | Pick the first free worker | Simple, no starvation | May overload low-index workers |
| Round-Robin | Cycle through workers in order | Even distribution | Doesn't consider current load |
| Least-Connections | Pick worker with fewest active tasks | Optimal distribution | Slightly more tracking needed |
| Weighted | Assign based on worker capacity | Handles heterogeneous workers | Complex configuration |
| Random | Pick a random worker | Simple, no state needed | Uneven distribution |

### Interview Q: "Why not Round-Robin?"

"First-Available is simpler to implement and sufficient for our use case. With a small number of workers, both perform similarly. In a production system, I'd use Least-Connections because it naturally handles varying task durations."

---

## 12. Code Walkthrough

### File Structure

```
Distributed Task Scheduler/
├── protocol.h      ← Shared definitions + wire protocol helpers
├── master.c        ← The control center (server)
├── worker.c        ← The execution unit (client)
├── Makefile         ← Build system
└── project_bible.md ← This file
```

### protocol.h — The Shared Layer

**Purpose**: Defines everything shared between Master and Worker.

| Component | What it does |
|---|---|
| Constants | PORT, MAX_WORKERS, BUFFER_SIZE |
| Worker struct | `{ fd, ip[16], isBusy }` |
| `send_message()` | Sends a length-prefixed message |
| `recv_message()` | Receives a length-prefixed message |

Both functions are `static inline` — they're defined in the header so both `master.c` and `worker.c` can use them without linking issues.

### master.c — Line-by-Line Key Sections

#### setup_server()

```c
server_fd = socket(AF_INET, SOCK_STREAM, 0);  // Create TCP socket
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));  // Allow port reuse
bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));  // Bind to port 8080
listen(server_fd, 5);   // Start listening, backlog=5
```

This is the classic Berkeley Sockets server setup — **memorize this sequence for interviews**.

#### Main Event Loop

```c
while (1) {
    FD_ZERO(&read_fds);              // Clear the watch set
    FD_SET(STDIN_FILENO, &read_fds); // Watch stdin
    FD_SET(server_fd, &read_fds);    // Watch for new workers
    for (i...) FD_SET(workers[i].fd, &read_fds);  // Watch all workers

    select(max_fd + 1, &read_fds, NULL, NULL, NULL);  // BLOCK

    if (FD_ISSET(server_fd, ...))    accept_worker();
    if (FD_ISSET(STDIN_FILENO, ...)) assign_task();
    for (i...) if (FD_ISSET(workers[i].fd, ...)) handle_result();
}
```

### worker.c — Line-by-Line Key Sections

#### execute_command() — The Complete Flow

```c
// 1. Create pipe for capturing output
pipe(pipe_fd);

// 2. Fork a child process
pid = fork();

if (pid == 0) {  // CHILD
    // 3. Redirect stdout → pipe
    dup2(pipe_fd[1], STDOUT_FILENO);
    dup2(pipe_fd[1], STDERR_FILENO);
    close(pipe_fd[0]);  // Don't need read end
    close(pipe_fd[1]);  // Already duplicated

    // 4. Execute the command
    execvp(argv[0], argv);
    _exit(127);  // Only reached if execvp fails
}

// PARENT
close(pipe_fd[1]);  // Don't need write end

// 5. Read output from pipe
while (read(pipe_fd[0], ...) > 0) { ... }

// 6. Wait for child and get exit status
waitpid(pid, &status, 0);
```

**This is the single most important code block in the project. Practice explaining it step by step.**

---

## 13. Error Handling & Edge Cases

### What if a Worker Crashes Mid-Task?

The TCP connection breaks. On the Master side:
- `recv_message()` returns 0 (connection closed) or -1 (error).
- `handle_worker_result()` detects this and removes the worker from the registry.
- The task's result is lost (in production, you'd re-queue it).

### What if the Master Crashes?

Workers detect the broken connection:
- `recv_message()` returns 0 or -1.
- Workers exit gracefully.

### What if execvp() Fails?

This happens when the command doesn't exist (e.g., user types "abc123"):
- `execvp()` returns -1 and sets errno.
- We print the error to stderr (**which is redirected to the pipe via dup2**).
- The parent reads this error message from the pipe.
- Exit code 127 is sent back as the status.

### What about Partial send()/recv() calls?

TCP may not send/receive all bytes in one call. Both `send_message()` and `recv_message()` use loops to handle this. This is called a "write-exactly" or "read-exactly" loop.

### What about SIGPIPE?

If we try to write to a socket that the other side has closed, the kernel sends SIGPIPE (which kills the process by default). In production, you'd either:
- `signal(SIGPIPE, SIG_IGN)` to ignore it, then check `send()` return value.
- Use `MSG_NOSIGNAL` flag in `send()`.

---

## 14. How to Build and Run

### Prerequisites

- Linux or WSL (Windows Subsystem for Linux)
- GCC compiler (`sudo apt install build-essential`)

### Build

```bash
cd "Distributed Task Scheduler"
make clean && make
```

This produces two binaries: `master` and `worker`.

### Run

**Terminal 1 — Start the Master:**
```bash
./master
```

**Terminal 2 — Start Worker #1:**
```bash
./worker
# Or for remote: ./worker 192.168.1.100
```

**Terminal 3 — Start Worker #2 (optional):**
```bash
./worker
```

**In Terminal 1 — Submit tasks:**
```
uptime                    ← check system uptime
ls -la                    ← list files
echo "Hello World"        ← simple echo
date                      ← current date/time
whoami                    ← current user
cat /etc/hostname         ← hostname
status                    ← show worker status
quit                      ← shutdown master
```

---

## 15. Potential Interview Questions (35+ Q&A)

### Category: Sockets & Networking

**Q1: Explain the socket lifecycle for a TCP server.**
A: `socket()` → creates an endpoint. `bind()` → assigns an address (IP+port). `listen()` → marks it as passive. `accept()` → blocks until a client connects and returns a new fd for that connection. This new fd is used for `send()`/`recv()`.

**Q2: What is the difference between TCP and UDP?**
A: TCP is connection-oriented, reliable (ACKs + retransmission), ordered, and has no message boundaries (byte stream). UDP is connectionless, unreliable, unordered, but preserves message boundaries (datagrams). We use TCP because we can't afford to lose tasks.

**Q3: What is the TCP 3-way handshake?**
A: Client sends SYN → Server responds with SYN-ACK → Client sends ACK. This establishes a reliable connection. Each side picks an initial sequence number (ISN) for ordering data.

**Q4: What is `SO_REUSEADDR` and why do you use it?**
A: When a socket is closed, the port enters `TIME_WAIT` state for ~60 seconds (to handle late packets). `SO_REUSEADDR` allows `bind()` to succeed even if the port is in `TIME_WAIT`, which is essential during development when you restart the server frequently.

**Q5: What is `htonl()` and why is it necessary?**
A: It converts a 32-bit integer from host byte order to network byte order (big-endian). Different CPUs may use different byte ordering (x86 is little-endian). Network protocols standardize on big-endian. Without conversion, a length of 6 (0x00000006) could be interpreted as 100663296 (0x06000000) on the other end.

**Q6: Why can't you rely on a single `recv()` call to get all the data?**
A: TCP is a stream protocol. `recv()` may return fewer bytes than requested (kernel delivers whatever's available). You must loop until you've received the expected number of bytes. This is called a "read-exactly" pattern.

**Q7: What happens if you don't call `close()` on a socket?**
A: The file descriptor leaks. Eventually, the process reaches the maximum number of open file descriptors (typically 1024 by default) and future `socket()`/`open()` calls fail with `EMFILE`.

---

### Category: Process Management (fork, exec, wait)

**Q8: What does `fork()` return?**
A: Two different values to two different processes: > 0 (child's PID) to the parent, and 0 to the child. On error, -1 to the caller and no child is created.

**Q9: What is Copy-On-Write (COW)?**
A: After fork(), parent and child share the same physical memory pages (marked read-only). When either process writes, a page fault triggers, and the kernel copies only that specific page. This makes fork() fast because most pages are never copied, especially when the child immediately calls exec().

**Q10: What's the difference between `fork()` and `vfork()`?**
A: `vfork()` is an old optimization where the child shares the parent's address space entirely (no COW). The parent is suspended until the child calls `exec()` or `_exit()`. It's mostly obsolete because modern COW makes `fork()` nearly as fast.

**Q11: What happens if you call `exec()` without `fork()` first?**
A: The current process is **replaced** by the new program. There's no going back. The Worker would cease to exist. That's why we fork() first — the child does exec(), and the parent (Worker) continues.

**Q12: What is the difference between `execvp()` and `execve()`?**
A: `execvp()` takes an argv array and searches the system PATH for the program. `execve()` takes argv + a custom environment array and requires the full path to the binary. `execve()` is the only "true" system call — all other exec variants are library wrappers around it.

**Q13: Why use `_exit()` instead of `exit()` in the child after execvp fails?**
A: `exit()` flushes stdio buffers and runs `atexit()` handlers, which are shared with (copied from) the parent. This could cause double-flushing of output or other side effects. `_exit()` terminates immediately without any cleanup.

**Q14: What is a zombie process and how do you prevent it?**
A: A zombie is a terminated child whose exit status hasn't been read by the parent via `waitpid()`. It occupies a slot in the process table. Prevention: always call `waitpid()` (or use `signal(SIGCHLD, SIG_IGN)` to auto-reap).

**Q15: What is an orphan process?**
A: A child whose parent has terminated. The init process (PID 1) adopts it and reaps it when it exits. Orphans are not harmful — zombies are.

**Q16: How does `waitpid()` differ from `wait()`?**
A: `wait()` waits for ANY child. `waitpid()` waits for a SPECIFIC child (by PID). `waitpid()` also supports the `WNOHANG` flag for non-blocking checks.

---

### Category: I/O and Pipes

**Q17: How does `dup2()` work?**
A: `dup2(old_fd, new_fd)` makes `new_fd` refer to the same open file description as `old_fd`. If `new_fd` was already open, it's closed first. After `dup2(pipe_fd[1], STDOUT_FILENO)`, any write to stdout goes to the pipe instead of the terminal.

**Q18: What is a pipe and how is it different from a socket?**
A: A pipe is a unidirectional IPC mechanism — one end writes, one end reads. It only works between related processes (parent-child via fork). A socket is bidirectional and works between unrelated processes, even across networks.

**Q19: Why close unused pipe ends?**
A: Two reasons: (1) `read()` on a pipe returns EOF (0) only when ALL write ends are closed. If the parent keeps pipe_fd[1] open, it will never see EOF. (2) File descriptors are a limited resource.

**Q20: What is the pipe buffer size?**
A: On Linux, it's typically 64KB (16 pages of 4KB). You can check with `cat /proc/sys/fs/pipe-max-size`. If the buffer is full, `write()` blocks until the reader drains some data.

**Q21: How does the shell implement `ls | grep "txt"`?**
A: The shell creates a pipe, forks twice. For `ls`: `dup2(pipe_write, STDOUT)` then `exec("ls")`. For `grep`: `dup2(pipe_read, STDIN)` then `exec("grep", "txt")`. The pipe connects ls's stdout to grep's stdin. This is exactly the same dup2 technique we use.

---

### Category: I/O Multiplexing

**Q22: What is `select()` and why do you use it?**
A: `select()` monitors multiple file descriptors for readability/writability. It blocks until at least one fd is ready. We use it because the Master needs to simultaneously watch stdin (for commands), the server socket (for new workers), and all worker sockets (for results) — without threads.

**Q23: What are the alternatives to `select()`?**
A: `poll()` (no FD_SETSIZE limit), `epoll()` (Linux, O(1) event notification, scales to millions of connections), `kqueue` (BSD/macOS). For our use case with < 10 fds, select() is sufficient.

**Q24: What is the `nfds` parameter in `select()`?**
A: It's the highest-numbered fd in any of the fd_sets, plus one. The kernel uses this to know how many bits to check. Setting it correctly is important for both correctness and performance.

**Q25: Why do you rebuild the fd_set on every iteration?**
A: `select()` **modifies** the fd_set to contain only the ready fds. After it returns, the original set is destroyed. You must rebuild it from scratch before calling select() again.

---

### Category: Architecture & Design

**Q26: Why the Master-Worker model?**
A: It provides a clean separation of concerns: the Master handles task scheduling and coordination, Workers handle execution. It's scalable (add more Workers to handle more tasks) and fault-tolerant (if one Worker fails, others continue).

**Q27: How does your load balancer work?**
A: First-Available selection. I iterate through the worker array and assign the task to the first worker where `isBusy == false`. When the worker sends back a result, I reset `isBusy` to false. It's O(n) complexity.

**Q28: What happens if all workers are busy?**
A: The Master prints a message asking the user to try again. In production, I'd implement a task queue that buffers pending tasks and dispatches them as workers become free.

**Q29: How would you implement a task queue?**
A: A circular buffer (ring buffer) or a linked list of task strings. When a task arrives and all workers are busy, enqueue it. When a worker finishes, dequeue the next task and assign it. This is a producer-consumer pattern.

**Q30: What if a worker crashes?**
A: The Master detects the broken TCP connection (recv returns 0 or -1), removes the worker from the registry, and the task result is lost. To handle this properly, we'd need: (1) task acknowledgment, (2) a pending tasks list, (3) retry logic with a configurable retry count.

**Q31: Why TCP instead of UDP?**
A: Reliability. We can't afford to lose task commands or results. TCP provides guaranteed, ordered delivery with automatic retransmission. The overhead is acceptable because task scheduling is not latency-sensitive.

---

### Category: Systems / General

**Q32: What does "everything is a file" mean in UNIX?**
A: UNIX represents most I/O resources as file descriptors: regular files, directories, sockets, pipes, devices (`/dev/*`), and even `/proc` entries. This uniform interface means `read()`, `write()`, and `close()` work on all of them.

**Q33: What are the standard file descriptors?**
A: `0 = stdin`, `1 = stdout`, `2 = stderr`. Every process starts with these three open. That's why when we `dup2(pipe_fd[1], 1)`, we redirect stdout — fd 1 now points to the pipe.

**Q34: What is `EINTR` and why do you handle it?**
A: `EINTR` means a system call was interrupted by a signal (e.g., SIGCHLD). The call returns -1 with `errno == EINTR`. The correct response is to simply retry the call. We check for this in the select() loop.

**Q35: How would you make this system production-ready?**
A: (1) Thread pool on the Master for handling multiple results concurrently. (2) Task persistence (store tasks in a database in case of Master crash). (3) Worker heartbeats for health monitoring. (4) TLS encryption for security. (5) Task timeout and retry logic. (6) Configurable load balancing strategies. (7) A web UI for monitoring.

---

## 16. How to Extend This Project

These are good talking points for "What would you do next?"

| Extension | Difficulty | Description |
|---|---|---|
| Task Queue | Easy | Buffer tasks when all workers are busy |
| Task Priority | Medium | Min-heap / priority queue for urgent tasks |
| Worker Heartbeat | Medium | Periodic ping-pong to detect dead workers |
| Task Retry | Medium | Re-queue a task if the worker crashes |
| Task Timeout | Medium | Kill tasks that take too long (`alarm()` + signal handler) |
| Multi-threaded Master | Hard | Use pthreads instead of select() |
| TLS Encryption | Hard | Use OpenSSL for encrypted communication |
| Web Dashboard | Hard | Add an HTTP endpoint for monitoring |

---

## 17. Glossary

| Term | Definition |
|---|---|
| **fd (File Descriptor)** | An integer that refers to an open file, socket, pipe, etc. |
| **Socket** | An endpoint for network communication |
| **TCP** | Transmission Control Protocol — reliable, ordered, byte-stream |
| **UDP** | User Datagram Protocol — unreliable, unordered, message-oriented |
| **SYN/ACK** | TCP handshake packets (Synchronize / Acknowledge) |
| **fork()** | System call to create a new process (copy of the parent) |
| **exec()** | System call to replace the current process with a new program |
| **COW** | Copy-On-Write — memory optimization for fork() |
| **Zombie** | Terminated child process not yet reaped by parent |
| **Orphan** | Child process whose parent has terminated |
| **pipe()** | Creates a unidirectional data channel between processes |
| **dup2()** | Duplicates a file descriptor (redirects I/O) |
| **waitpid()** | Waits for a child process and retrieves its exit status |
| **select()** | I/O multiplexing — monitors multiple fds for activity |
| **htonl()/ntohl()** | Host-to-Network / Network-to-Host byte order conversion |
| **Big-Endian** | Most significant byte first (network byte order) |
| **Little-Endian** | Least significant byte first (x86 CPUs) |
| **Backlog** | Queue size for pending connections in listen() |
| **TIME_WAIT** | TCP state after closing — port stays reserved ~60s |
| **SIGPIPE** | Signal sent when writing to a broken pipe/socket |
| **EINTR** | Error code: system call interrupted by a signal |

---

*Last Updated: April 2026*
*Project: Distributed Task Scheduler — C-DAC Interview Preparation*
