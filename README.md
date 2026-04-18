# Distributed Task Scheduler

A **Master-Worker distributed task scheduler** built with **C++** and **Python** — demonstrating TCP socket programming, process management (`fork`/`exec`/`dup2`/`waitpid`), I/O multiplexing (`select`), task queuing, and basic load balancing.

## Architecture

```
┌──────────────────────────────────────────────┐
│                 MASTER NODE (C++)            │
│  TCP Server · select() Loop · Task Queue     │
│  ┌──────────┐  ┌──────────┐  ┌────────────┐  │
│  │ Listener │  │ Worker   │  │ Load       │  │
│  │ Socket   │  │ Registry │  │ Balancer   │  │
│  │          │  │ (vector) │  │            │  │
│  └──────────┘  └──────────┘  └────────────┘  │
│  ┌──────────────────────────────────────────┐ │
│  │ std::queue<string> — pending task buffer │ │
│  └──────────────────────────────────────────┘ │
└──────────┬──────────────────────┬─────────────┘
           │ TCP                  │ TCP
     ┌─────▼─────┐         ┌─────▼──────┐
     │ WORKER (C++) │       │ CLIENT (Py) │
     │ fork+exec  │         │ socket lib  │
     └────────────┘         └─────────────┘
```

## Tech Stack

| Component | Language | Key Features |
|---|---|---|
| Master | C++ | Classes, `std::vector`, `std::queue`, RAII, `select()` |
| Worker | C++ | `fork()`, `execvp()`, `dup2()`, `pipe()`, `waitpid()` |
| Client | Python | `socket`, `struct`, length-prefix protocol |
| Protocol | Shared | 4-byte big-endian length-prefix wire protocol |

## Build & Run

**Prerequisites:** Linux / WSL / macOS with g++ and Python 3

```bash
make

# Terminal 1 — Start the Master
./master

# Terminal 2 — Start a Worker (C++)
./worker

# Terminal 3 — Submit tasks via Python client
python3 client.py
```

**Submit tasks:**
```
>>> uptime
>>> ls -la
>>> echo "Hello from distributed system"
>>> date
>>> quit
```

**Or use the Master's stdin directly:**
```
uptime
status     ← show worker status + queued tasks
quit       ← shutdown
```

## Project Structure

```
├── protocol.hpp      # Shared C++ protocol — RAII Socket, wire format
├── master.cpp        # Master node — TCP server, task queue, load balancer
├── worker.cpp        # Worker node — fork/exec execution pipeline
├── client.py         # Python client — cross-language task submission
├── Makefile          # Build system (g++, C++17)
└── project_bible.md  # Interview documentation (40+ Q&A)
```

## License

MIT
