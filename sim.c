#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

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

int signextend(int num){
	// convert a 16-bit number into a 32-bit integer
	if (num & (1<<15) ) {
		num -= (1<<16);
	}
	return num;
}

/*
void print_stats(int n_instrs){
	printf("INSTRUCTIONS: %d\n", n_instrs);
}
*/

void run(statetype* state){

	// Reused variables;
	int instr = 0;
	int regA = 0;
	int regB = 0;
	int offset = 0;
	int branchtarget = 0;
	int aluresult = 0;

	int total_instrs = 0;

	// Primary loop
	while(1){
		total_instrs++;

		// printstate(state);

		// Instruction Fetch
		instr = state->mem[state->pc];

		/* check for halt */
		if (opcode(instr) == HALT) {
			printf("machine halted\n");
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
				state->reg[field0(instr)] = state->mem[aluresult];
			}else if(opcode(instr) == SW){
				// Store
				state->mem[aluresult] = regA;
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
	// print_stats(total_instrs);
}

int main(int argc, char** argv){

    /** Get command line arguments **/
    char* file_name = (char*)malloc(sizeof(char)*100);
    int block_size_in_words = -1;
    int number_of_sets = -1;
    int associativity = -1;

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
    while(block_size_in_words == -1){
        printf("Enter the block size of the cache (in words):\n");
        scanf("%d",&block_size_in_words);
    }
    while(number_of_sets == -1){
        printf("Enter the number of sets in the cache:\n");
        scanf("%d",&number_of_sets);
    }
    while(associativity == -1){
        printf("Enter the associativity of the cache:\n");
        scanf("%d",&associativity);
    }

    printf("Inputs: %s, %d, %d, %d\n", file_name, block_size_in_words, number_of_sets, associativity);

    /*
	if(argc == 1){
		file_name = (char*)malloc(sizeof(char)*100);
		printf("Enter the name of the machine code file to simulate: ");
		fgets(file_name, 100, stdin);
		file_name[strlen(file_name)-1] = '\0'; // gobble up the \n with a \0
	}
	else if (argc == 2){

		int strsize = strlen(argv[1]);

		file_name = (char*)malloc(strsize);
		file_name[0] = '\0';

		strcat(file_name, argv[1]);
	}else{
		printf("Please run this program correctly\n");
		exit(-1);
	}

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

	// Run the simulation
	run(state);

	free(state);
	free(file_name);
    */
}
