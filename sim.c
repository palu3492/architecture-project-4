#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#define NUMMEMORY 65536 /* maximum number of data words in memory */
#define NUMREGS 8 /* number of machine registers */

#define ADD 0
#define NAND 1
#define LW 2
#define SW 3
#define BEQ 4
#define JALR 5
#define HALT 6
#define NOOP 7

#define NOOPINSTRUCTION 0x1c00000

typedef struct statestruct {
	int pc;
	int mem[NUMMEMORY];
	int reg[NUMREGS];
	int nummemory;
} statetype;

int field0(int instruction){
	return( (instruction>>19) & 0x7);
}

int field1(int instruction){
	return( (instruction>>16) & 0x7);
}

int field2(int instruction){
	return(instruction & 0xFFFF);
}

int opcode(int instruction){
	return(instruction>>22);
}

/*
 cache_to_processor: reading data from the cache to the processor
 processor_to_cache: writing data from the processor to the cache
 memory_to_cache: reading data from the memory to the cache
 cache_to_memory: evicting cache data by writing it to the memory
 cache_to_nowhere: evicting cache data by throwing it away
*/
enum action_type {cache_to_processor, processor_to_cache, memory_to_cache, cache_to_memory, cache_to_nowhere};

void print_action(int address, int size, enum action_type type) {
    printf("transferring word [%i-%i] ", address, address + size - 1);
    if (type == cache_to_processor) {
        printf("from the cache to the processor\n");
    } else if (type == processor_to_cache) {
        printf("from the processor to the cache\n");
    } else if (type == memory_to_cache) {
        printf("from the memory to the cache\n");
    } else if (type == cache_to_memory) {
        printf("from the cache to the memory\n");
    } else if (type == cache_to_nowhere) {
        printf("from the cache to nowhere\n");
    }
}

/*
void printstate(statetype *stateptr){
	int i;
	printf("\n@@@\nstate:\n");
	printf("\tpc %d\n", stateptr->pc);
	printf("\tmemory:\n");
	for(i = 0; i < stateptr->nummemory; i++){
		printf("\t\tmem[%d]=%d\n", i, stateptr->mem[i]);
	}	
	printf("\tregisters:\n");
	for(i = 0; i < NUMREGS; i++){
		printf("\t\treg[%d]=%d\n", i, stateptr->reg[i]);
	}
	printf("end state\n");
}
*/

/*
The three sources of address references are instruction fetch, lw, and sw.

Each memory address reference should be passed to the cache simulator. The cache simulator keeps
track of what blocks are currently in the cache and what state they are in (e.g. dirty, valid, etc.). To
service the address reference, the cache simulator may need to write back a dirty cache block to
memory, then it may need to read a block into the cache from memory. After these possible steps, the
cache simulator should return the data to the processor (for read accesses) or write the data to the cache
(for write accesses). Each of these data transfers will be logged by calling the print_action function.
 */
/*
  The cache simulator keeps track of what blocks are currently in the cache and what state they are in (e.g. dirty, valid, etc.).
*/


// Block in set is called a way
typedef struct waystruct {
    int valid;
    int dirty;
    int tag;
    int *data;
} waytype;

typedef struct setstruct {
    waytype** ways;
    int lru;
} settype;

typedef struct cachestruct {
    // Each set will have its own LRU
    // what blocks are in the cache and what state they are in
    int block_size_in_words;
    int number_of_sets;
    int associativity;
    int size;

    int number_of_offset_bits;
    int number_of_set_bits;
    int number_of_tag_bits;

    settype** sets;
} cachetype;

unsigned int get_offset(unsigned int address, cachetype* cache){
    unsigned int offset;
    offset = address << (cache->number_of_set_bits + cache->number_of_tag_bits);
    offset = offset >> (cache->number_of_set_bits + cache->number_of_tag_bits);
    return offset;
}

unsigned int get_set(unsigned int address, cachetype* cache){
    unsigned int set;
    set = address << (cache->number_of_tag_bits);
    set = set >> (cache->number_of_offset_bits + cache->number_of_tag_bits);
    return set;
}

unsigned int get_tag(unsigned int address, cachetype* cache){
    unsigned int tag;
    tag = address >> (cache->number_of_offset_bits + cache->number_of_set_bits);
    return tag;
}

// returns way (block) in set where data will be
int find_way(int address, int set, int tag, cachetype* cache){
    // check if memory is already in cache
    // Look through all ways in set
    for(int i=0; i<cache->associativity; i++){
        int way_tag = cache->sets[set]->ways[i]->tag;
        int way_valid = cache->sets[set]->ways[i]->valid; // data present or not
        if(way_valid && way_tag == tag){
            cache->sets[set]->ways[i]->tag = tag;
            cache->sets[set]->ways[i]->valid = 1;
            return i;
        }
    }
    // If tag not found in set
    // try to place in open set way
    for (int i = 0; i < cache->associativity; i++) {
        int way_valid = cache->sets[set]->ways[i]->valid;
        if (!way_valid) {
            return i;
        }
    }
    // no free ways, replace one
    return cache->sets[set]->lru;
}

