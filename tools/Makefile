.PHONY: all
all: renum reval p2txt

renum: renum.cpp
	g++ -std=c++17 renum.cpp -O2 -o renum

reval: reval.cpp
	g++ -std=c++17 reval.cpp -O2 -o reval

p2txt: p2txt.c
	gcc p2txt.c -o p2txt -lm

.PHONY: clean
clean: 
	${RM} renum reval p2txt

