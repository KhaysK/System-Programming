#ifndef VECTOR_H
#define VECTOR_H

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int *data;
    char **str_data;
    size_t size;
    size_t capacity;
    int _isString;
    int _isInt;
} Vector;

void vector_init(Vector *vec)
{
    vec->data = NULL;
    vec->str_data = NULL;
    vec->size = 0;
    vec->capacity = 0;
    vec->_isString = 0;
    vec->_isInt = 0;
}

void vector_push_back(Vector *vec, int value)
{
    if (!vec->_isString)
    {
        if (vec->size == vec->capacity)
        {
            size_t new_capacity = vec->capacity == 0 ? 1 : vec->capacity * 2;
            int *new_data = (int *)realloc(vec->data, new_capacity * sizeof(int));
            if (new_data == NULL)
            {
                printf("Error: Out of memory\n");
                exit(EXIT_FAILURE);
            }
            vec->data = new_data;
            vec->capacity = new_capacity;
        }
        vec->data[vec->size++] = value;
        vec->_isInt = 1;
    }
}

void vector_push_str(Vector *vec, char *value)
{
    if (!vec->_isInt)
    {
        if (vec->size == vec->capacity)
        {
            size_t new_capacity = vec->capacity == 0 ? 1 : vec->capacity * 2;
            char **new_data = (char **)realloc(vec->str_data, new_capacity * sizeof(char *));
            if (new_data == NULL)
            {
                printf("Error: Out of memory\n");
                exit(EXIT_FAILURE);
            }
            vec->str_data = new_data;
            vec->capacity = new_capacity;
        }
        vec->str_data[vec->size++] = strdup(value);
        vec->_isString = 1;
    }
}

void vector_free(Vector *vec)
{
    free(vec->data);

    if (vec->_isString)
    {
        for (int i = 0; i < vec->size; i++)
        {
            free(vec->str_data[i]);
        }
    }
    free(vec->str_data);
    vec->data = NULL;
    vec->size = 0;
    vec->capacity = 0;
}

#endif /* VECTOR_H */
