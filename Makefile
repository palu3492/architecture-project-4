CC=gcc

all: cache_sim.o
	$(CC) cache_sim.o -o cache_sim -lm
	
sim: cache_sim.c
	$(CC) cache_sim.c -c cache_sim.o -lm
	
clean:
	rm cache_sim.o cache_sim
