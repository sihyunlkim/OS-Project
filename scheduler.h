#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_CMD_LEN 512

typedef struct Task {
    // client number 
    int client_num; 
    // socket fd
    int client_fd; 

    // buffer to store the command 
    char command[MAX_CMD_LEN]; 

    // variable for busrt time 
    int burst_time;
    // how much work is left
    int remaining;
    // how many scheduling rounds this has had so far
    int round; 
    // the order in which tasks arrived in the queue
    int arrival_order; 

    // pointer to next Task in the queue
    struct Task *next; 
} Task; 

// function to set up the queue, mutex, and semaphores 
void scheduler_init(void);

// function to create the scheduler thread that continuously picks and runs tasks 
void scheduler_start(void);

// function to add a new task to the waiting queue and let the scheduler know that the work is available 
void scheduler_enqueue(Task *t);

// function to remove all the tasks that belong to a specific client when they disconnect
void scheduler_remove_client(int client_num);

#endif