int cache_load_block(cachetype* cache, statetype* state, int offset, int set, int tag, int address){
    int way = find_way(address, set, tag, cache);
    int start_of_block = (address / cache->block_size_in_words) * cache->block_size_in_words; // integer division
    if(cache->sets[set]->ways[way]->dirty){
        // write to memory first
        print_action(start_of_block, cache->block_size_in_words, cache_to_memory);
    }
    if(cache->sets[set]->ways[way]->tag != tag){
        print_action(start_of_block, cache->block_size_in_words, memory_to_cache);
        memcpy(cache->sets[set]->ways[way]->data, state->mem + start_of_block, cache->block_size_in_words * sizeof(int));
    }
    cache->sets[set]->ways[way]->tag = tag;
    cache->sets[set]->ways[way]->valid = 1;
    cache->sets[set]->ways[way]->dirty = 0;
    print_action(address, 1, cache_to_processor);

    return cache->sets[set]->ways[way]->data[offset];
}

void cache_write_block(cachetype* cache, statetype* state, int offset, int set, int tag, int destination, int source){
//    int way = find_way(address, set, tag, cache);
//    if(cache->sets[set]->ways[way]->dirty){
//        // write to memory first
//        print_action(address, cache->block_size_in_words, cache_to_memory);
//    }
//    if(cache->sets[set]->ways[way]->tag != tag){
//        print_action(address, cache->block_size_in_words, memory_to_cache);
//        int start_of_block = (address / cache->block_size_in_words) * cache->block_size_in_words; // integer division
//        memcpy(cache->sets[set]->ways[way]->data, state->mem + start_of_block, cache->block_size_in_words * sizeof(int));
//    }
//    cache->sets[set]->ways[way]->tag = tag;
//    cache->sets[set]->ways[way]->valid = 1;
//    cache->sets[set]->ways[way]->dirty = 0;
//    print_action(address, 1, cache_to_processor);

}

int cache_read(cachetype* cache, statetype* state, int address){
    int offset = get_offset(address, cache);
    int set = get_set(address, cache);
    int tag = get_tag(address, cache);

    return cache_load_block(cache, state, offset, set, tag, address);
}

// 1. memory already in cache
// 2. memory not in cache
    // 2.1 memory moved into free cache way
    // 2.2 replace a cache way with this one
        // if dirty then write
void cache_write(cachetype* cache, statetype* state, int destination, int source){
    int offset = get_offset(destination, cache);
    int set = get_set(destination, cache);
    int tag = get_tag(destination, cache);

    cache_write_block(cache, state, offset, set, tag, destination, source);
}

int signextend(int num){
	// convert a 16-bit number into a 32-bit integer
	if (num & (1<<15) ) {
		num -= (1<<16);
	}
	return num;
}

void run(statetype* state, cachetype* cache){

	// Reused variables;
	int instr = 0;
	int regA = 0;
	int regB = 0;
	int offset = 0;
	int branchtarget = 0;
	int aluresult = 0;

	// Primary loop
	while(1){

		// Fetch instruction from cache
		instr = cache_read(cache, state, state->pc);

		// printf("instr: %d\n", instr);
		// printf("opcode: %d\n", opcode(instr));

		/* check for halt */
		if (opcode(instr) == HALT) {
			// printf("machine halted\n");
			break;
		}

		// Increment the PC
		state->pc = state->pc+1;

		// Set reg A and B
		regA = state->reg[field0(instr)];
		regB = state->reg[field1(instr)];

		// Set sign extended offset
		offset = signextend(field2(instr));

		// Branch target gets set regardless of instruction
		branchtarget = state->pc + offset;

		/**
		 *
		 * Action depends on instruction
		 *
		 **/

		// ADD
		if(opcode(instr) == ADD){
			// Add
			aluresult = regA + regB;
			// Save result
			state->reg[field2(instr)] = aluresult;
		}
		// NAND
		else if(opcode(instr) == NAND){
			// NAND
			aluresult = ~(regA & regB);
			// Save result
			state->reg[field2(instr)] = aluresult;
		}
		// LW or SW
		else if(opcode(instr) == LW || opcode(instr) == SW){
			// Calculate memory address
			aluresult = regB + offset;
			if(opcode(instr) == LW){
				// Load
				int mem = cache_read(cache, state, aluresult);
				state->reg[field0(instr)] = mem;
			}else if(opcode(instr) == SW){
				// Store
				// state->mem[aluresult] = regA;
                cache_write(cache, state, aluresult, regA);
			}
		}
		// JALR
		else if(opcode(instr) == JALR){
			// Save pc+1 in regA
			state->reg[field0(instr)] = state->pc;
			//Jump to the address in regB;
			state->pc = state->reg[field1(instr)];
		}
		// BEQ
		else if(opcode(instr) == BEQ){
			// Calculate condition
			aluresult = (regA == regB);
			// ZD
			if(aluresult){
				// branch
				state->pc = branchtarget;
			}
		}
	} // While
}

