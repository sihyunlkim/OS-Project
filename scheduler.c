#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "scheduler.h"
#include "common.h"

// quantum (in seconds) for the first scheduling round
#define QUANTUM_ROUND0  3
// quantum (in seconds) for all subsequent scheduling rounds
#define QUANTUM_REST    7

// head of the singly-linked waiting queue
static Task            *queue_head  = NULL;
// monotonically increasing counter used to stamp each task's arrival order
static int              arrival_seq = 0;
// mutex to protect the queue and all shared scheduler state
static pthread_mutex_t  queue_mutex = PTHREAD_MUTEX_INITIALIZER;

// semaphore whose value equals the number of tasks currently in the queue
static sem_t queue_sem;

// set to 1 by scheduler_enqueue() when an arriving task is shorter than the
// currently running one, causing the scheduler loop to abort the quantum early
static volatile int preempt_flag = 0;

// pointer to the task that is currently being executed by the scheduler thread
// written only by the scheduler thread; read by scheduler_enqueue() to decide
// whether to raise the preempt flag
static Task *current_task = NULL;

// dynamically growing string that records the execution order for the summary
// line printed at the end, e.g. "P5-(3)->P6-(6)->P7-(20)->..."
static char  *timeline     = NULL;
static size_t timeline_len = 0;
static size_t timeline_cap = 0;

extern char         *execute_and_capture(ExecutionPlan *plan);
extern ExecutionPlan *parse_line(char *line);

// append one scheduling slot entry to the timeline string
static void timeline_append(int client_num, int remaining) {
    char buf[64];
    int  n;

    // first entry has no leading arrow
    if (timeline_len == 0) {
        n = snprintf(buf, sizeof(buf), "P%d-(%d)", client_num, remaining);
    } else {
        n = snprintf(buf, sizeof(buf), "->P%d-(%d)", client_num, remaining);
    }

    // grow the buffer if the new entry does not fit
    while (timeline_len + (size_t)n + 1 > timeline_cap) {
        timeline_cap = timeline_cap ? timeline_cap * 2 : 256;
        timeline = realloc(timeline, timeline_cap);
    }

    // copy the entry into the buffer and update the length
    memcpy(timeline + timeline_len, buf, n);
    timeline_len += n;
    timeline[timeline_len] = '\0';
}

// append a task to the tail of the waiting queue
// must be called with queue_mutex held
static void enqueue_locked(Task *t) {
    t->next = NULL;
    // if the queue is empty, the new task becomes the head
    if (!queue_head) {
        queue_head = t;
        return;
    }
    // walk to the last node and link the new task there
    Task *cur = queue_head;
    while (cur->next) cur = cur->next;
    cur->next = t;
}

// select and remove the best task from the queue using SRJF with FCFS tie-breaking
// if last_task is non-NULL and other candidates exist, skip it to prevent the
// same task from running two consecutive rounds
// must be called with queue_mutex held
static Task *dequeue_best(Task *last_task) {
    if (!queue_head) return NULL;

    Task *best_prev = NULL;
    Task *best      = NULL;
    Task *prev      = NULL;
    Task *cur       = queue_head;

    while (cur) {
        // skip the previously run task when there are other options available
        if (cur == last_task && queue_head->next != NULL) {
            prev = cur;
            cur  = cur->next;
            continue;
        }

        if (!best) {
            best_prev = prev;
            best      = cur;
        } else if (cur->remaining < best->remaining) {
            // prefer the task with the shorter remaining time (SRJF)
            best_prev = prev;
            best      = cur;
        } else if (cur->remaining == best->remaining &&
                   cur->arrival_order < best->arrival_order) {
            // break ties by choosing the task that arrived first (FCFS)
            best_prev = prev;
            best      = cur;
        }

        prev = cur;
        cur  = cur->next;
    }

    if (!best) return NULL;

    // unlink the chosen task from the queue
    if (best_prev) {
        best_prev->next = best->next;
    } else {
        queue_head = best->next;
    }
    best->next = NULL;
    return best;
}

