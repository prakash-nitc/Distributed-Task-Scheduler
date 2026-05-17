# Interview Preparation Guide — Distributed Task Scheduler

> This is your spoken preparation guide. Everything here is written the way you should SAY it — not the way you'd write a technical paper. Read it aloud. Practice it. The goal is to sound natural, not to recite.

---

## PART 1 — Understand Your Project Completely First

Before any script, you must have a clear mental picture. Read this section until it feels obvious.

### What Did You Actually Build?

You built a system where **one central server (the master) receives shell commands and distributes them to multiple worker machines for parallel execution**.

That's it. Everything else — TCP, fork, select, Docker, logging — is HOW you built it.

### The Three Characters

| Character | What It Is | What It Does |
|---|---|---|
| **Master** | A C++ TCP server | Accepts commands, distributes them, collects results |
| **Worker** | A C++ process | Connects to master, executes commands, sends output back |
| **Client** | A Python script | Demonstrates the protocol works from any language |

### The Mental Movie (Play This in Your Head)

Imagine a restaurant:
- You (the client) place an order: "I want uptime"
- The waiter (master) takes your order
- The waiter checks which chef (worker) is free
- The waiter tells that chef: "Cook uptime"
- The chef goes to the kitchen, makes the dish (runs the command in a child process), and sends the output back
- The waiter delivers the result to you
- If all chefs are busy, your order goes on a waiting list (the task queue)

That mental model covers 80% of what you'll be asked. Everything else is implementation detail.

---

## PART 2 — Your Opening Pitch

This is the first thing out of your mouth when they say "tell me about your project." Have two versions ready.

### The 30-Second Version (for casual opening)

> "I built a distributed task scheduler from scratch in C++ and Python. The idea is: you have one master server that accepts shell commands, and multiple worker processes that execute them in parallel. You can start a five-worker cluster with a single Docker command and submit tasks from any machine. The interesting part was building everything at the systems level — raw TCP sockets, process creation with fork and exec, I/O multiplexing with select, and a custom binary protocol. No frameworks, every component is hand-written."

### The 2-Minute Version (when they want more detail)

> "I built a distributed task scheduler. Let me break it down.
>
> The system has three components. First, a Master — this is a C++ TCP server that sits in the middle. It accepts shell commands and distributes them to workers. Second, Workers — each worker is a separate C++ process that connects to the master, waits for commands, executes them, and sends the output back. Third, I wrote a Python client that demonstrates the same binary wire protocol works from Python as well — showing cross-language interoperability.
>
> The technically interesting part is how the worker actually executes a command. When it receives 'uptime', it doesn't just call system(). Instead, it uses fork() to create a child process, sets up a pipe, redirects the child's stdout into that pipe using dup2(), then runs execvp() which replaces the child with the actual uptime program. The parent reads the output from the pipe and sends it back over TCP. This is exactly how your shell implements piping — ls | grep 'txt' works the same way.
>
> On the master side, I use select() for I/O multiplexing — one thread monitoring stdin, the server socket, and all connected worker sockets simultaneously. When a worker finishes, it's marked free and the next queued task is dispatched automatically.
>
> I also containerized the entire system with Docker. Multi-stage build so the runtime image has no compiler, docker-compose to spin up master and workers with one command, structured logging with timestamps and log levels, and graceful shutdown with signal handling. The project has a CI pipeline on GitHub Actions that builds and tests on every commit."

---

## PART 3 — The End-to-End Walkthrough

Interviewers love asking: *"Walk me through exactly what happens when someone submits a task."*

Here is your complete, conversational answer. Practice saying this out loud.

### "Walk me through what happens when I type 'uptime'"

