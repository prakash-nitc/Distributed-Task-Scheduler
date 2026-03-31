# Distributed Task Scheduler

A **Master-Worker distributed task scheduler** built in C using POSIX APIs — demonstrating TCP socket programming, process management (`fork`/`exec`/`dup2`/`waitpid`), I/O multiplexing (`select`), and basic load balancing.

## Architecture

```
┌──────────────────────────────────────────────┐
│                 MASTER NODE                   │
│  TCP Server · select() Event Loop · LB       │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│  │ Listener │  │ Worker   │  │ Load       │  │
│  │ Socket   │  │ Registry │  │ Balancer   │  │
│  └──────────┘  └──────────┘  └────────────┘  │
└──────────┬──────────────────────┬─────────────┘
           │ TCP                  │ TCP
     ┌─────▼─────┐         ┌─────▼─────┐
     │ WORKER #1  │         │ WORKER #2  │
     │ fork+exec  │         │ fork+exec  │
     └────────────┘         └────────────┘
```

## Key Concepts

| Concept | System Calls Used |
|---|---|
| TCP Networking | `socket()`, `bind()`, `listen()`, `accept()`, `connect()` |
| I/O Multiplexing | `select()`, `FD_SET`, `FD_ISSET` |
| Process Creation | `fork()` (with Copy-On-Write) |
| Process Execution | `execvp()` |
| I/O Redirection | `dup2()`, `pipe()` |
| Process Reaping | `waitpid()`, `WIFEXITED`, `WEXITSTATUS` |
| Wire Protocol | Length-prefix framing, `htonl()`/`ntohl()` |
| Load Balancing | First-available worker selection |

## Build & Run

**Prerequisites:** Linux / WSL / macOS with GCC

```bash
make

# Terminal 1 — Start the Master
./master

# Terminal 2 — Start a Worker
./worker

# Terminal 3 — Start another Worker (optional)
./worker
```

**Submit tasks in Terminal 1:**
```
uptime
ls -la
echo "Hello from distributed system"
date
status     ← show worker status
quit       ← shutdown
```

## Project Structure

```
├── protocol.h        # Shared wire protocol & constants
├── master.c          # Control center (TCP server + load balancer)
├── worker.c          # Execution unit (fork/exec pipeline)
├── Makefile          # Build system
└── project_bible.md  # Detailed documentation & interview prep (35+ Q&A)
```

## Documentation

See [`project_bible.md`](project_bible.md) for comprehensive documentation including:
- End-to-end data flow walkthrough
- Deep dives on every system call
- 35+ interview questions with model answers
- Error handling & edge cases
- Extension ideas

## License

MIT
