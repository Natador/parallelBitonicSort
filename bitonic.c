/*
 * bitonic.c
 * 	A C program written for an MPI environment which implements Bitonic
 * 	Sort on an array of 'n' integers using 'm' processors. It is assumed
 * 	that both 'n' and 'm' are powers of 2. The integers are generated within
 * 	the program using a random number generator.
 *
 * Author: Nathaniel Cantwell
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <sys/time.h>
#include "mpi.h"

#define MASTER 0
#define SIZE 1024
#define TASKS 4 	// Number of tasks/processors
#define STEPS 2 	// log2(TASKS), the number of "steps" required for bitonic sort.

void comp_exchange_arr(int myid, int minid, int maxid, int arr[], int size);
void comp_exchange_min(int myid, int maxid, int arr[], int size);
void comp_exchange_max(int myid, int minid, int arr[], int size);
void sort(int arr[], int size);
void generate_random_arr(int arr[], int size);
void quit(int code);
int timedif(struct timeval start, struct timeval end);
void parallel_print(int taskid, int numTasks, int arr[], int size);

int main(int argc, char* argv[]) {
	// Seed random number generator
	srand(42);

	// Variables to store processor task id and number of tasks
	int numTasks, taskid, i;

	// Size and array length variable
	int size, loc_size, len, loc_len;

	// File pointer to result file
	FILE* resfp;
	char resfilename[50] = "result.csv";

	// Filename and pointer to timing file
	FILE* timefp;
	char timefname[] = "/gpfs/home/n/a/nadacant/BigRed2/Tasks/Task6/timing.csv";

	// Arrays to hold the integers, both in the master and in the slaves
	int *start_arr;
	int *local_arr;
	int *end_arr;

	// Structs for timing
	struct timeval start, end;
	int microsec;

	// Initialize MPI environment
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &numTasks);
	MPI_Comm_rank(MPI_COMM_WORLD, &taskid);

	// Get size from argument list or use default
	if (argc < 2) {
		if (taskid == MASTER)
			printf("No size parameter given. Using default %d\n", SIZE);
		size = SIZE;
	} else {
		size = atoi(argv[1]);
		if (size <= 0) {
			if (taskid == MASTER)
				printf("Invalid size argument. Using default %d\n", SIZE);
			size = SIZE;
		}
	}

	if (taskid == MASTER) printf("size = %d\n", size);

	// Generate the random sequence in the master array
	int badfp = 0;
	if (taskid == MASTER) {
		// Construct result filename and open result file
		sprintf(resfilename, "res_%dnodes_%dsize.csv", numTasks, size);

		// Open result file
		resfp = fopen(resfilename, "w");
		if (resfp == NULL) {
			printf("Error opening result file.\n");
			badfp = 1;
		}

		// Open timing file
		timefp = fopen(timefname, "a");
		if (timefp == NULL) {
			printf("Error opening timing file.\n");
			badfp = 1;
		}
		
		// Allocate space for array
		start_arr = (int*) malloc(size*sizeof(int));
		generate_random_arr(start_arr, size);

		// Print original array to file
		for (i = 0; i < size-1; i++)
			fprintf(resfp, "%d,", start_arr[i]);
		fprintf(resfp, "%d\n", start_arr[size-1]);

		// Allocate space for result array
		end_arr = (int*) malloc(size*sizeof(int));
	}

	// Broadcast status of file opening to nodes
	MPI_Bcast(&badfp, 1, MPI_INT, MASTER, MPI_COMM_WORLD);
	
	// Check for failure
	if (badfp) quit(1);

	// Allocate space for local_arr
	loc_size = size/numTasks;
	local_arr = (int*) malloc(loc_size*sizeof(int));
	
	// Scatter array to all tasks
	MPI_Scatter(start_arr, loc_size, MPI_INT, local_arr, loc_size, MPI_INT, MASTER, MPI_COMM_WORLD);

	/*** Bitonic Sort section ***/

	int group; 				// Current "group" based on "step"
	int subgroup; 			// Current subgroup based on substep
	int step, maxsteps; 	// Current "step" in bitonic sort and maximum step
	int substep; 			// Smaller steps, where the two bitonic sequences are merged into a larger sorted sequence
	int upperhalf; 			// Determines if the processor is in the upper or lower half of the group
	int partner; 			// Partner for compare-exchange

	// Start timing sorting
	MPI_Barrier(MPI_COMM_WORLD);
	gettimeofday(&start, NULL);

	// Sort the local array serially
	sort(local_arr, loc_size);

	// Calculate maximum number of steps
	maxsteps = log2(numTasks);

	// Iterate through large steps (dictates group size)
	for (step = 1; step <= maxsteps; step++) {
		// Determine current group. Group size is 2^step
		group = (int)(taskid/pow(2, step));

		// Iterate through smaller steps (exchange within group, merge bitonic sequences)
		for (substep = step; substep > 0; substep--) {
			// Determine if processor is in upper or lower half of subgroup (partner less than or greater than)
			subgroup = (int)(taskid/pow(2, substep));
			upperhalf = (int)( (taskid - subgroup*pow(2.0, substep)) >= pow(2.0, substep-1) );

			// Determine partner based on upperhalf parameter
			MPI_Barrier(MPI_COMM_WORLD);
			if (upperhalf) {
				partner = taskid - pow(2.0, substep-1);
			} else {
				partner = taskid + pow(2.0, substep-1);
			}

			// Determine if sorting order should be min or max
			if (group % 2 == 0) { 	// Min-order case
				// Lower partner gets min in the min-order case
				if (taskid < partner)
					comp_exchange_min(taskid, partner, local_arr, loc_size);
				else
					comp_exchange_max(taskid, partner, local_arr, loc_size);
			} else { 				// Max-order case
				// Higher partner gets min in the max-order case
				if (taskid > partner)
					comp_exchange_min(taskid, partner, local_arr, loc_size);
				else
					comp_exchange_max(taskid, partner, local_arr, loc_size);
			}

			// Synchronize
			MPI_Barrier(MPI_COMM_WORLD);
		}
	}

	// Gather array in master
	MPI_Gather(local_arr, loc_size, MPI_INT, end_arr, loc_size, MPI_INT, MASTER, MPI_COMM_WORLD);

	// Get end time and take difference
	MPI_Barrier(MPI_COMM_WORLD);
	gettimeofday(&end, NULL);
	microsec = timedif(start, end);

	// Print for debugging
	//if (taskid == MASTER) printf("After:\n");
	//parallel_print(taskid, numTasks, local_arr, loc_size);

	// Output sorted array to file
	if (taskid == MASTER) {
		// Print result array to file
		for (i = 0; i < size-1; i++)
			fprintf(resfp, "%d,", end_arr[i]);
		fprintf(resfp, "%d\n", end_arr[size-1]);

		// Print timing information
		printf("time (ms): %lf\n", (double)microsec/1000.0);
		fprintf(resfp, "%lf\n", (double)microsec/1000.0);
		fprintf(timefp, "%d,%d,%lf\n", numTasks, size, (double)microsec/1000.0);

		// Close result file
		fclose(resfp);

		// Close timing file
		fclose(timefp);
	}

	// Free dynamic memory
	free(local_arr);
	if (taskid == MASTER) {
		free(start_arr);
		free(end_arr);
	}

	// Free MPI resources
	MPI_Finalize();
	return 0;
}

