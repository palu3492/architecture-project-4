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

// Block in set is called a way
typedef struct entrystruct {
    int valid;
    int dirty;
    int tag;
    int *data;
    int last_used;
    int address;
} entrytype;

typedef struct setstruct {
    entrytype** entries;
} settype;

typedef struct cachestruct {
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
    if(cache->number_of_sets == 1){
        return 0;
    }
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

void increment_entries_lru(cachetype* cache, int set){
    for(int way = 0; way < cache->associativity; way++){
        if(cache->sets[set]->entries[way]->valid){
            cache->sets[set]->entries[way]->last_used++;
        }
    }
}

// Finds least recently used entry in a set
// Looks at each set and finds one with lowest last used counter
// Lowest last used will be entry that
int find_lru(cachetype* cache, int set){
    int lru = 0;
    int highest = 0;
    for(int way = 0; way < cache->associativity; way++){
        int last_used = cache->sets[set]->entries[way]->last_used;
        if(last_used > highest){
            highest = last_used;
            lru = way;
        }
    }
    return lru;
}

// returns entry (way/index) in set where data will be
// returns just the index where data will be, data may or may not be there currently
int find_entry(int set, int tag, cachetype* cache){
    // check if memory is already in cache
    // Look through all entries in set
    for(int way = 0; way < cache->associativity; way++){
        int entry_tag = cache->sets[set]->entries[way]->tag;
        int entry_valid = cache->sets[set]->entries[way]->valid; // data present or not
        if(entry_valid && entry_tag == tag){
            cache->sets[set]->entries[way]->tag = tag;
            cache->sets[set]->entries[way]->valid = 1;
            return way;
        }
    }
    // If tag not found in set
    // try to place in open set entry
    for (int i = 0; i < cache->associativity; i++) {
        int entry_valid = cache->sets[set]->entries[i]->valid;
        if (!entry_valid) {
            return i;
        }
    }
    // no free entries, replace one
    return find_lru(cache, set);
}

// When the program is halted write any dirty entries to memory and invalidate all entries
void clean_up_cache(cachetype* cache, statetype* state){
    // Loop over all entries in each set and write any entries to memory that are dirty
    for(int set = 0; set < cache->number_of_sets; set++) {
        for (int way = 0; way < cache->associativity; way++) {
            if (cache->sets[set]->entries[way]->dirty) {
                // Write cache set entry block to memory
                memcpy(state->mem + cache->sets[set]->entries[way]->address, cache->sets[set]->entries[way]->data,
                       cache->block_size_in_words * sizeof(int));
                print_action(cache->sets[set]->entries[way]->address, cache->block_size_in_words, cache_to_memory);
            }
            // invalidate all entries
            cache->sets[set]->entries[way]->valid = 0;
        }
    }
}

// Loads block of data into set entry
// if entry already in cache then do nothing
// If not then replace what's there with new entry
// If whats there is dirty then write it to memory first
void load_entry(cachetype* cache, statetype* state, int way, int set, int tag, int address){
    if(!cache->sets[set]->entries[way]->valid || cache->sets[set]->entries[way]->tag != tag){
        // write cache to memory first
        if(cache->sets[set]->entries[way]->dirty){
            // Write cache set entry block to memory
            memcpy(state->mem + cache->sets[set]->entries[way]->address, cache->sets[set]->entries[way]->data, cache->block_size_in_words * sizeof(int));
            print_action(cache->sets[set]->entries[way]->address, cache->block_size_in_words, cache_to_memory);
            cache->sets[set]->entries[way]->dirty = 0;
        } else if(cache->sets[set]->entries[way]->valid){
            // Clear cache set entry block
            print_action(cache->sets[set]->entries[way]->address, cache->block_size_in_words, cache_to_nowhere);
        }

        int start_of_block = (address / cache->block_size_in_words) * cache->block_size_in_words; // integer division
        print_action(start_of_block, cache->block_size_in_words, memory_to_cache);
        // write memory to cache
        memcpy(cache->sets[set]->entries[way]->data, state->mem + start_of_block, cache->block_size_in_words * sizeof(int));
        cache->sets[set]->entries[way]->address = start_of_block;
    }
}

int cache_read(cachetype* cache, statetype* state, int address){
    int offset = get_offset(address, cache);
    int set = get_set(address, cache);
    int tag = get_tag(address, cache);

    increment_entries_lru(cache, set);
    int way = find_entry(set, tag, cache);
    load_entry(cache, state, way, set, tag, address);
    cache->sets[set]->entries[way]->last_used = 0;

    // Read specific
    cache->sets[set]->entries[way]->tag = tag;
    cache->sets[set]->entries[way]->valid = 1;

    print_action(address, 1, cache_to_processor);

    return cache->sets[set]->entries[way]->data[offset];
}

void cache_write(cachetype* cache, statetype* state, int destination, int *source){
    int offset = get_offset(destination, cache);
    int set = get_set(destination, cache);
    int tag = get_tag(destination, cache);

    increment_entries_lru(cache, set);
    int way = find_entry(set, tag, cache);
    load_entry(cache, state, way, set, tag, destination);
    cache->sets[set]->entries[way]->last_used = 0;

    // Write specific
    cache->sets[set]->entries[way]->tag = tag;
    cache->sets[set]->entries[way]->valid = 1;
    cache->sets[set]->entries[way]->dirty = 1;
    // write processor data to cache
    print_action(destination, 1, processor_to_cache);
    memcpy(cache->sets[set]->entries[way]->data + offset, source, sizeof(int));
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

		/* check for halt */
		if (opcode(instr) == HALT) {
            clean_up_cache(cache, state);
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
                cache_write(cache, state, aluresult, &regA);
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
    while(1){
        while(number_of_sets < 1 || !is_power_of_two(number_of_sets)){
            printf("Enter the number of sets in the cache:\n");
            scanf("%d",&number_of_sets);
        }
        while(associativity < 1 || !is_power_of_two(associativity)){
            printf("Enter the associativity of the cache:\n");
            scanf("%d",&associativity);
        }
        if(number_of_sets * associativity > 256){
            number_of_sets = 0;
            associativity = 0;
        } else {
            break;
        }
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


	/*
	 * Cache
	 */
    cachetype* cache = (cachetype*)malloc(sizeof(cachetype));
    cache->block_size_in_words = block_size_in_words;
    cache->number_of_sets =  number_of_sets;
    cache->associativity = associativity;
    cache->size = block_size_in_words * number_of_sets * associativity;
    int number_of_offset_bits = ceil( log(block_size_in_words) / log(2) );
    int number_of_set_bits = ceil( log(number_of_sets) / log(2) );
    int number_of_tag_bits = 32 - number_of_offset_bits - number_of_set_bits;
    cache->number_of_offset_bits = number_of_offset_bits;
    cache->number_of_set_bits = number_of_set_bits;
    cache->number_of_tag_bits = number_of_tag_bits;

    cache->sets = (settype**)malloc(sizeof(settype*) * number_of_sets);
    for(i = 0; i < number_of_sets; i++){
        cache->sets[i] = (settype*)malloc(sizeof(settype));
        cache->sets[i]->entries = (entrytype**)malloc(sizeof(entrytype*) * associativity);
        for(int w = 0; w < associativity; w++){
            cache->sets[i]->entries[w] = (entrytype*)malloc(sizeof(entrytype));
            cache->sets[i]->entries[w]->dirty = 0;
            cache->sets[i]->entries[w]->valid = 0;
            cache->sets[i]->entries[w]->tag = 0;
            cache->sets[i]->entries[w]->last_used = 0;
            // holds way (block) data from memory
            cache->sets[i]->entries[w]->data = (int*)malloc(sizeof(int) * block_size_in_words);
        }
    }

	// Run the simulation
    run(state, cache);

    free(cache);
	free(state);
	free(file_name);

	return 1;
}
