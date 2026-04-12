# Makefile — Distributed Task Scheduler (C++)

CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread
TARGETS  = master worker
HEADERS  = protocol.hpp logger.hpp

all: $(TARGETS)

master: master.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o master master.cpp

worker: worker.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f $(TARGETS)

.PHONY: all clean