// Wrapper function to do compare and exchange and keep the minimum
void comp_exchange_min(int myid, int maxid, int arr[], int size) {
	comp_exchange_arr(myid, myid, maxid, arr, size);
}

// Wrapper function to do compare and exchange and keep the maximum
void comp_exchange_max(int myid, int minid, int arr[], int size) {
	comp_exchange_arr(myid, minid, myid, arr, size);
}

// Compare and exchange an array of numbers. Generic function
void comp_exchange_arr(int myid, int minid, int maxid, int arr[], int size) {
	// Dynamically-sized partner's array
	int* partner_arr = (int*) malloc(size*sizeof(int));

	// Dynamically-sized array to hold the merged arrays
	int* merged_arr = (int*) malloc(2*size*sizeof(int));

	// Status of recv
	MPI_Status stat;

	// Counters for merge
	int i, j, k;

	// Make arrays common to both. Alternate send/recv calls.
	if (myid == minid) {
		MPI_Send(arr, size, MPI_INT, maxid, 0, MPI_COMM_WORLD);
		MPI_Recv(partner_arr, size, MPI_INT, maxid, 0, MPI_COMM_WORLD, &stat);
	} else {
		MPI_Recv(partner_arr, size, MPI_INT, minid, 0, MPI_COMM_WORLD, &stat);
		MPI_Send(arr, size, MPI_INT, minid, 0, MPI_COMM_WORLD);
	}

	// Merge the two arrays into one large array
	j = 0;
	k = 0;
	for (i = 0; i < 2*size; i++) {
		// Check if the indices have hit the limit. Otherwise, take the minimum.
		if (k == size) {
			merged_arr[i] = arr[j++];
		} else if (j == size) {
			merged_arr[i] = partner_arr[k++];
		} else if (arr[j] < partner_arr[k]) {
			merged_arr[i] = arr[j++];
		} else {
			merged_arr[i] = partner_arr[k++];
		}
	}

	// Take the small half if I am minid, or the large half if I am maxid
	if (myid == minid) {
		// Copy the first half (minimum half) of the array
		for (i = 0; i < size; i++)
			arr[i] = merged_arr[i];
	} else {
		// Copy the second half (maximum half) of the array
		for (i = 0; i < size; i++)
			arr[i] = merged_arr[i+size];
	}

	// Free memory for temp arrays
	free(partner_arr);
	free(merged_arr);
}