> "Sure. Let me trace it from start to finish.
>
> **Step 1: The user types 'uptime' on the master's terminal.**
> The master is sitting in a select() call — think of this as the master watching multiple doors at once. One of those doors is stdin. When you type and hit enter, select() wakes up and says 'stdin has data.'
>
> **Step 2: Master picks a free worker.**
> The master has a vector of connected workers, each marked as either busy or free. It scans from the beginning and picks the first free one. This is called first-available load balancing. If all workers are busy, it pushes the task into a queue and handles it later.
>
> **Step 3: Master sends the command over TCP.**
> But here's the thing — TCP is a byte stream, there are no message boundaries. If I just do send('uptime'), the receiver might get 'upt' then 'ime', or it might arrive with another message's bytes. So I use a length-prefix protocol. I send 4 bytes first — the length of the message in big-endian format — then the actual bytes. The worker reads the 4-byte header first, knows the exact length, then reads that many bytes. This is called framing.
>
> **Step 4: Worker receives the command and forks a child process.**
> The worker calls fork(). This creates an exact copy of the worker process. Now two processes exist: the parent (the worker itself) and the child (a disposable copy created just to run the command). Why fork instead of just running the command directly? Because exec() is destructive — it replaces the process's entire memory with the new program. If I called exec() directly on the worker, the worker itself would become 'uptime' and stop being a worker. Fork first, exec in the child.
>
> **Step 5: Child redirects output into a pipe.**
> Before calling exec, the child does two things. First, it creates a pipe — a one-way channel between two processes. Then it uses dup2() to redirect stdout and stderr into the write end of that pipe. Now anything that 'uptime' would normally print to the terminal goes into the pipe instead. The program doesn't know the difference — it just prints normally.
>
> **Step 6: Child executes the command.**
> The child calls execvp('uptime', ...). This replaces the child process's memory with the uptime binary and runs it. Uptime prints its output — which, because of dup2, goes into the pipe.
>
> **Step 7: Parent reads the output.**
> Meanwhile, the parent (the worker) is reading from the read end of the pipe. It collects all the output into a string. Then it calls waitpid() to wait for the child to finish and retrieve the exit code.
>
> **Step 8: Worker sends the result back.**
> Worker appends '[Exit Code: 0]' to the output and sends it back to the master using the same length-prefix protocol.
>
> **Step 9: Master receives and displays.**
> The master's select() wakes up on the worker's file descriptor. Master reads the result, prints it to the terminal, marks the worker as free, and checks if anything is in the task queue."

---

## PART 4 — Concept Mental Models (Your Analogies)

These are the analogies you use when explaining technical concepts. Practice saying these naturally.

### TCP — "It's a phone call, not letters"

> "TCP is like a phone call between two programs. Once you establish the connection, it's a continuous stream — both sides can talk and listen. The important thing is that TCP has no concept of message boundaries. If I say 'hello' and then 'world' on a phone call with no pause, you hear 'helloworld' as one continuous sound. That's why I need my own protocol on top of TCP to know where one message ends and the next begins."

### The Wire Protocol — "I tell you how many words I'm going to say first"

> "Imagine before speaking you say: 'I'm going to say exactly six words: please run uptime for me.' Now you know to wait for exactly six words. That's my protocol. I send 4 bytes that encode the message length, then exactly that many bytes of message. The receiver always reads the length header first, then reads exactly that many bytes. This works regardless of how TCP fragments and delivers the data."

### select() — "One security guard watching multiple doors"

> "The master needs to respond to three things simultaneously: someone typing a command on the terminal, a new worker connecting, or an existing worker sending back a result. The naive approach is one thread per thing — but that means mutexes and race conditions. select() lets one thread watch all those file descriptors simultaneously. It's like a hotel security guard who watches 10 camera feeds at once. When any one of them shows activity, the guard handles it. That's my event loop."

### fork() — "Photocopying yourself"

> "fork() creates a new process that's an exact copy of the caller — same code, same memory, same file descriptors. After fork, both processes execute the same code but see different return values. Parent sees the child's PID, child sees zero. In my case, the child immediately calls exec() to transform into the target command. Why not just exec() directly? Because exec is destructive — it overwrites the process's memory. If the worker called exec directly, the worker itself would become 'uptime' and stop existing. Fork first, exec in the disposable copy."

