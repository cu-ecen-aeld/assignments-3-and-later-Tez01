#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;
   

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;
 
    // sleep for n ms
    int rc = usleep(thread_func_args->wait_to_obtain_ms * 1000);
    if(rc != 0){
        // didn't sleep well :(
        ERROR_LOG("Didn't sleep well before obtaining mutex :( RC = %d\n", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // obtain mutex
    rc = pthread_mutex_lock(thread_func_args->mutex);
    if(rc != 0){
        ERROR_LOG("Couldn't obtain mutex! RC = %d\n", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // sleep for n ms
    rc = usleep(thread_func_args->wait_to_release_ms * 1000);
    if(rc != 0){
        // didn't sleep well :(
        ERROR_LOG("Didn't sleep well after obtaining mutex :( RC = %d\n", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }
    
    // release mutex
    rc = pthread_mutex_unlock(thread_func_args->mutex);
    if(rc != 0){
        ERROR_LOG("Couldn't release mutex! RC = %d\n", rc);
        thread_func_args->thread_complete_success = false;
        return thread_param;
    }
    
    // Everything worked ok
    thread_func_args->thread_complete_success = true;

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    if(mutex == NULL){
        return false;
    }


    // allocate memory for thread_data
    struct thread_data *data_ptr = malloc(sizeof(struct thread_data));
    if(data_ptr == NULL){
        // cannot allocate data
        ERROR_LOG("Couldn't allocate data.");
        return false;
    }
    

    // setup mutex and wait arguments
    data_ptr->wait_to_obtain_ms = wait_to_obtain_ms;
    data_ptr->wait_to_release_ms = wait_to_release_ms;
    data_ptr->mutex = mutex;

    // create thread
    int rc = pthread_create(thread,
                            NULL,
                            threadfunc,
                            data_ptr);               
    if(rc != 0){
        // cannot create thread
        ERROR_LOG("Couldn't create thread! RC = %d\n", rc);
        return false;
    }

    return true;
}

