#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

#include "student.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/* Global variables and synchronization primitives */
static pcb_t **current;     // Array of pointers to the currently running processes
static queue_t *rq;         // Pointer to the ready queue

static pthread_mutex_t current_mutex;
static pthread_mutex_t queue_mutex;
static pthread_cond_t queue_not_empty;

/*
 * state_mutex is used to protect updates to process->state.
 */
static pthread_mutex_t state_mutex;

static sched_algorithm_t scheduler_algorithm;
static unsigned int cpu_count;
static unsigned int age_weight;
static int timeslice;       // For Round-Robin; -1 means infinite (as in FCFS)

/**
 * priority_with_age() calculates the effective priority of a process.
 * Formula: effective = priority - (current_time - enqueue_time) * age_weight
 */
extern double priority_with_age(unsigned int current_time, pcb_t *process) {
    return process->priority - age_weight * (current_time - process->enqueue_time);
}

/**
 * enqueue() adds a process to the ready queue.
 */
void enqueue(queue_t *queue, pcb_t *process)
{
    pthread_mutex_lock(&queue_mutex);
    process->enqueue_time = get_current_time();
    if (is_empty(queue)) {
        queue->head = process;
        queue->tail = process;
    } else {
        queue->tail->next = process;
        queue->tail = process;
    }
    process->next = NULL;  // Ensure the new tail's next is NULL
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_mutex);
}

/**
 * dequeue() removes and returns a process from the ready queue,
 * selecting one based on the current scheduling algorithm.
 */
pcb_t *dequeue(queue_t *queue)
{
    pthread_mutex_lock(&queue_mutex);
    if (is_empty(queue)) {
        pthread_mutex_unlock(&queue_mutex);
        return NULL;
    }
    pcb_t *selected = NULL;
    pcb_t *prev_selected = NULL;
    if (scheduler_algorithm == FCFS) {
        pcb_t *curr = queue->head;
        pcb_t *prev = NULL;
        selected = curr;
        prev_selected = NULL;
        while (curr) {
            if (curr->arrival_time < selected->arrival_time) {
                selected = curr;
                prev_selected = prev;
            }
            prev = curr;
            curr = curr->next;
        }
    } else if (scheduler_algorithm == RR) {
        selected = queue->head;
        prev_selected = NULL;
    } else if (scheduler_algorithm == PA) {
        unsigned int curr_time = get_current_time();
        pcb_t *curr = queue->head;
        pcb_t *prev = NULL;
        selected = curr;
        prev_selected = NULL;
        while (curr) {
            if (priority_with_age(curr_time, curr) < priority_with_age(curr_time, selected)) {
                selected = curr;
                prev_selected = prev;
            }
            prev = curr;
            curr = curr->next;
        }
    } else { // SRTF
        pcb_t *curr = queue->head;
        pcb_t *prev = NULL;
        selected = curr;
        prev_selected = NULL;
        while (curr) {
            if (curr->total_time_remaining < selected->total_time_remaining) {
                selected = curr;
                prev_selected = prev;
            }
            prev = curr;
            curr = curr->next;
        }
    }
    if (prev_selected == NULL) {
        queue->head = queue->head->next;
    } else {
        prev_selected->next = selected->next;
    }
    if (selected == queue->tail) {
        queue->tail = prev_selected;
    }
    selected->next = NULL;
    pthread_mutex_unlock(&queue_mutex);
    return selected;
}

/**
 * is_empty() checks whether the ready queue is empty.
 */
bool is_empty(queue_t *queue)
{
    return (queue->head == NULL);
}

/**
 * schedule() selects a process from the ready queue and assigns it to a CPU.
 * Updates to process->state are protected by state_mutex.
 */
static void schedule(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* proc = dequeue(rq);
    pthread_mutex_unlock(&current_mutex);
    if (proc) {
        pthread_mutex_lock(&state_mutex);
        proc->state = PROCESS_RUNNING;
        pthread_mutex_unlock(&state_mutex);
    }
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = proc;
    pthread_mutex_unlock(&current_mutex);
    context_switch(cpu_id, proc, timeslice);
}

/**
 * idle() blocks 
 * (avoids wasting cpu cycles)
 * (avoids busy waiting) 
 * until there is at least one process in the ready queue, then calls schedule().
 */
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&queue_mutex);
    while (is_empty(rq)) {
        pthread_cond_wait(&queue_not_empty, &queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);
    schedule(cpu_id);
}

