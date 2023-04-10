#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

enum states {
	TASK_WAITING,
	TASK_RUNNING,
	TASK_FINISHED
};

struct thread_task {
	thread_task_f function;
	void *arg;
	enum states task_state;

	/* PUT HERE OTHER MEMBERS */
};

struct thread_pool {
	pthread_t *threads;
	int max_thread_count;
	struct thread_task *task_queue;
	int task_count;
	bool is_shutdown;
	pthread_mutex_t *mutex;
	pthread_cond_t task_cond;
	/* PUT HERE OTHER MEMBERS */
};

void *worker_thread(void *arg) {
    struct thread_pool *pool = (struct thread_pool *) arg;

    for (;;) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->task_count == 0 && !pool->is_shutdown) {
            pthread_cond_wait(&pool->task_cond, &pool->mutex);
        }

        if (pool->is_shutdown) {
            pthread_mutex_unlock(&pool->mutex);
            pthread_exit(NULL);
        }

        struct thread_task *task = &pool->task_queue[--pool->task_count];

        pthread_mutex_unlock(&pool->mutex);

        task->task_state = TASK_RUNNING;
        task->function(task->arg);

        // if (task->result) {
        //     *(task->result) = task->function(task->arg);
        // }

        task->task_state = TASK_FINISHED;

        // if (task->completion_signal) {
        //     pthread_cond_signal(&task->completion_cond);
        // }
    }
}

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if(max_thread_count > TPOOL_MAX_THREADS || max_thread_count <= 0) 
		return TPOOL_ERR_INVALID_ARGUMENT;

	// Allocate memory for the pool object
    struct thread_pool *new_pool = malloc(sizeof(struct thread_pool));
    if (new_pool == NULL) {
        return -1;
    }

    // Initialize the pool object
    new_pool->max_thread_count = max_thread_count;
    new_pool->is_shutdown = false;
    new_pool->task_queue = malloc(sizeof(struct thread_task) * TPOOL_MAX_TASKS);
    if (new_pool->task_queue == NULL) {
        free(new_pool);
        return -1;
    }
    // for (int i = 0; i < max_thread_count; i++) {
    //     new_pool->task_queue[i].task_state = false;
    // }
    new_pool->task_count = 0;
    new_pool->threads = malloc(sizeof(pthread_t) * max_thread_count);
    if (new_pool->threads == NULL) {
        free(new_pool->task_queue);
        free(new_pool);
        return -1;
    }
    for (int i = 0; i < max_thread_count; i++) {
        pthread_create(&(new_pool->threads[i]), NULL, worker_thread, (void*)new_pool);
    }

    // Initialize the mutex
    if (pthread_mutex_init(&(new_pool->mutex), NULL) != 0) {
        free(new_pool->task_queue);
        free(new_pool->threads);
        free(new_pool);
        return -1;
    }

	pthread_cond_init(&new_pool->task_cond, NULL);

    // Return the pool object
    *pool = new_pool;
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
    int active_threads = 0;

    // Lock the mutex to prevent other threads from modifying the pool
    pthread_mutex_lock(&(pool->mutex));

    // Count the number of active threads
    for (int i = 0; i < pool->max_thread_count; i++) {
        pthread_t thread = pool->threads[i];

        if (pthread_equal(thread, pthread_self())) {
            active_threads++;
        }
    }

    // Unlock the mutex to allow other threads to modify the pool
    pthread_mutex_unlock(&(pool->mutex));

    return active_threads;
}

int thread_pool_delete(struct thread_pool *pool) {
    // Check if there are any pending tasks
    if (pool->task_count > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }
    // Mark the pool as shutdown
    pool->is_shutdown = true;
	printf("Wake up\n");
    // Wake up all waiting threads
    pthread_cond_broadcast(&(pool->task_cond));
	printf("Join threads\n");
    // Join all threads
    for (int i = 0; i < pool->max_thread_count; i++) {
		printf("Join thread %d\n",i);
        pthread_join(pool->threads[i], NULL);
    }
    // Free the task queue
    free(pool->task_queue);

    // Free the threads array
    free(pool->threads);

    // Destroy the mutex and condition variables
    pthread_mutex_destroy(&(pool->mutex));
    pthread_cond_destroy(&(pool->task_cond));

    // Free the pool structure itself
    free(pool);
	printf("Delete end\n");
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

int
thread_task_delete(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
