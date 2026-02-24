#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

ExecutionPlan* parse_line(char *line){

    // check whether user input has useful information or not
    if (!line || strlen(line) <= 1){
        return NULL; 
    }

    // create a pointer to the memory address 
    // where we store the information about ExecutionPlan
    ExecutionPlan *new_plan = malloc(sizeof(ExecutionPlan));
    // initialize the number of commands to 0 
    new_plan->num_cmds = 0; 

    // up to max 10 command segments at a time
    char *steps[10]; 
    // searches for | and replace it with null terminator 
    char *step = strtok(line, "|"); 
    while(step){
        // store the memory address of the first character of the command segment 
        // and store it into the corresponding slot in steps 
        steps[new_plan->num_cmds] = step;
        new_plan->num_cmds++;
        // keep looking for the rest of |
        step = strtok(NULL, "|"); 
    }

    // allocate memory space to store all the commands in the user input
    new_plan->cmds = malloc(sizeof(Command) * new_plan->num_cmds); 





    
    
}