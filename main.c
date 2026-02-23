#include <stdio.h>
#include <stdlib.h>
#include "common.h"

int main() {
    printf("--- Starting Input Redirection Test: wc -w < test_input.txt ---\n");

    // 1. Setup the command: "wc" "-w"
    char *wc_args[] = {"wc", "-w", NULL};
    Command cmd1;
    cmd1.argv = wc_args;
    
    // 2. Set the input file (This is the < part)
    cmd1.input_file = "test_input.txt"; 
    cmd1.output_file = NULL;
    cmd1.error_file = NULL;

    // 3. Assemble the Execution Plan (Just 1 command this time)
    ExecutionPlan plan;
    plan.cmds = &cmd1;
    plan.num_cmds = 1;

    // 4. Run YOUR Executor
    execute_plan(&plan);

    printf("--- Test Complete ---\n");
    return 0;
}