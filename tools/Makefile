CC = gcc
LIBS = -lm

CXX = g++
CXXFLAGS = -std=c++17 -O2

.PHONY: all
all: renum reval p2txt

renum: renum.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

reval: reval.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

p2txt: p2txt.c
	$(CC) $< -o $@ $(LIBS)

.PHONY: clean
clean: 
	${RM} renum reval p2txt