### dup2() — "Call forwarding for file descriptors"

> "Every process has numbered file descriptors. 0 is stdin, 1 is stdout, 2 is stderr. Normally when a program writes to fd 1, it goes to the terminal. dup2(pipe, 1) is like setting up call forwarding — anything going to fd 1 now goes to the pipe instead. The command being executed doesn't know or care. It just 'prints normally' but the output silently flows into the pipe where the parent is waiting to read it."

### pipe() — "A walkie-talkie between parent and child"

> "Before forking, I create a pipe — a one-way channel with two ends: a read end and a write end. After forking, the child has the write end (connected to stdout via dup2), the parent has the read end. Child produces output → it flows through the pipe → parent collects it. Critical detail: I must close the unused ends. If the parent keeps the write end open, the child's read() never gets EOF — the pipe would hang forever."

### RAII — "The door that closes itself"

> "C++ RAII means the constructor acquires a resource and the destructor releases it — automatically. My Socket class wraps a file descriptor. When the Socket object goes out of scope — whether because the function returned normally, or an exception was thrown — the destructor runs and closes the file descriptor. You never need to remember to close it. This is similar to Python's 'with' statement or Java's try-with-resources, but in C++ it works for any resource."

### Docker — "A shipping container for your code"

> "The classic problem: 'it works on my machine.' Docker solves this by packaging the application together with its entire environment — OS libraries, runtime, everything — into a container. I have a multi-stage Dockerfile: Stage 1 compiles the C++ code using a full Ubuntu image with g++. Stage 2 starts fresh with a minimal runtime image and only copies the compiled binaries. The final image is about 180MB with zero build tools, versus 800MB if you included the compiler. Anyone with Docker can run docker-compose up --scale worker=3 and have a working cluster in under 30 seconds."

### Signal Handling — "The emergency stop button"

> "SIGINT is what gets sent when you press Ctrl+C. Without handling it, the OS immediately kills the process — workers are left hanging with open TCP connections. My signal handler sets a global flag to zero. When select() is interrupted by the signal, it returns with errno=EINTR. Instead of just retrying, I check the flag — if it's zero, I break out of the loop, close all worker connections, and exit cleanly. Workers detect the connection drop via recv() returning empty and also exit gracefully."

---

## PART 5 — Design Decision Scripts

These are the "why" questions. Every design choice has a reason. Know yours.

### "Why did you use C++?"

> "C++ gives me two things I needed. First, direct access to POSIX system calls — fork, exec, pipe, dup2, select — these are the low-level building blocks of how operating systems work. Second, C++ gives me RAII for automatic resource management. In C, I'd have to remember to close every file descriptor in every code path, including error paths. In C++, my Socket class handles it automatically in the destructor. I couldn't use Python or Java for the core because I wanted to demonstrate systems programming at the OS level."

### "Why raw sockets instead of a library like Boost.Asio?"

> "Two reasons. First, I wanted to demonstrate I understand what's actually happening — every system call, every byte in the protocol, every edge case. A library would abstract all that away. Second, this project is specifically about showing those fundamentals — TCP connection lifecycle, manual protocol design, I/O multiplexing. Using a library would defeat the purpose. Everything you see in this project is intentional."

### "Why select() instead of threads for the master?"

> "The master's job is coordination, not computation. It's mostly waiting — waiting for a worker to connect, waiting for a command from the user, waiting for a result to come back. For that workload, select() is simpler and correct. With threads, I'd need to protect the worker registry and task queue with mutexes, handle race conditions, worry about deadlocks. select() handles all I/O in a single thread — there's no shared state to protect. For less than 10 workers, the performance difference is irrelevant. If I needed to scale to thousands of connections, I'd switch to epoll."

### "Why fork() for command execution instead of running it in the worker's thread?"

