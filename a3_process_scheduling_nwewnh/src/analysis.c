#include <stdio.h>
#include <stdlib.h>

#include "dyn_array.h"
#include "processing_scheduling.h"

#define FCFS "FCFS"
#define P "P"
#define RR "RR"
#define SJF "SJF"
#define SRTF "SRTF"

#define USAGE "%s <pcb file> <schedule algorithm> [quantum]\n", argv[0]

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf(USAGE);
        return EXIT_FAILURE;
    }

    dyn_array_t *pcbs = load_process_control_blocks(argv[1]);
    if (pcbs == NULL) {
        printf("Error: failed to load PCB file: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int code = 0;
    ScheduleResult_t results;
    if (strncmp(argv[2], FCFS, 4) == 0) {
        if (first_come_first_serve(pcbs, &results) == false)
            code = 2;
    } else if (strncmp(argv[2], SJF, 3) == 0) {
        if (shortest_job_first(pcbs, &results) == false)
            code = 2;
    } else if (strncmp(argv[2], SRTF, 3) == 0) {
        if (shortest_remaining_time_first(pcbs, &results) == false)
            code = 2;
    } else if (strncmp(argv[2], RR, 2) == 0) {
        if (argc < 4) {
            printf("Error: the RR algorithm requires a quantum\n");
            printf(USAGE);
            code = 1;
        } else {
            int quantum = atoi(argv[3]);
            if (round_robin(pcbs, &results, quantum) == false)
                code = 2;
        }
    } else {
        printf("Error: scheduling algorithm not recognized: %s\n", argv[2]);
        printf("Supported values are: %s, %s, %s, %s\n", FCFS, SJF, SRTF, RR);
        printf(USAGE);
        code = 1;
    }

    dyn_array_destroy(pcbs);

    if (code == 0) {
        printf("     Total runtime = %lu\n", results.total_run_time);
        printf("      Average wait = %.3f\n", results.average_waiting_time);
        printf("Average turnaround = %.3f\n", results.average_turnaround_time);
    } else if (code == 2) {
        printf("Error: failed to run scheduling algorithm: %s\n", argv[2]);
    }
    return code;
}