/**
 * When using a preemptive scheduling algorithm, a CPU-bound process may be preempted before it completes its CPU operations.
 * It marks the process as ready, re-enqueues it, and schedules a new process.
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* proc = current[cpu_id];
    pthread_mutex_lock(&state_mutex);
    proc->state = PROCESS_READY;
    pthread_mutex_unlock(&state_mutex);
    enqueue(rq, proc);
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/**
 * A process completes its CPU operations and yields the processor to perform an I/O request.
 * It marks the process as waiting and schedules a new process.
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* proc = current[cpu_id];
    pthread_mutex_lock(&state_mutex);
    proc->state = PROCESS_WAITING;
    pthread_mutex_unlock(&state_mutex);
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/**
 * terminate() is called when a process completes execution.
 * It marks the process as terminated and schedules a new process.
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t* proc = current[cpu_id];
    pthread_mutex_lock(&state_mutex);
    proc->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&state_mutex);
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/**
 * wake_up() is called when a process's I/O completes.
 * For PA scheduling, if the waking process's effective priority is better (lower)
 * than that of the worst running process, it preempts that CPU.
 */
extern void wake_up(pcb_t *process)
{
    pthread_mutex_lock(&state_mutex);
    process->state = PROCESS_READY;
    pthread_mutex_unlock(&state_mutex);
    enqueue(rq, process);

    pthread_mutex_lock(&current_mutex);
    if (scheduler_algorithm == PA) {
        double worst_process = DBL_MIN;
        unsigned int worst_cpu = 0;
        unsigned int curr_time = get_current_time();
        for (unsigned int i = 0; i < cpu_count; i++) {
            if (current[i] == NULL) {
                pthread_mutex_unlock(&current_mutex);
                return;
            }
            double curr_priority = priority_with_age(curr_time, current[i]);
            if (curr_priority > worst_process) {
                worst_cpu = i;
                worst_process = curr_priority;
            }
        }
        pthread_mutex_unlock(&current_mutex);
        if (priority_with_age(curr_time, process) < worst_process) {
            preempt(worst_cpu);
        }
    } else if (scheduler_algorithm == SRTF) {
        double worst_process = DBL_MIN;
        unsigned int worst_cpu = 0;
        for (unsigned int i = 0; i < cpu_count; i++) {
            if (current[i] == NULL) {
                pthread_mutex_unlock(&current_mutex);
                return;
            }
            double remaining = current[i]->total_time_remaining;
            if (remaining > worst_process) {
                worst_cpu = i;
                worst_process = remaining;
            }
        }
        pthread_mutex_unlock(&current_mutex);
        if (process->total_time_remaining < worst_process) {
            preempt(worst_cpu);
        }
    } else {
        pthread_mutex_unlock(&current_mutex);
    }
}

/**
 * main() parses command-line arguments and starts the simulator.
 */
int main(int argc, char *argv[])
{
    printf("[DEBUG][FCFS]\n");
    scheduler_algorithm = FCFS;
    age_weight = 0;
    timeslice = -1; // default timeslice = -1 for infinite

    if (argc != 2)
    {
        if (strcmp(argv[2], "-r") == 0) {
            scheduler_algorithm = RR;
            timeslice = atoi(argv[3]);
        } else if (strcmp(argv[2], "-s") == 0) {
            scheduler_algorithm = SRTF;
        } else if (strcmp(argv[2], "-p") == 0) {
            scheduler_algorithm = PA;
            age_weight = (unsigned int) atoi(argv[3]);
        } else {
            fprintf(stderr, "CS 2200 Project 4 -- Multithreaded OS Simulator\n"
                    "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p <age weight> | -s ]\n"
                    "    Default : FCFS Scheduler\n"
                    "         -r : Round-Robin Scheduler\n"
                    "         -p : Priority Aging Scheduler\n"
                    "         -s : Shortest Remaining Time First\n");
            return -1;
        }
    }

    /* Parse the CPU count from the first argument */
    cpu_count = strtoul(argv[1], NULL, 0);

    /* Allocate and initialize the current process array and synchronization primitives */
    current = malloc(sizeof(pcb_t *) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_mutex_init(&state_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);

    /* Allocate and initialize the ready queue */
    rq = malloc(sizeof(queue_t));
    assert(rq != NULL);
    rq->head = NULL;
    rq->tail = NULL;

    /* Start the simulator (provided by os-sim) */
    start_simulator(cpu_count);

    return 0;
}

#pragma GCC diagnostic pop