// send output followed by the EOF sentinel to the client so the client's
// recv loop knows the full response has arrived
static int send_to_client(int client_fd, const char *output) {
    int length = (int)strlen(output);
    // handle zero-length output (e.g. a task that produces no stdout)
    if (length == 0) {
        send(client_fd, "\n\x04", 2, 0);
        return 0;
    }
    send(client_fd, output, length, 0);
    send(client_fd, "\x04", 1, 0);
    return length;
}

// execute up to quantum seconds of work for the given task
// forks a child that runs the demo binary with the remaining count as its argument
// reads one output line per second, forwarding each to the client
// checks preempt_flag after every iteration and aborts early if it is set
// kills the child when the quantum expires or preemption is triggered
// returns the number of seconds actually consumed
static int run_task_for_quantum(Task *task, int quantum) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        send_to_client(task->client_fd, "Internal error: pipe failed.\n");
        return 0;
    }

    // build the command for this slice using only the executable name and
    // the remaining time so the child runs exactly that many iterations
    char exe[MAX_CMD_LEN];
    strncpy(exe, task->command, sizeof(exe) - 1);
    exe[sizeof(exe) - 1] = '\0';
    // strip any existing argument to isolate the executable path
    char *sp = strchr(exe, ' ');
    if (sp) *sp = '\0';

    char cmd[MAX_CMD_LEN];
    snprintf(cmd, sizeof(cmd), "%s %d", exe, task->remaining);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        send_to_client(task->client_fd, "Internal error: fork failed.\n");
        return 0;
    }

    if (pid == 0) {
        // child: redirect stdout and stderr into the write end of the pipe
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        // parse and execute the slice command
        ExecutionPlan *plan = parse_line(cmd);
        if (plan) {
            execute_plan(plan);
            free_plan(plan);
        }
        exit(0);
    }

    // parent: wrap the read end in a FILE* for line-by-line reading
    close(pipefd[1]);
    FILE *fp = fdopen(pipefd[0], "r");
    if (!fp) {
        perror("fdopen");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return 0;
    }

    char line[512];
    int  consumed = 0;

    while (consumed < quantum) {
        // check for a preemption request before reading the next output line
        pthread_mutex_lock(&queue_mutex);
        int should_preempt = preempt_flag;
        pthread_mutex_unlock(&queue_mutex);

        // abort the quantum early if a higher-priority task has arrived
        if (should_preempt) break;

        // read one output line from the child (one line == one second of work)
        if (!fgets(line, sizeof(line), fp)) break;

        // forward the line to the client and update the remaining counter
        send(task->client_fd, line, strlen(line), 0);
        consumed++;
        task->remaining--;

        printf("[%d]--- running (%d)\n", task->client_num, task->remaining);
        fflush(stdout);
    }

    // clean up: close the pipe and terminate the child if still running
    fclose(fp);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);

    return consumed;
}