> "Isolation. Two reasons. First, if the command hangs, crashes, or calls exit(), only the child process dies — the worker itself is unaffected and continues serving future tasks. Second, execvp() completely replaces the process image — it can't be done in a thread. A thread shares memory with the parent; you can't exec in a thread because it would destroy the entire worker process's memory. Fork gives you a disposable copy that can safely call exec."

### "Why a custom binary protocol instead of something like JSON?"

> "TCP is a byte stream — it has no concept of where one message ends and the next begins. You need framing. JSON with newline delimiters works, but requires parsing text. My approach is simpler and faster: 4 bytes encoding the message length, then the payload. The receiver reads 4 bytes, knows exactly how many bytes to wait for, reads them. No parsing, no ambiguity. And crucially, the same format works identically in C++ with htonl() and in Python with struct.pack('!I') — proving the protocol is language-agnostic."

### "Why Docker?"

> "Production mindset. Without Docker, running this project requires: a Linux machine or WSL, the right g++ version, Python 3, correct paths. With Docker, anyone runs docker-compose up --scale worker=3 and has a working 3-node distributed system in 30 seconds. Multi-stage build is important — the runtime image has zero compiler tools, which is both smaller (~180MB vs ~800MB) and more secure. It also exercises real deployment thinking — healthchecks so workers wait for master to be ready, bridge networking so containers find each other by name."

### "Why structured logging?"

> "In a distributed system, you need to know WHAT happened, WHEN it happened, and HOW SERIOUS it is. Raw cout gives you none of that. My logger adds millisecond timestamps, severity levels (DEBUG/INFO/WARN/ERROR), component labels, and ANSI colors in terminals. It auto-detects Docker via isatty() and switches to plain text in container logs. It's thread-safe with a mutex. When you watch docker-compose logs -f, you see exactly which worker handled which task and how long it took."

### "Why getaddrinfo() instead of inet_pton()?"

> "inet_pton() only converts IP address strings like '192.168.1.1'. It doesn't do DNS. In Docker, workers connect to the master by service name — 'master' — not by IP. Docker has an embedded DNS server that resolves container names to IPs. inet_pton('master') just fails. getaddrinfo() performs actual DNS resolution and handles both IP strings and hostnames. This is why workers work identically locally (IP address) and in Docker (hostname)."

---

## PART 6 — Question Bank With Natural Answers

These are the most common interview questions for this type of project. The answers are written in spoken language — conversational, not robotic.

---

### SOCKET PROGRAMMING QUESTIONS

**"What is a file descriptor?"**
> "A file descriptor is just an integer that represents an open resource — a file, a socket, a pipe. The OS maintains a table for each process. 0 is always stdin, 1 is stdout, 2 is stderr. When you call socket() or open(), it returns the next available integer from that table. My Socket class in C++ wraps this integer and closes it automatically when the object is destroyed."

**"What is the difference between TCP and UDP?"**
> "TCP is connection-oriented and reliable. You establish a connection first, data arrives in order, the OS retransmits lost packets automatically. UDP is connectionless and unreliable — you fire packets off and don't know if they arrive. I use TCP because I cannot afford to lose tasks. If a command gets lost in transit, the worker never executes it, and the master waits forever."

**"What does SO_REUSEADDR do?"**
> "When a server closes, the port stays in a TIME_WAIT state for about 60 seconds — the OS is waiting to make sure all in-flight packets have been delivered. If you try to restart the server during that window, bind() fails with 'address already in use.' SO_REUSEADDR tells the OS to let you bind to that port even if it's in TIME_WAIT. Without this, you'd have to wait a minute every time you restart the server during development."

**"What is the listen() backlog?"**
> "The backlog is the maximum number of connections that can be waiting in the queue before accept() processes them. If the server is slow to call accept() and more clients connect in the meantime, they wait in this queue. If the queue is full, new connections are refused. Setting it to 5 means at most 5 pending connections — since workers connect once and stay connected, this is more than enough."

