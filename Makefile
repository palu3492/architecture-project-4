CC=gcc

all: sim.o
	$(CC) sim.o -o sim -lm
	
sim: sim.c
	$(CC) sim.c -c sim.o -lm
	
clean:
	rm sim.o sim
