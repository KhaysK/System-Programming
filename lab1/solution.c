#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include "vector.h"
#include <limits.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>

Vector **vectors;
int vectors_size = 0;
int files_size = 0;
Vector queue;
struct param
{
	uint64_t coro_id;
	uint64_t coro_max_time;
	uint64_t coro_start_time;
	uint64_t coro_total_time;
	uint64_t coro_switch_num;
};

uint64_t getTime()
{
	struct timespec currentTime;
	clock_gettime(CLOCK_MONOTONIC, &currentTime);
	return (uint64_t)(currentTime.tv_sec * 1000000) + (uint64_t)(currentTime.tv_nsec / 1000);
}
/**
	A function that implements the quicksort algorithm to sort the given vector.
	@param vec A pointer to the vector to be sorted
	@return A pointer to the sorted vector
*/
Vector *quick_sort(Vector *vec, struct param *coro_info)
{
	// Base case: vector of size 0 or 1 is already sorted
	if (vec->size < 2)
		return vec;

	// Choose a key element and divide the vector into two sub-vectors
	int key_elem_id = vec->size / 2;
	int key_elem = vec->data[key_elem_id];
	Vector left_vec;
	Vector right_vec;
	vector_init(&left_vec);
	vector_init(&right_vec);

	for (int i = 0; i < vec->size; i++)
	{
		if (i == key_elem_id)
			continue;
		else if (vec->data[i] <= key_elem)
			vector_push_back(&left_vec, vec->data[i]);
		else
			vector_push_back(&right_vec, vec->data[i]);
	}

	// Sort the left and right sub-vectors recursively
	uint64_t working_time = getTime() - coro_info->coro_start_time;

	if (working_time > coro_info->coro_max_time)
	{
		coro_info->coro_total_time += working_time;
		coro_info->coro_switch_num++;
		coro_yield();
		coro_info->coro_start_time = getTime();
	}
	quick_sort(&left_vec, coro_info);
	quick_sort(&right_vec, coro_info);

	// Merge the sorted sub-vectors and the key element
	int i = 0;
	int k = 0;
	while (i < left_vec.size)
	{
		vec->data[k] = left_vec.data[i];
		i++;
		k++;
	}

	vec->data[k] = key_elem;
	k++;
	i = 0;
	while (i < right_vec.size)
	{
		vec->data[k] = right_vec.data[i];
		k++;
		i++;
	}

	// Free the memory used by the sub-vectors
	vector_free(&left_vec);
	vector_free(&right_vec);

	// Return a pointer to the sorted vector
	return vec;
}

/**
	The coroutine function that reads integers from a file, sorts them using quicksort, and saves them in a Vector.
	@param context A pointer to the filename of the file to read.
	@return 0 on success, 1 on failure.
*/
static int coroutine_func_f(void *context)
{
	// Cast the context to a char pointer and store it in a local variable
	struct param *coro_info = (struct param *)context;
	char *filename = NULL;
	
	if(files_size > 0)
		filename = queue.str_data[--files_size];

	while (filename)
	{
		coro_info->coro_start_time = getTime();
		// Print a message to indicate which file is being read
		printf("Coroutine №%ld reading file: %s\n", coro_info->coro_id, filename);

		// Open the file with the given filename for reading
		FILE *file = fopen(filename, "r");
		if (file == NULL) // Check if file opening failed
		{
			fprintf(stderr, "Failed to open file: %s\n", filename);
			free(filename);
			return 1;
		}

		// Read the integers from the file and store them in a vector
		Vector *vector = (Vector *)malloc(sizeof(Vector));
		vector_init(vector);
		int num;
		while (fscanf(file, "%d", &num) != EOF)
		{
			vector_push_back(vector, num);
		}
		fclose(file);

		// Sort the vector and store in the global vectors array
		quick_sort(vector, coro_info);

		vectors[vectors_size++] = vector;
		printf("\nCoroutine №%ld ", coro_info->coro_id);
		printf("finished file %s\n", filename);
		if (files_size == 0)
			break;
		filename = queue.str_data[--files_size];
	}
	printf("\nCoroutine №%ld Statistics:\n", coro_info->coro_id);
	printf("Total working time :%ld \n", coro_info->coro_total_time);
	printf("Number of switches :%ld \n", coro_info->coro_switch_num);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("Error: arguments are not passed use -h to see description\n");
		return -1;
	}
	uint64_t main_start_time = getTime();
	int coro_num = atoi(argv[argc - 2]);
	int opt;
	uint64_t timeout;
	while ((opt = getopt(argc, argv, "hc:t:")) != -1)
	{
		switch (opt)
		{
		case 'h':
			printf("Usage: ./program [-h] [-c] <num_coroutines>  [-t] <timeout> <file1> <file2> ...\n");
			printf("Options:\n");
			printf("-h  Show this help message and exit\n");
			printf("-c  The number of coroutines to use for testing\n");
			printf("-t  The T value for coroutine timeouts in microseconds\n");
			exit(EXIT_SUCCESS);
		case 'c':
			coro_num = atoi(optarg);
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		}
	}

	int files_num = argc - optind;
	files_size = files_num;
	vectors = (Vector **)malloc(files_num * sizeof(Vector *));
	vector_init(&queue);

	/* Initialize our coroutine global cooperative scheduler. */

	coro_sched_init();

	/* put filenames in queue. */
	for (int i = optind; i < argc; i++)
	{
		char name[16];
		sprintf(name, "%s", argv[i]);
		vector_push_str(&queue, name);
	}

	struct param *param = (struct param *)malloc(sizeof(struct param) * coro_num);
	for (int i = 0; i < coro_num; i++)
	{
		param[i].coro_id = i + 1;
		param[i].coro_max_time = timeout / coro_num;
		param[i].coro_start_time = 0;
		param[i].coro_total_time = 0;
		param[i].coro_switch_num = 0;
		coro_new(coroutine_func_f, &param[i]);
	}

	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL)
	{
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}

	/* All coroutines have finished */

	/* Merge result of sorting*/
	FILE *result = fopen("result.txt", "w");
	if (result == NULL)
	{
		printf("Error: Unable to create file.\n");
		return 1;
	}

	int *ids = (int *)calloc(files_num, sizeof(int));

	while (true)
	{
		int min_value = INT_MAX;
		bool isNumAdded = false;

		for (int i = 0; i < files_num; i++)
		{
			if (ids[i] >= vectors[i]->size)
			{
				continue;
			}
			else if (min_value > vectors[i]->data[ids[i]])
			{
				min_value = vectors[i]->data[ids[i]];
				ids[i]++;
				isNumAdded = true;
			}
		}

		if (!isNumAdded)
			break;

		fprintf(result, "%d ", min_value);
	}

	fclose(result);
	for (int i = 0; i < files_num; i++)
	{
		vector_free(vectors[i]);
		free(vectors[i]);
	}
	free(vectors);
	free(param);
	vector_free(&queue);
	free(ids);

	printf("\nProgram total working time is %ld \n", getTime() - main_start_time);
	return 0;
}