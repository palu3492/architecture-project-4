CC=gcc

all: sim.o
	$(CC) sim.o -o sim
	
sim: sim.c
	$(CC) sim.c -c sim.o
	
clean:
	rm sim.o sim