**"What happens at the byte level when a message is sent?"**
> "My protocol sends two things: first, 4 bytes representing the message length in big-endian format — that's network byte order. I use htonl() to convert from the CPU's native byte order to network order. Then the payload bytes follow. The receiver does the opposite: reads 4 bytes, calls ntohl() to convert to host byte order, now knows the payload length, then reads exactly that many bytes in a loop to handle TCP fragmentation."

---

### PROCESS MANAGEMENT QUESTIONS

**"What does fork() return and why does it return twice?"**
> "fork() returns once in each of two processes — the parent and the newly created child. The parent gets the child's PID as the return value. The child gets zero. Both processes continue executing from the same point in the code. The difference in return value is how you tell them apart: if pid > 0, you're the parent; if pid == 0, you're the child."

**"What is Copy-On-Write in the context of fork()?"**
> "After fork(), both parent and child share the same physical memory pages — the OS doesn't immediately copy everything. Pages are marked read-only. Only when one process writes to a page does the OS make a private copy for that process. This makes fork() very fast — especially when the child immediately calls exec() and replaces its memory entirely, the copy never even happens."

**"Why does exec() never return on success?"**
> "Because exec() replaces the current process's code, data, and stack with the new program. After exec(), there's nothing to return to — the original code is gone. The process continues executing, but now it's running 'uptime' or 'ls', not the worker code. If exec() fails — for example the command doesn't exist — it does return with an error, which is why I call _exit(127) right after it."

**"What is a zombie process and how do you prevent it?"**
> "When a child process exits, it doesn't immediately disappear. It stays in a 'zombie' state until the parent reads its exit status with waitpid(). The zombie holds a process table slot but uses no other resources. If the parent never calls waitpid(), zombie processes accumulate. I always call waitpid() after the child exits, which cleans it up and lets me retrieve the exit code."

**"Why _exit() and not exit() in the child?"**
> "exit() flushes stdio buffers and runs all atexit() handlers registered in the process. After fork(), the child has a copy of the parent's stdio buffers. If the child calls exit(), it could flush output that the parent hasn't flushed yet — causing double output. _exit() terminates immediately without any cleanup. In the child, after a failed execvp(), _exit(127) is the correct call."

---

### I/O AND PIPES QUESTIONS

**"Walk me through how you capture a command's output."**
> "Four steps. First, pipe() — I create two file descriptors: a read end and a write end. Second, fork() — two processes now exist, both holding copies of both pipe ends. Third, in the child: dup2(write_end, STDOUT_FILENO) redirects stdout into the pipe, then I close both pipe file descriptors since stdout is now pointing there. Then execvp() runs the command — its output flows into the pipe. Fourth, in the parent: I close the write end so I'll get EOF when the child finishes, then read from the read end in a loop, collecting all output into a string."

**"Why do you close the write end of the pipe in the parent?"**
> "Because read() only returns zero — indicating EOF — when ALL write ends of the pipe are closed. If the parent holds the write end open, even after the child exits and closes its copy, the parent's read() will block forever waiting for more data that will never come. Closing unused pipe ends is essential for correct behavior."

**"What is dup2() exactly?"**
> "dup2(old_fd, new_fd) makes new_fd point to exactly the same underlying file or pipe as old_fd. After dup2(pipe_write, 1), file descriptor 1 — which is STDOUT — now refers to the pipe's write end. When the child's program writes to stdout, it's writing to the pipe. The child doesn't know and doesn't care — it just uses fd 1 normally."

---

### C++ CONCEPTS QUESTIONS

**"What is RAII and why is it important?"**
> "RAII stands for Resource Acquisition Is Initialization. The idea is: you acquire a resource in the constructor and release it in the destructor. The destructor runs automatically when the object goes out of scope — whether the function returns normally, returns early, or throws an exception. My Socket class wraps a file descriptor. No matter how the function exits, the file descriptor is closed. Without RAII, you'd need to close the fd in every return path and every catch block — which is tedious and error-prone."