// Serially sort an array passed by reference. In this case, use bubble sort
void sort(int arr[], int size) {
	int i, swap, swap_occurred;

	// Keep looping/sorting until no swaps occur
	swap_occurred = 1;
	while (swap_occurred) {
		// No swaps have occurred yet
		swap_occurred = 0;

		// Loop through all adjacent pairs of elements in the list
		for (i = 0; i < size - 1; i++) {
			// Swap the ith and (i+1)th element to the correct order
			if (arr[i] > arr[i+1]) {
				swap = arr[i+1];
				arr[i+1] = arr[i];
				arr[i] = swap;

				// Swap occurred, set flag to true
				swap_occurred = 1;
			}
		}
	}
}

// Generates a random array of size 'size' by shuffling the sequence of numbers
//  from zero to (size-1)
void generate_random_arr(int arr[], int size) {
	int i, swap_index;
	int swap;

	// Generate the sequence from zero to 'size'
	for (i = 0; i < size; i++)
		arr[i] = i;

	// Shuffle the array from end to beginning
	for (i = size-1; i > 0; i--) {
		swap_index = rand() % i;
		swap = arr[i];
		arr[i] = arr[swap_index];
		arr[swap_index] = swap;
	}
}

// Function to quit execution with finalize
void quit(int code) {
	MPI_Finalize();
	exit(code);
}

// Calculates the time difference between two different struct timeval structures. Returns the number of microseconds.
int timedif(struct timeval start, struct timeval end) {
	return (end.tv_sec*1000000 + end.tv_usec) - (start.tv_sec*1000000 + start.tv_usec);
}

// Debug function to print a group of arrays in order
void parallel_print(int taskid, int numTasks, int arr[], int size) {
	int i, j;

	// Initial synchronization step
	MPI_Barrier(MPI_COMM_WORLD);
	for (j = 0; j < numTasks; j++) {
		if (taskid == j) {
			printf("Processor %d: ", taskid);
			for (i = 0; i < size; i++)
				printf("%d ", arr[i]);
			printf("\n");
		}
		MPI_Barrier(MPI_COMM_WORLD);
	}
}
