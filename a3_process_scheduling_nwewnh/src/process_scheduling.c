#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dyn_array.h"
#include "processing_scheduling.h"

// You might find this handy.  I put it around unused parameters, but you should
// remove it before you submit. Just allows things to compile initially.
#define UNUSED(x) (void)(x)

#define SIZE_T_MAX ((size_t)(~0UL))

// private function
void virtual_cpu(ProcessControlBlock_t *process_control_block)
{
    // decrement the burst time of the pcb
    --process_control_block->remaining_burst_time;
}

bool any_burst_remaining(dyn_array_t *arr)
{
    ProcessControlBlock_t
        *pcb = dyn_array_front(arr),
        *end = dyn_array_back(arr);
    for (; pcb<=end; pcb++)
        if (pcb->remaining_burst_time > 0)
            return true;
    return false;
}

ProcessControlBlock_t *find_shortest_burst(dyn_array_t *arr, unsigned t)
{
    ProcessControlBlock_t *shortest = NULL,
        *pcb = dyn_array_front(arr),
        *end = dyn_array_back(arr);
    for (uint32_t burst; pcb<=end; pcb++) {
        if (t < pcb->arrival)
            continue;
        burst = pcb->remaining_burst_time;
        if (burst > 0)
            if (shortest == NULL || burst < shortest->remaining_burst_time)
                shortest = pcb;
    }
    return shortest;
}

bool first_come_first_serve(dyn_array_t *ready_queue, ScheduleResult_t *result)
{
    if (ready_queue == NULL)
        return false;
    if (result == NULL)
        return false;

    size_t size = dyn_array_size(ready_queue);
    if (size == 0)
        return false;

    result->average_waiting_time = 0.0;
    result->average_turnaround_time = 0.0;
    result->total_run_time = 0;

    unsigned t = 0;
    ProcessControlBlock_t pcb;
    while (dyn_array_size(ready_queue)) {
        dyn_array_extract_back(ready_queue, &pcb);
        pcb.started = true;
        result->average_waiting_time += t;
        for (; pcb.remaining_burst_time; t++)
            virtual_cpu(&pcb);
        result->average_turnaround_time += t;
    }

    result->average_waiting_time /= size;
    result->average_turnaround_time /= size;
    result->total_run_time = t;

    return true;
}

bool shortest_job_first(dyn_array_t *ready_queue, ScheduleResult_t *result)
{
    if (ready_queue == NULL)
        return false;
    if (result == NULL)
        return false;

    size_t size = dyn_array_size(ready_queue);
    if (size == 0)
        return false;

    result->average_waiting_time = 0.0;
    result->average_turnaround_time = 0.0;
    result->total_run_time = 0;

    // long instead of unsigned to prevent underflow with time calculations
    long t = 0, t_wait = 0;
    ProcessControlBlock_t *pcb;

    while (any_burst_remaining(ready_queue)) {
        pcb = find_shortest_burst(ready_queue, t);
        pcb->started = true;
        t_wait = t - pcb->arrival;
        result->average_waiting_time += t_wait;
        result->average_turnaround_time += t_wait + pcb->remaining_burst_time;
        for (; pcb->remaining_burst_time; t++)
            virtual_cpu(pcb);
    }

    result->average_waiting_time /= size;
    result->average_turnaround_time /= size;
    result->total_run_time = t;

    return true;
}

bool priority(dyn_array_t *ready_queue, ScheduleResult_t *result)
{
    UNUSED(ready_queue);
    UNUSED(result);
    return false;
}

/**
 * Time calculations:
 *   average_wait = exit_time - burst_time
 *   average_turnaround = exit_time - arrival_time
 */