**"Why is your Socket class movable but not copyable?"**
> "If two Socket objects held the same file descriptor, both destructors would call close() on it. The second close() would either be a no-op or, if the fd was reused by a new socket in between, close the wrong connection. This is a classic double-free bug. Making Socket movable but not copyable — like std::unique_ptr — guarantees only one owner. Moving transfers ownership and sets the source's fd to -1, so its destructor does nothing."

**"What is std::queue and why did you use it for task buffering?"**
> "std::queue is a FIFO container adapter. I use it when all workers are busy and a new task arrives. push() adds to the back, front() peeks at the front, pop() removes from the front. When a worker finishes a task, I check if the queue is non-empty, pop the next task, and dispatch it immediately. The queue is purely in-memory, which means tasks are lost if the master crashes — that's a known limitation."

**"What does volatile sig_atomic_t mean?"**
> "Signal handlers run asynchronously — they can interrupt normal execution at any point. Accessing a regular variable from a signal handler and from main code simultaneously can cause undefined behavior. volatile tells the compiler not to cache the variable in a register — always read from memory. sig_atomic_t is a type that the C++ standard guarantees can be read and written atomically on the target platform. Together, they make it safe to set a flag in a signal handler and read it in the main loop."

---

### DOCKER QUESTIONS

**"What is the difference between a Docker image and a container?"**
> "An image is a read-only snapshot — a blueprint. A container is a running instance of that image. Same relationship as a class and an object in OOP. Multiple containers can run from the same image — I run three worker containers all from the same image, each gets its own network identity and process space."

**"Explain your multi-stage Docker build."**
> "Stage one starts from Ubuntu 22.04, installs g++ and make, copies the source files, and compiles the binaries. Stage two starts completely fresh from a clean Ubuntu image, installs only Python3, and copies just the compiled binaries from stage one using COPY --from=builder. The result: zero compiler in the runtime image. Size drops from roughly 800MB to 180MB. And it's more secure — an attacker who gets into a running container can't use g++ to recompile malicious code."

**"How do containers in your system find each other?"**
> "Docker Compose creates a bridge network and runs an embedded DNS server inside every container. Container service names become resolvable hostnames. So when a worker calls connect('master', 8080), it triggers a DNS lookup. Docker's DNS at 127.0.0.11 resolves 'master' to the master container's IP, say 172.18.0.2. The connection is established. That's why I use getaddrinfo() instead of inet_pton() — getaddrinfo() does DNS resolution, inet_pton() only converts IP strings."

**"What is a Docker healthcheck and why do you need one?"**
> "A healthcheck is a command Docker runs periodically to verify a container is actually functional — not just started. The difference matters: the master container starts immediately when Docker runs ./master, but ./master takes a few milliseconds to call bind() and listen(). Workers depend_on master with condition: service_healthy. Without a healthcheck, depends_on just waits for the container to start, not for the port to be listening. Workers would try to connect and fail. My healthcheck uses bash's /dev/tcp — bash -c 'echo > /dev/tcp/localhost/8080' — this succeeds only if port 8080 is accepting connections."

---

### ARCHITECTURE QUESTIONS

**"How does your load balancer work?"**
> "First-available scheduling. The master maintains a vector of workers, each with an is_busy flag. When a task arrives, I scan from the beginning and pick the first worker where is_busy is false. I send the task to that worker and set its flag to true. When the result comes back, I set it to false and check the queue for pending tasks. It's O(n) per dispatch, which is fine for 10 workers. Round-robin would be O(1) but could assign a task to a worker that's already running something — you'd need a ready/not-ready signal. First-available is simpler and correct."

**"What happens if a worker crashes while executing a task?"**
> "The TCP connection breaks. The master is waiting for a result from that worker's file descriptor. When the connection breaks, select() marks that fd as readable, and recv() returns zero. My handle_result() treats zero as a disconnect — it logs a warning, closes the fd, removes the worker from the vector, and continues. The task result is lost. In a production system, you'd track which task went to which worker and re-queue it on disconnect. For this project, I've identified it as a known limitation."

