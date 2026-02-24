#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

// function to count the number of tokens 
static int count_num_tokens(char *str){
    int count = 0; 
    // make a copy of str so that we can keep the original form of it even after editing it
    char *copy = strdup(str); 
    // replace the space with the null terminator
    char *token = strtok(copy, " "); 
    // loop through the command line to count the number of commands 
    while (token){
        count++; 
        token = strtok(NULL, " "); 
    }
    // free the allocated memory
    free(copy); 

    return count; 
}

// function to parse the line 
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

    for (int i = 0; i < new_plan->num_cmds; i++){
        // data cleansing by initializing them to null
        new_plan->cmds[i].input_file = NULL; 
        new_plan->cmds[i].output_file = NULL; 
        new_plan->cmds[i].error_file = NULL; 

        // get the number of tokens
        int numOfTokens = count_num_tokens(steps[i]); 

        // creates address space for each command segment to be executed, including NULL
        new_plan->cmds[i].argv = malloc(sizeof(char*) * (numOfTokens + 1)); 

        int arg_idx = 0; 
        // search for seperators (space, enter, etc) and replace it with null terminator 
        // return the memory address of the first character of the command 
        char *token = strtok(steps[i], " \n\t"); 
        while (token){
            if (strcmp(token, "<") == 0){
                // return the file name
                token = strtok(NULL, " \n\t"); 
                // error if the file name is not there 
                if (!token){
                    fprintf(stderr, "Input file is missing\n"); 
                    return NULL; 
                }
                // store the file name into the input file 
                new_plan->cmds[i].input_file = strdup(token); 
            } else if (strcmp(token, ">") == 0){
                // return the file name
                token = strtok(NULL, " \n\t"); 
                // error if the file name is not there 
                if (!token){
                    fprintf(stderr, "Input file is missing\n"); 
                    return NULL; 
                }
                // store the file name into the output file 
                new_plan->cmds[i].output_file = strdup(token);
            } else if (strcmp(token, "2>") == 0){
                // return the file name
                token = strtok(NULL, " \n\t"); 
                // error if the file name is not there 
                if (!token){
                    fprintf(stderr, "Input file is missing\n"); 
                    return NULL; 
                }
                // store the name of the file where user wants to store errors
                new_plan->cmds[i].error_file = strdup(token); 
            } else{
                // store other commands into argv
                new_plan->cmds[i].argv[arg_idx] = strdup(token); 
                arg_idx++; 
            }
            // return the first character of the new word
            token = strtok(NULL, " \n\t"); 
        }
        // signal the end of the command 
        new_plan->cmds[i].argv[arg_idx] = NULL; 
    }

    return new_plan; 
    
}