bool round_robin(dyn_array_t *not_ready_queue, ScheduleResult_t *result, size_t quantum)
{
    if (not_ready_queue == NULL)
        return false;
    if (result == NULL)
        return false;
    if (quantum == 0)
        return false;

    size_t size = dyn_array_size(not_ready_queue);
    if (size == 0)
        return false;

    dyn_array_t *ready_queue = dyn_array_create(
        size, sizeof(ProcessControlBlock_t), NULL);
    if (ready_queue == NULL)
        return false;

    result->average_waiting_time = 0.0;
    result->average_turnaround_time = 0.0;
    result->total_run_time = 0;

    unsigned t = 0, q = 0;
    int i, turnaround;
    ProcessControlBlock_t *pcb, tmp;

    while (dyn_array_size(not_ready_queue) || dyn_array_size(ready_queue)) {
        // Move ready pcb's to the ready queue
        for (i=dyn_array_size(not_ready_queue)-1; i>=0; i--) {
            pcb = dyn_array_at(not_ready_queue, i);
            if (t >= pcb->arrival) {
                dyn_array_extract(not_ready_queue, i, &tmp);
                dyn_array_push_back(ready_queue, &tmp);
            }
        }

        pcb = dyn_array_front(ready_queue);

        if (q == 0 && pcb->started == false) {
            pcb->started = true;
            // Subtract off total burst times
            result->average_waiting_time -= pcb->remaining_burst_time;
        }

        // Execute the front of the ready queue for 1 tick
        virtual_cpu(pcb);
        q++; t++;

        if (pcb->remaining_burst_time == 0) {
            // Done executing (exit_time = t)
            // Remove from ready queue
            q = 0;
            turnaround = t - pcb->arrival;
            result->average_waiting_time += turnaround;
            result->average_turnaround_time += turnaround;
            dyn_array_pop_front(ready_queue);
        } else if (q >= quantum) {
            // Not done executing but time slice expired
            // Move to end of ready queue
            q = 0;
            dyn_array_extract_front(ready_queue, &tmp);
            dyn_array_push_back(ready_queue, &tmp);
        }
    }

    result->average_waiting_time /= size;
    result->average_turnaround_time /= size;
    result->total_run_time = t;

    dyn_array_destroy(ready_queue);
    return true;
}

dyn_array_t *load_process_control_blocks(const char *input_file)
{
    if (input_file == NULL)
        goto err1;

    int fd = open(input_file, O_RDONLY);
    if (fd == -1)
        goto err1;

    uint32_t n_pcbs;
    if (read(fd, &n_pcbs, sizeof(uint32_t)) <= 0)
        goto err2;

    dyn_array_t *arr = dyn_array_create(
        n_pcbs, sizeof(ProcessControlBlock_t), NULL);
    if (arr == NULL)
        goto err2;

    ProcessControlBlock_t pcb = { .started=false };
    ssize_t should_read = 3 * sizeof(uint32_t);
    while (n_pcbs-- > 0) {
        if (read(fd, &pcb, should_read) < should_read)
            goto err3;
        if (dyn_array_push_back(arr, &pcb) == false)
            goto err3;
    }

    // Check for excess records in the file (read returns 0 on EOF)
    uint8_t c;
    if (read(fd, &c, sizeof(uint8_t)) != 0)
        goto err3;

    return arr;

err3:
    dyn_array_destroy(arr);
err2:
    close(fd);
err1:
    return NULL;
}

/**
 * Time calculations:
 *   average_wait = exit_time - burst_time
 *   average_turnaround = exit_time - arrival_time
 */
bool shortest_remaining_time_first(dyn_array_t *ready_queue, ScheduleResult_t *result)
{
    if (ready_queue == NULL)
        return false;
    if (result == NULL)
        return false;

    size_t size = dyn_array_size(ready_queue);
    if (size == 0)
        return false;

    result->average_waiting_time = 0.0;
    result->average_turnaround_time = 0.0;
    result->total_run_time = 0;

    long t = 0;
    ProcessControlBlock_t *pcb;
    int turnaround;

    while (any_burst_remaining(ready_queue)) {
        pcb = find_shortest_burst(ready_queue, t);
        if (pcb->started == false) {
            pcb->started = true;
            // Subtract off total burst times
            result->average_waiting_time -= pcb->remaining_burst_time;
        }

        virtual_cpu(pcb);
        t++;

        if (pcb->remaining_burst_time == 0) {
            // Done executing, exit_time = t
            turnaround = t - pcb->arrival;
            result->average_waiting_time += turnaround;
            result->average_turnaround_time += turnaround;
        }
    }

    result->average_waiting_time /= size;
    result->average_turnaround_time /= size;
    result->total_run_time = t;

    return true;
}