**"What happens when all workers are busy?"**
> "I have a std::queue<std::string> for pending tasks. If assign_task() can't find a free worker, the task gets pushed onto the queue with a warning log. When any worker sends back a result and gets marked free, I immediately check if the queue is non-empty. If it is, I pop the front task and dispatch it to that now-free worker. This is the producer-consumer pattern — tasks are produced by the user, consumed by workers, and the queue is the buffer."

**"How do you handle graceful shutdown?"**
> "I register SIGINT and SIGTERM handlers that set a global volatile sig_atomic_t flag to zero. When the user presses Ctrl+C or docker-compose down sends SIGTERM, the signal handler fires. This interrupts select() — on Linux, select() returns -1 with errno set to EINTR when interrupted by a signal. In the EINTR handler, I check the flag. If it's zero, I break out of the main loop, close all worker file descriptors, and exit. The workers detect the TCP connection drop via recv() returning empty and also exit cleanly."

**"How would you scale this to 10,000 workers?"**
> "Three changes. First, replace select() with epoll — select() has a hard limit around 1024 file descriptors and is O(n) per call. epoll is O(1) for event notification and scales to any number of fds. Second, the master currently runs single-threaded. With 10,000 workers, I'd want a thread pool for I/O processing. Third, the worker registry is a std::vector with O(n) removal — I'd switch to an unordered_map keyed on fd for O(1) lookup and removal. The fundamental architecture stays the same."

---

## PART 7 — The Hard Questions

These questions test your honesty and self-awareness. Never pretend limitations don't exist.

### "What are the limitations of your current design?"

> "I know of three main ones. First, task persistence — tasks exist only in memory. If the master crashes, the queue is lost. A production system would persist tasks to a database. Second, no heartbeat mechanism — if a worker dies silently while idle, the master doesn't know until it tries to send the next task. I detect disconnects only on send/receive, not proactively. Third, no authentication — any process that connects to port 8080 is treated as a worker. In production you'd want authentication tokens."

### "What would you do differently if you started over?"

> "I'd add task IDs from day one — assign a sequential ID to each task and log it at every step. Right now you can't easily correlate 'task dispatched' with 'result received' in the logs across master and worker. I'd also design a proper client protocol separate from the worker protocol — right now the Python client is a protocol demonstration, not a full task submission client. And I'd add a task timeout — if a worker is running a command for more than N seconds, the master should kill it and re-queue."

### "What was the hardest part to get right?"

> "The fork-pipe-dup2-exec sequence. The order of operations matters critically. If you close a pipe end at the wrong time, you get a hung read. If you dup2 before closing unused ends, you leak descriptors. If you call exit() instead of _exit() in the child, you get double-flushed stdio buffers. I had to think carefully about the file descriptor table state at each step and draw it out before the code was correct."

### "Have you tested this?"

> "I've done functional testing — running master with multiple workers, submitting tasks, verifying outputs, testing the queue behavior when all workers are busy, testing Docker deployment, and testing graceful shutdown. The CI pipeline on GitHub Actions builds both the Make and CMake versions plus the Docker image on every commit. I haven't written unit tests for the individual components — that would be my next addition, using Google Test for the protocol layer and the TaskExecutor."

---

## PART 8 — Confidence Builders

### Phrases That Sound Good in Interviews

When explaining a choice:
- *"The reason I went with X over Y is..."*
- *"This is a deliberate trade-off — X gives us A at the cost of B. At this scale, that's the right call."*
- *"The industry standard here is X. I went with Y because this project is specifically about demonstrating the underlying mechanism."*

When asked about a known limitation:
- *"That's a great point — it's a known limitation. The fix would be..."*
- *"I've thought about this. Right now it works this way because... The production approach would be..."*

When asked about something you're not 100% sure of:
- *"My understanding is... but I'd verify that before implementing it in a production system."*
- *"I know the behavior I observe is X. The underlying reason, I believe, is Y — would you like me to reason through it?"*

When asked how to scale:
- *"The current design is correct for the stated scale. To scale to N, I'd change these three things..."*
- Always answer: what changes, why each change helps, what new problems the change introduces.

