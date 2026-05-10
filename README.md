# Distributed Task Scheduler

A **distributed task scheduler** built in **C++17** and **Python 3**, implementing the master-worker architecture over raw TCP sockets — no frameworks, no libraries beyond the C++ standard library.

[![CI](https://github.com/prakash-nitc/Distributed-Task-Scheduler/actions/workflows/build.yml/badge.svg)](https://github.com/prakash-nitc/Distributed-Task-Scheduler/actions/workflows/build.yml)

---

## Architecture

```
                    ┌──────────────────────────────────────┐
                    │          MASTER NODE  (C++)           │
                    │                                      │
                    │  TCP Server  ·  select() Loop        │
                    │  Worker Registry  (std::vector)      │
                    │  Task Queue       (std::queue)       │
                    │  Load Balancer    (first-available)  │
                    │  Structured Logger (logger.hpp)      │
                    └──────┬───────────────────┬───────────┘
                           │ TCP               │ TCP
              ┌────────────▼────┐      ┌───────▼──────────┐
              │  WORKER  (C++)  │ ...  │  CLIENT  (Python) │
              │                 │      │                   │
              │  fork()         │      │  struct.pack()    │
              │  pipe()         │      │  socket.recv()    │
              │  dup2()         │      │                   │
              │  execvp()       │      └───────────────────┘
              │  waitpid()      │
              └─────────────────┘
```

**Data flow for one task:**
```
Client sends "uptime"  →  Master queues it  →  First free Worker picks it up
→  Worker: fork() + dup2() + execvp("uptime")  →  Output captured via pipe()
→  Worker sends result back  →  Master forwards to client  →  Worker marked free
```

---

## What It Demonstrates

| Concept | How |
|---|---|
| **TCP Socket Programming** | Raw `socket/bind/listen/accept/connect/recv/send` — no wrappers |
| **I/O Multiplexing** | `select()` monitors stdin + server fd + all worker fds simultaneously |
| **Process Management** | `fork()` + `execvp()` to run each command in an isolated child process |
| **I/O Redirection** | `dup2()` + `pipe()` to capture `stdout`/`stderr` from the child |
| **Child Reaping** | `waitpid()` prevents zombie processes, retrieves exit code |
| **RAII** | `Socket` class auto-closes file descriptors — no manual cleanup |
| **Custom Binary Protocol** | 4-byte big-endian length-prefix framing, compatible across C++ and Python |
| **Load Balancing** | First-available scheduling; `std::queue` buffers tasks when all workers busy |
| **Structured Logging** | Thread-safe logger: levels, millisecond timestamps, ANSI colors, `isatty()` |
| **Signal Handling** | `SIGINT`/`SIGTERM` trigger graceful shutdown — workers exit cleanly |
| **Containerization** | Multi-stage Docker build; `docker-compose` for one-command cluster startup |
| **Docker Networking** | `getaddrinfo()` resolves Docker service hostnames via embedded DNS |
| **Cross-Language IPC** | Python client speaks the exact same binary wire protocol as C++ components |

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++17 (master, worker) · Python 3 (client) |
| Build | Makefile · CMake 3.16+ |
| Containerization | Docker (multi-stage) · docker-compose |
| CI | GitHub Actions — native build + Docker build |
| OS | Linux · macOS · WSL2 |

---

## Quick Start

### Option 1 — Docker (recommended, no local install needed)

```bash
docker-compose build
docker-compose up --scale worker=3
```

In a second terminal, submit tasks:
```bash
python3 client.py 127.0.0.1 8080
>>> uptime
>>> ls -la /
>>> date
>>> quit
```

Scale workers up or down without restarting:
```bash
docker-compose up --scale worker=5
docker-compose down
```

### Option 2 — Make

```bash
# Prerequisites: g++ (C++17), Python 3, Linux/WSL/macOS
make

./master &            # Terminal 1
./worker &            # Terminal 2 (run multiple for more workers)
python3 client.py     # Terminal 3
```

Use `status` to inspect the worker pool, `quit` to shut down.

### Option 3 — CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/master
```

---

## What the Logs Look Like

```
[2026-05-17 14:32:01.123] [INFO ] [MASTER] Listening on port 8080
[2026-05-17 14:32:01.130] [INFO ] [MASTER] Running in server-only mode — use client.py to submit tasks
[2026-05-17 14:32:03.451] [INFO ] [WORKER] Connected to Master at master:8080
[2026-05-17 14:32:03.456] [INFO ] [MASTER] Worker #0 connected from 172.18.0.3 (pool size: 1)
[2026-05-17 14:32:03.489] [INFO ] [WORKER] Connected to Master at master:8080
[2026-05-17 14:32:03.492] [INFO ] [MASTER] Worker #1 connected from 172.18.0.4 (pool size: 2)
[2026-05-17 14:32:08.001] [INFO ] [MASTER] Dispatching to Worker #0 [172.18.0.3]: "uptime"
[2026-05-17 14:32:08.224] [INFO ] [WORKER] Task completed in 223ms — sending result
[2026-05-17 14:32:08.226] [INFO ] [MASTER] Result received from Worker #0 [172.18.0.3]
[2026-05-17 14:32:15.000] [INFO ] [MASTER] Shutdown signal received — closing all connections
[2026-05-17 14:32:15.002] [INFO ] [WORKER] Shutdown signal received — exiting
```

---

## Project Structure

```
├── master.cpp          Master node — TCP server, select() loop, load balancer
├── worker.cpp          Worker node — fork/exec pipeline, retry logic
├── protocol.hpp        Shared — RAII Socket class, wire protocol, Worker struct
├── logger.hpp          Structured logger — levels, timestamps, mutex, ANSI colors
├── client.py           Python client — cross-language wire protocol demo
│
├── Dockerfile          Multi-stage build: builder (g++) → runtime (binaries only)
├── docker-compose.yml  Cluster orchestration — healthcheck, scaling, bridge network
├── .dockerignore       Excludes binaries and build artifacts from Docker context
│
├── CMakeLists.txt      CMake — Threads, install targets, out-of-source build
├── Makefile            Simple make alternative
│
├── .github/
│   └── workflows/
│       └── build.yml   CI — Make + CMake + Docker builds on every push
│
└── project_bible.md    75+ interview Q&A on every concept in this project
```

---

## Key Design Decisions

**Why raw sockets?**
To demonstrate every step of TCP communication — `socket()`, `bind()`, `listen()`, `accept()`, `recv()`, `send()` — rather than hiding them behind a library. Every byte in the protocol is intentional.

**Why fork() and not threads for task execution?**
`fork()` gives true process isolation — a misbehaving command can't corrupt the worker's state. The `dup2()` + `pipe()` pattern captures output without the child knowing it's being captured.

**Why select() and not threads for the master?**
`select()` handles all I/O in a single thread, eliminating mutex complexity for the shared worker registry and task queue. For this scale (< 10 workers), it is simpler and equally correct.

**Why a custom binary protocol?**
Length-prefix framing solves TCP's stream-boundary problem cleanly. The same 4-byte big-endian format works identically in C++ (`htonl`) and Python (`struct.pack("!I")`), proving the protocol is language-agnostic.

**Why multi-stage Docker?**
The runtime image contains zero build tools — only the compiled binaries and Python. This drops the image from ~800MB (with g++) to ~180MB, and eliminates the attack surface of a compiler in production.

**Why getaddrinfo() instead of inet_pton()?**
`inet_pton()` only converts IP strings. `getaddrinfo()` performs actual DNS resolution, allowing workers to connect to `"master"` (a Docker service hostname) instead of a hardcoded IP.

---

## Documentation

[`project_bible.md`](project_bible.md) contains 75+ interview Q&A covering every concept used:

- System calls: `fork`, `exec`, `dup2`, `pipe`, `waitpid`, `select`, `getaddrinfo`
- C++17: RAII, move semantics, `std::mutex`, `std::chrono`, Singleton pattern
- Docker: images vs containers, multi-stage builds, networking, healthchecks, DNS
- CMake: `find_package`, `PRIVATE/PUBLIC/INTERFACE`, out-of-source builds
- Architecture: trade-offs, extension roadmap, design decisions

---

## License

MIT
