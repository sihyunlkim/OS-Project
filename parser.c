#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"


// fuction to count the number of tokens by skipping redirection operators and filenames
// so that argv is allocated with the correct size
static int count_num_tokens(char *str){
    int count = 0;
    char *copy = strdup(str);
    char *token = strtok(copy, " \t\n");
    while (token){
        // if token is a redirection operator, skip it and the next token (filename)
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0 || strcmp(token, "2>") == 0){
            token = strtok(NULL, " \t\n"); // skip the filename too
        } else {
            count++;
        }
        token = strtok(NULL, " \t\n");
    }
    free(copy);
    return count;
}

// function to parse the line
ExecutionPlan* parse_line(char *line){

    // check whether user input has useful information or not
    if (!line || strlen(line) < 1){
        return NULL;
    }

    // create a pointer to the memory address
    // where we store the information about ExecutionPlan
    ExecutionPlan *new_plan = malloc(sizeof(ExecutionPlan));
    // initialize the number of commands to 0
    new_plan->num_cmds = 0;

    int len = strlen(line);

    // manually scan for trailing pipe (e.g., "ls |")
    // strtok skips empty tokens so we must check this before splitting
    for (int k = len - 1; k >= 0; k--) {
        if (line[k] == ' ' || line[k] == '\t' || line[k] == '\n') continue;
        if (line[k] == '|') {
            fprintf(stderr, "Command missing after pipe.\n");
            free(new_plan);
            return NULL;
        }
        break;
    }

    // manually scan for empty segment between pipes (e.g., "ls | | grep foo")
    // strtok skips empty tokens so we must check this before splitting
    for (int k = 0; k < len; k++) {
        if (line[k] == '|') {
            int j = k + 1;
            while (j < len && (line[j] == ' ' || line[j] == '\t')) j++;
            if (line[j] == '|') {
                fprintf(stderr, "Empty command between pipes.\n");
                free(new_plan);
                return NULL;
            }
        }
    }

    // up to max 100 command segments at a time
    char *steps[100];
    // searches for | and replaces it with null terminator
    char *step = strtok(line, "|");
    while(step){
        // store the memory address of the first character of the command segment
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

        // get the number of tokens (excluding redirection operators and filenames)
        int numOfTokens = count_num_tokens(steps[i]);

        // creates address space for each command segment to be executed, including NULL
        new_plan->cmds[i].argv = malloc(sizeof(char*) * (numOfTokens + 1));

        int arg_idx = 0;
        // search for separators (space, enter, etc) and replace with null terminator
        // return the memory address of the first character of the command
        char *token = strtok(steps[i], " \n\t");
        while (token){
            if (strcmp(token, "<") == 0){
                // return the file name
                token = strtok(NULL, " \n\t");
                // error if the file name is not there
                if (!token){
                    fprintf(stderr, "Input file is not specified.\n");
                    free(new_plan->cmds);
                    free(new_plan);
                    return NULL;
                }
                // store the file name into the input file
                new_plan->cmds[i].input_file = strdup(token);
            } else if (strcmp(token, ">") == 0){
                // return the file name
                token = strtok(NULL, " \n\t");
                // error if the file name is not there
                if (!token){
                    fprintf(stderr, "Output file not specified.\n");
                    free(new_plan->cmds);
                    free(new_plan);
                    return NULL;
                }
                // store the file name into the output file
                new_plan->cmds[i].output_file = strdup(token);
            } else if (strcmp(token, "2>") == 0){
                // return the file name
                token = strtok(NULL, " \n\t");
                // error if the file name is not there
                if (!token){
                    fprintf(stderr, "Error output file not specified.\n");
                    free(new_plan->cmds);
                    free(new_plan);
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