### Things Never to Say

- "I just used a tutorial for that part" — even if true, say what you learned from it
- "I'm not sure what that is" without following up — follow with what you DO know
- "It's complex" without explaining it — everything has an explanation
- "I used this library/thing but haven't looked inside it" — know your dependencies

---

## PART 9 — One-Sentence Quick Reference

If your mind goes blank, these are your lifelines. One sentence each.

| Term | Say This |
|---|---|
| **TCP** | A reliable, ordered byte stream between two endpoints — like a phone call where nothing gets dropped |
| **Socket** | An integer (file descriptor) that represents one end of a network connection |
| **bind()** | Attaches a socket to a specific IP address and port on this machine |
| **listen()** | Marks a socket as a server socket that accepts incoming connections |
| **accept()** | Blocks until a client connects and returns a new socket just for that client |
| **select()** | A system call that monitors multiple file descriptors and returns when any of them are ready |
| **fork()** | Creates a new process that's an exact copy of the calling process |
| **exec()** | Replaces the current process's code and memory with a new program |
| **pipe()** | Creates a one-way channel — write end in, read end out |
| **dup2()** | Makes one file descriptor point to the same resource as another |
| **waitpid()** | Waits for a child process to exit and retrieves its exit code |
| **RAII** | Constructor acquires a resource, destructor releases it — automatic, even on exceptions |
| **Move semantics** | Transfer ownership of a resource from one object to another without copying it |
| **Wire protocol** | The exact binary format two processes use to communicate over a network |
| **Length-prefix framing** | Sending a length header before each message so the receiver knows how many bytes to read |
| **Network byte order** | Big-endian format used for multi-byte integers in network protocols — htonl() converts to it |
| **Docker image** | A read-only filesystem snapshot that serves as the blueprint for containers |
| **Docker container** | A running instance of an image — isolated process with its own filesystem and network |
| **Multi-stage build** | Using multiple FROM stages in a Dockerfile to separate build-time and runtime dependencies |
| **docker-compose** | A tool to define and run multi-container applications from a single YAML file |
| **Healthcheck** | A command Docker runs periodically to verify a container is functional, not just running |
| **Bridge network** | A software-defined network in Docker where containers can reach each other by service name |
| **getaddrinfo()** | Resolves both IP strings and hostnames to socket addresses — DNS-aware |
| **SIGINT** | Signal sent by Ctrl+C — "interrupt" signal, default action is termination |
| **volatile sig_atomic_t** | The only type the C++ standard guarantees is safe to access from a signal handler |
| **CMake** | A build system generator that produces Makefiles, Visual Studio projects, etc. from one config |
| **std::mutex** | A mutual exclusion primitive — only one thread can hold it at a time |
| **std::lock_guard** | RAII wrapper for a mutex — acquires on construction, releases on destruction |
| **Zombie process** | A terminated child process whose exit status hasn't been read by waitpid() |
| **isatty()** | Returns true if a file descriptor is connected to a terminal — used to detect Docker/CI environments |

---

## PART 10 — The Night Before Checklist

Do these in order the night before an interview:

- [ ] Read Part 1 (the mental movie) twice — close your eyes and see the restaurant analogy clearly
- [ ] Say the 30-second pitch out loud three times without stopping
- [ ] Say the end-to-end walkthrough out loud once — it should take about 3 minutes
- [ ] Read through the analogies in Part 4 — make sure each one feels natural to you
- [ ] Read the "why" scripts in Part 5 for the three things you're most likely to be asked about
- [ ] Read the hard questions in Part 7 — don't skip these
- [ ] Review the quick reference in Part 9 until no term is unfamiliar

**Final confidence check:** Can you answer "walk me through your project" for 5 minutes without checking notes? If yes, you're ready.

---

*This guide is your spoken preparation. The project_bible.md is your technical reference. You don't need to memorize every line — you need to understand every concept deeply enough to explain it in your own words.*
