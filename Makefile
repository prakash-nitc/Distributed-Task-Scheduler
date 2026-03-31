# ============================================================================
#  Makefile — Build System for Distributed Task Scheduler
# ============================================================================
#
#  Usage:
#    make          → Build both master and worker
#    make master   → Build only the master
#    make worker   → Build only the worker
#    make clean    → Remove compiled binaries
#
#  Interview Q: "What does -Wall -Wextra do?"
#  Answer: "They enable all major compiler warnings. -Wall enables the most
#           common warnings, and -Wextra adds additional ones. This catches
#           bugs at compile time instead of runtime."
#
#  Interview Q: "What does -g do?"
#  Answer: "It includes debug symbols in the binary, allowing you to use gdb
#           (GNU Debugger) to step through the code, inspect variables, and
#           set breakpoints."
# ============================================================================

CC      = gcc
CFLAGS  = -Wall -Wextra -g
TARGETS = master worker

# Default target: build everything
all: $(TARGETS)

# Build the Master
master: master.c protocol.h
	$(CC) $(CFLAGS) -o master master.c

# Build the Worker
worker: worker.c protocol.h
	$(CC) $(CFLAGS) -o worker worker.c

# Clean up compiled binaries
clean:
	rm -f $(TARGETS)

.PHONY: all clean
