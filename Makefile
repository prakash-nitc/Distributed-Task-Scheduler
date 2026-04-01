# Makefile — Distributed Task Scheduler (C++)

CXX     = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g
TARGETS = master worker

all: $(TARGETS)

master: master.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o master master.cpp

worker: worker.cpp protocol.hpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f $(TARGETS)

.PHONY: all clean
