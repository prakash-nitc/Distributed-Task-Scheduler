# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY protocol.hpp logger.hpp master.cpp worker.cpp ./

RUN g++ -std=c++17 -Wall -Wextra -O2 -pthread -o master master.cpp \
 && g++ -std=c++17 -Wall -Wextra -O2 -pthread -o worker worker.cpp

# ── Stage 2: Runtime ──────────────────────────────────────────────────────────
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /build/master /build/worker ./
COPY client.py ./

EXPOSE 8080

# Default: run master. Override with: docker run <img> ./worker <master-ip>
CMD ["./master"]