int is_power_of_two(int number) {
    return (number & (number - 1)) == 0;
}

int main(int argc, char** argv){

    /** Get command line arguments **/
    char* file_name = (char*)malloc(sizeof(char)*100);
    int block_size_in_words = 0;
    int number_of_sets = 0;
    int associativity = 0;

    int option;
    opterr = 0;
    // Iterate over the supplied command line options
    // If -i or -o then store supplied file name
    while ((option = getopt(argc, argv, "f:b:s:a:")) != -1) {
        switch(option) {
            case 'f':
                memcpy(file_name, optarg, strlen(optarg));
                break;
            case 'b':
                block_size_in_words = atoi(optarg);
                break;
            case 's':
                number_of_sets = atoi(optarg);
                break;
            case 'a':
                associativity = atoi(optarg);
                break;
            case '?':
                printf("unknown option: `%c`\n", optopt);
            default:
                break;
        }
    }

    /*
     Enter the machine code program to simulate:
     - file_name
     Enter the block size of the cache (in words):
     - block_size_in_words
     Enter the number of sets in the cache:
     - number_of_sets
     Enter the associativity of the cache:
     - associativity
    */

    while(strlen(file_name) == 0){
        printf("Enter the name of the machine code file to simulate:\n");
        fgets(file_name, 100, stdin);
        file_name[strlen(file_name)-1] = '\0'; // gobble up the \n with a \0
    }
    while(block_size_in_words < 1 || !is_power_of_two(block_size_in_words)){
        printf("Enter the block size of the cache (in words):\n");
        scanf("%d",&block_size_in_words);
    }
    while(number_of_sets < 1 || !is_power_of_two(number_of_sets)){
        printf("Enter the number of sets in the cache:\n");
        scanf("%d",&number_of_sets);
    }
    while(associativity < 1 || !is_power_of_two(associativity)){
        printf("Enter the associativity of the cache:\n");
        scanf("%d",&associativity);
    }

    // printf("Inputs: %s, %d, %d, %d\n", file_name, block_size_in_words, number_of_sets, associativity);

	FILE *fp = fopen(file_name, "r");
	if (fp == NULL) {
		printf("Cannot open file '%s' : %s\n", file_name, strerror(errno));
		return -1;
	}

	// count the number of lines by counting newline characters
	int line_count = 0;
	int c;
	while (EOF != (c=getc(fp))) {
		if ( c == '\n' ){
			line_count++;
		}
	}
	// reset fp to the beginning of the file
	rewind(fp);

	statetype* state = (statetype*)malloc(sizeof(statetype));

	state->pc = 0;
	memset(state->mem, 0, NUMMEMORY*sizeof(int));
	memset(state->reg, 0, NUMREGS*sizeof(int));

	state->nummemory = line_count;

	char line[256];

	int i = 0;

	while (fgets(line, sizeof(line), fp)) {
		//note that fgets doesn't strip the terminating \n, checking its
        // presence would allow to handle lines longer that sizeof(line)
		state->mem[i] = atoi(line);
		i++;
	}
	fclose(fp);

	// Cache

    cachetype* cache = (cachetype*)malloc(sizeof(cachetype));
    cache->block_size_in_words = block_size_in_words;
    cache->number_of_sets =  number_of_sets;
    cache->associativity = associativity;
    cache->size = block_size_in_words * number_of_sets * associativity;
    int number_of_offset_bits = ceil( log(cache->block_size_in_words) / log(2) );
    int number_of_set_bits = ceil( log(cache->number_of_sets) / log(2) );
    int number_of_tag_bits = 32 - number_of_offset_bits - number_of_set_bits;
    cache->number_of_offset_bits = number_of_offset_bits;
    cache->number_of_set_bits = number_of_set_bits;
    cache->number_of_tag_bits = number_of_tag_bits;

    cache->sets = (settype**)malloc(sizeof(settype*) * number_of_sets);
    for(i=0; i<number_of_sets; i++){
        cache->sets[i] = (settype*)malloc(sizeof(settype));
        cache->sets[i]->lru = 0;
        for(int w=0; w<associativity; w++){
            cache->sets[i]->ways = (waytype**)malloc(sizeof(waytype*) * associativity);
            cache->sets[i]->ways[w] = (waytype*)malloc(sizeof(waytype));
            cache->sets[i]->ways[w]->dirty = 0;
            cache->sets[i]->ways[w]->valid = 0;
            cache->sets[i]->ways[w]->tag = -1;
            // holds way (block) data from memory
            cache->sets[i]->ways[w]->data = (int*)malloc(sizeof(int) * block_size_in_words);
        }
    }

//    for(i=0; i<number_of_sets; i++){
//        for(int w=0; w<associativity; w++){
//            printf("\nDirty: %d\n", cache->sets[i]->ways[w]->tag);
//        }
//    }



	// Run the simulation
	 run(state, cache);

	free(state);
	free(file_name);
}
