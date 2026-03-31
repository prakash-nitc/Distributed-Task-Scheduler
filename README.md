# Distributed Task Scheduler

Master—Worker distributed task scheduler built in C with POSIX APIs. It showcases TCP socket programming, I/O multiplexing, process supervision, and lightweight load balancing — a compact project that demonstrates systems fundamentals recruiters care about.

## Why it stands out
- **Systems depth:** end-to-end TCP server, event loop (`select`), and process orchestration (`fork`/`exec`/`dup2`/`waitpid`).
- **Resilient execution path:** length-prefixed wire protocol, input validation, and worker lifecycle management.
- **Practical load balancing:** first-available worker selection to keep throughput high without over-engineering.
- **Interview-ready:** comes with a 35+ question “project bible” to explain design trade-offs.

## Skills & tech
- C, POSIX sockets, process control, file descriptor management
- I/O multiplexing with `select()`, length-prefixed framing (`htonl`/`ntohl`)
- Unix tooling: `make`, gcc, debugging with `-g`

## Architecture (high level)
```
┌──────────────────────────────────────────────┐
│                 MASTER NODE                  │
│  TCP Server · select() Event Loop · LB       │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│  │ Listener │  │ Worker   │  │ Load       │  │
│  │ Socket   │  │ Registry │  │ Balancer   │  │
│  └──────────┘  └──────────┘  └────────────┘  │
└──────────┬──────────────────────┬─────────────┘
           │ TCP                  │ TCP
     ┌─────▼─────┐         ┌─────▼─────┐
     │ WORKER #1 │         │ WORKER #2 │
     │ fork+exec │         │ fork+exec │
     └──────────┘         └───────────┘
```

## Quick start (5-minute demo)
**Prerequisites:** Linux / WSL / macOS with GCC
```bash
make

# Terminal 1 — start the Master
./master

# Terminal 2 — start a Worker
./worker

# Terminal 3 — add another Worker (optional)
./worker
```

**Run tasks from Terminal 1:**
```
uptime
ls -la
echo "Hello from distributed system"
date
status   # show worker status
quit     # shutdown
```

## How it works
1) Master accepts TCP connections from workers and clients.  
2) Tasks are framed with a length-prefix protocol to avoid partial reads.  
3) Master picks the first available worker, forwards the task, and streams back output.  
4) Worker `fork+exec` handles command execution with `dup2` for I/O redirection; Master reaps exits with `waitpid`.  

## Key concepts (at a glance)
| Concept | System Calls Used |
|---|---|
| TCP Networking | `socket()`, `bind()`, `listen()`, `accept()`, `connect()` |
| I/O Multiplexing | `select()`, `FD_SET`, `FD_ISSET` |
| Process Creation | `fork()` (Copy-On-Write) |
| Process Execution | `execvp()` |
| I/O Redirection | `dup2()`, `pipe()` |
| Process Reaping | `waitpid()`, `WIFEXITED`, `WEXITSTATUS` |
| Wire Protocol | Length-prefix framing, `htonl()`/`ntohl()` |
| Load Balancing | First-available worker selection |

## Project structure
```
├── protocol.h        # Shared wire protocol & constants
├── master.c          # Control center (TCP server + load balancer)
├── worker.c          # Execution unit (fork/exec pipeline)
├── Makefile          # Build system
└── project_bible.md  # Detailed documentation & interview prep (35+ Q&A)
```

## Documentation for interviews
See [`project_bible.md`](project_bible.md) for:
- End-to-end data flow walkthrough
- Deep dives on every system call
- 35+ interview questions with model answers
- Error handling & edge cases
- Extension ideas to discuss in screenings

## License

MIT
