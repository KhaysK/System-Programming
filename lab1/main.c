#include <stdio.h>
#include <stdbool.h>
#include "vector.h"
#include <limits.h>

Vector *quick_sort(Vector *vec)
{
    if (vec->size < 2)
        return vec;

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

    // Sort left and right vectors recursively
    quick_sort(&left_vec);
    quick_sort(&right_vec);

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

    vector_free(&left_vec);
    vector_free(&right_vec);

    return vec;
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Error: arguments are not passed\n");
        return 1;
    }
    int files_num = argc - 1;
    FILE **fp = (FILE **)malloc(sizeof(FILE *) * files_num);
    int num;
    Vector *vectors = (Vector *)malloc(sizeof(Vector) * files_num);

    for (int i = 0; i < files_num; i++)
    {
        fp[i] = fopen(argv[i + 1], "r");
        if (fp[i] == NULL)
        {
            printf("Error opening file.\n");
            return 1;
        }
        vector_init(&vectors[i]);
        while (fscanf(fp[i], "%d", &num) != EOF)
        {
            vector_push_back(&vectors[i], num);
        }
        fclose(fp[i]);

        quick_sort(&vectors[i]);
    }
    free(fp);

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
            if (ids[i] >= vectors[i].size)
            {
                continue;
            }
            else if (min_value > vectors[i].data[ids[i]])
            {
                min_value = vectors[i].data[ids[i]];
                ids[i]++;
                isNumAdded = true;
            }
        }

        if (!isNumAdded)
            break;

        fprintf(result,"%d ", min_value);
    }

    fclose(result);
    for (int i = 0; i < files_num; i++)
    {

        vector_free(&vectors[i]);
    }
    free(vectors);
    free(ids);
    return 0;
}