// main loop of the scheduler thread
// waits for tasks on the semaphore, picks the best one, runs it for one quantum,
// and either frees it (if done) or re-enqueues it (if more work remains)
static void *scheduler_thread(void *arg) {
    (void)arg;

    // track the task that ran most recently to prevent consecutive execution
    Task *last_run = NULL;

    while (1) {
        // block until at least one task is available in the queue
        sem_wait(&queue_sem);

        // select the best task and clear any pending preemption request
        pthread_mutex_lock(&queue_mutex);
        preempt_flag = 0;
        Task *task = dequeue_best(last_run);
        if (!task) {
            // spurious wake or all tasks were removed; go back to waiting
            pthread_mutex_unlock(&queue_mutex);
            continue;
        }
        current_task = task;
        pthread_mutex_unlock(&queue_mutex);

        // use the smaller quantum on the first round so short tasks finish quickly
        int quantum = (task->round == 0) ? QUANTUM_ROUND0 : QUANTUM_REST;

        // log started only on the first round; subsequent rounds skip straight to running
        if (task->round == 0) {
            printf("[%d]--- started (%d)\n", task->client_num, task->burst_time);
        }
        fflush(stdout);

        // record this slot in the execution timeline
        pthread_mutex_lock(&queue_mutex);
        timeline_append(task->client_num, task->remaining);
        pthread_mutex_unlock(&queue_mutex);

        // run the task for up to one quantum
        run_task_for_quantum(task, quantum);

        // update shared state after the quantum finishes
        pthread_mutex_lock(&queue_mutex);
        current_task = NULL;

        if (task->remaining <= 0) {
            // task has finished: send the EOF sentinel and log completion
            int sent = send_to_client(task->client_fd, "");
            printf("[%d]<<< %d bytes sent\n", task->client_num, sent);
            printf("[%d]--- ended (%d)\n",    task->client_num, task->burst_time);
            fflush(stdout);

            // print the full timeline summary when the queue becomes empty
            if (!queue_head) {
                printf("\n%s\n\n", timeline ? timeline : "");
                fflush(stdout);
            }

            last_run = NULL;
            free(task);
        } else {
            // task was preempted or used its full quantum but still has work left:
            // increment its round counter and return it to the waiting queue
            task->round++;
            preempt_flag = 0;

            printf("[%d]--- waiting (%d)\n", task->client_num, task->remaining);
            fflush(stdout);

            enqueue_locked(task);
            // post the semaphore so the scheduler wakes up for this task
            sem_post(&queue_sem);

            last_run = task;
        }
        pthread_mutex_unlock(&queue_mutex);
    }

    return NULL;
}

// initialise the semaphore and reset all shared scheduler state
// must be called once before scheduler_start() or scheduler_enqueue()
void scheduler_init(void) {
    sem_init(&queue_sem, 0, 0);
    queue_head   = NULL;
    arrival_seq  = 0;
    preempt_flag = 0;
    current_task = NULL;
    timeline     = NULL;
    timeline_len = 0;
    timeline_cap = 0;
}

// spawn the background scheduler thread in detached mode
void scheduler_start(void) {
    pthread_t      tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    if (pthread_create(&tid, &attr, scheduler_thread, NULL) != 0) {
        perror("pthread_create (scheduler)");
        exit(1);
    }
    pthread_attr_destroy(&attr);
}

// add a new task to the waiting queue and wake the scheduler thread
// if the new task has a shorter remaining time than the currently running task,
// set preempt_flag so the scheduler aborts the current quantum early
void scheduler_enqueue(Task *t) {
    pthread_mutex_lock(&queue_mutex);

    // stamp the task with an arrival order for FCFS tie-breaking
    t->arrival_order = arrival_seq++;
    t->next          = NULL;

    // trigger preemption if the new task is shorter than what is running now
    if (current_task != NULL && t->remaining < current_task->remaining) {
        preempt_flag = 1;
    }

    enqueue_locked(t);
    // log that this task is now waiting in the queue
    printf("[%d]--- waiting (%d)\n", t->client_num, t->remaining);
    fflush(stdout);
    pthread_mutex_unlock(&queue_mutex);

    // signal the semaphore to wake the scheduler thread
    sem_post(&queue_sem);
}

// remove all queued tasks belonging to the given client number
// called when a client sends "exit" or drops the connection
void scheduler_remove_client(int client_num) {
    pthread_mutex_lock(&queue_mutex);

    Task *prev    = NULL;
    Task *cur     = queue_head;
    int   removed = 0;

    while (cur) {
        if (cur->client_num == client_num) {
            // unlink this task and free its memory
            Task *to_free = cur;
            if (prev) {
                prev->next = cur->next;
            } else {
                queue_head = cur->next;
            }
            cur = cur->next;
            free(to_free);
            removed++;
        } else {
            prev = cur;
            cur  = cur->next;
        }
    }

    // if this client's task is currently running, preempt it immediately
    if (current_task && current_task->client_num == client_num) {
        preempt_flag = 1;
    }

    pthread_mutex_unlock(&queue_mutex);

    // drain the semaphore by the number of removed tasks to keep it consistent
    for (int i = 0; i < removed; i++) {
        sem_trywait(&queue_sem);
    }

    printf("[INFO] Removed %d queued task(s) for client %d.\n", removed, client_num);
    fflush(stdout);
}