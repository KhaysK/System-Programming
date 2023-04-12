#include "thread_pool.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

enum states {
    TASK_WAITING_PUSH,
	TASK_WAITING_THREAD,
	TASK_RUNNING,
	TASK_FINISHED,
    TASK_DETACHED
};

struct thread_task {
	thread_task_f function;
	void *arg;
	enum states task_state;
    pthread_t thread_worker;
    void *result;
    pthread_cond_t *task_cond;
    pthread_mutex_t *task_mutex;
	/* PUT HERE OTHER MEMBERS */
};

struct thread_pool {
	pthread_t *threads;
	int max_thread_count;
	struct thread_task *task_queue;
	int task_count;
	bool is_shutdown;
	pthread_mutex_t *mutex;
	pthread_cond_t *task_cond;
	/* PUT HERE OTHER MEMBERS */
};

typedef struct {
    struct thread_pool *pool;
    int thread_id;
} thread_args_t;

void *worker_thread(void *arg) {
    thread_args_t *args = (thread_args_t *) arg; 
    struct thread_pool *pool = args->pool;
    
    for (;;) {
        pthread_mutex_lock(pool->mutex);
        
        while (pool->task_count == 0 && !pool->is_shutdown) {
            pthread_cond_wait(pool->task_cond, pool->mutex);
        }

        if (pool->is_shutdown) {
            pthread_mutex_unlock(pool->mutex);
            pthread_exit(NULL);
        }
        
        struct thread_task *task = &pool->task_queue[--pool->task_count];
        
        if (task->task_state == TASK_DETACHED) {
            pthread_mutex_unlock(pool->mutex);
            pthread_exit(NULL);
        }

        pthread_mutex_lock(task->task_mutex);

        task->thread_worker = pool->threads[args->thread_id];

        pthread_mutex_unlock(pool->mutex);
        
        task->task_state = TASK_RUNNING;

        thread_task_f function = task->function;
	    void *arg = task->arg;
        
        pthread_mutex_unlock(task->task_mutex);

        void *result = function(arg);
        
        pthread_mutex_lock(task->task_mutex);
        
        task->result = result;
        task->task_state = TASK_FINISHED;

        pthread_mutex_unlock(task->task_mutex);
        pthread_cond_signal(task->task_cond);
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
        thread_args_t *args = malloc(sizeof(thread_args_t));
        args->thread_id = i;
        args->pool = new_pool;
        pthread_create(&(new_pool->threads[i]), NULL, worker_thread, (void*)args);
    }
    
    // Initialize the mutex
    new_pool->mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    if (pthread_mutex_init(new_pool->mutex, NULL) != 0) {
        free(new_pool->task_queue);
        free(new_pool->threads);
        free(new_pool);
        return -1;
    }
    new_pool->task_cond = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
	pthread_cond_init(new_pool->task_cond, NULL);

    // Return the pool object
    *pool = new_pool;
    return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
    int active_threads = 0;

    // Lock the mutex to prevent other threads from modifying the pool
    pthread_mutex_lock(pool->mutex);

    // Count the number of active threads
    for (int i = 0; i < pool->max_thread_count; i++) {
        pthread_t thread = pool->threads[i];

        if (pthread_equal(thread, pthread_self())) {
            active_threads++;
        }
    }

    // Unlock the mutex to allow other threads to modify the pool
    pthread_mutex_unlock(pool->mutex);

    return active_threads;
}

int thread_pool_delete(struct thread_pool *pool) {
    // Check if there are any pending tasks
    if (pool->task_count > 0) {
        return TPOOL_ERR_HAS_TASKS;
    }
    // Mark the pool as shutdown
    pool->is_shutdown = true;
    // Wake up all waiting threads
    pthread_cond_broadcast(pool->task_cond);
    // Join all threads
    for (int i = 0; i < pool->max_thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    // Free the task queue
    free(pool->task_queue);

    // Free the threads array
    free(pool->threads);

    // Destroy the mutex and condition variables
    pthread_mutex_destroy(pool->mutex);
    pthread_cond_destroy(pool->task_cond);

    // Free the pool structure itself
    free(pool);
    return 0;
}

int
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	pthread_mutex_lock(pool->mutex);
    if (pool->task_count >= TPOOL_MAX_TASKS) {
        pthread_mutex_unlock(pool->mutex);
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    pool->task_queue[pool->task_count++] = *task;
    task->task_state = TASK_WAITING_THREAD;

    pthread_cond_signal(pool->task_cond);
    pthread_mutex_unlock(pool->mutex);

    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	*task = malloc(sizeof(struct thread_task));

    (*task)->function = function;
    (*task)->arg = arg;
    (*task)->task_state = TASK_WAITING_PUSH;

    (*task)->task_mutex = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init((*task)->task_mutex, NULL);

    (*task)->task_cond = (pthread_cond_t*) malloc(sizeof(pthread_cond_t));
	pthread_cond_init((*task)->task_cond, NULL);
    return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	if(task->task_state == TASK_FINISHED) 
		return true;
	return false;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	if(task->task_state == TASK_RUNNING) 
		return true;
	return false;
}

int
thread_task_join(struct thread_task *task, void **result)
{
    if (task == NULL) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

    pthread_mutex_lock(task->task_mutex); 

    if (task->task_state == TASK_WAITING_PUSH) {
        pthread_mutex_unlock(task->task_mutex); 
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }
    
    // Wait for the task to finish
    while (task->task_state != TASK_FINISHED)
    {
        pthread_cond_wait(task->task_cond, task->task_mutex);
    }
    
    printf("state %d", task->task_state);
    printf("value %d", *(int *) task->result);

    *result = task->result;

    pthread_mutex_unlock(task->task_mutex); 
    return 0;
}

int
thread_task_delete(struct thread_task *task)
{
    if (task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    if (task->task_state > TASK_WAITING_PUSH) {
        return TPOOL_ERR_TASK_IN_POOL;
    }

    free(task);
    return 0;
}

#ifdef NEED_DETACH

int thread_task_detach(struct thread_task *task) {
    if (!task) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&task->mutex);

    // If the task is already finished, delete it and return success.
    if (task->status == THREAD_TASK_FINISHED) {
        pthread_mutex_unlock(&task->mutex);
        thread_task_delete(task);
        return TPOOL_SUCCESS;
    }

    // If the task is not pushed to a pool, return an error.
    if (!task->pool) {
        pthread_mutex_unlock(&task->mutex);
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    // Mark the task as detached and unlock its mutex.
    task->status = TASK_DETACHED;
    pthread_mutex_unlock(&task->mutex);

    // Return success.
    return TPOOL_SUCCESS;
}

#endif
