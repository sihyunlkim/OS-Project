#ifndef COMMON_H
#define COMMON_H

/**
 * Information for an individual command.
 * This structure stores the command, its arguments, and any redirection targets.
 */
typedef struct {
    char **argv;        // Array of strings (e.g., {"ls", "-l", NULL}) [cite: 31, 34]
    char *input_file;   // Filename for input redirection (<) [cite: 48]
    char *output_file;  // Filename for output redirection (>) [cite: 49]
    char *error_file;   // Filename for error redirection (2>) [cite: 50]
} Command;

/**
 * The complete execution plan for a single user input line.
 * This includes all commands separated by pipes (|).
 */
typedef struct {
    Command *cmds;      // Array of Command structures in the pipeline 
    int num_cmds;       // Total number of commands (to determine pipe count) 
} ExecutionPlan;

void execute_plan(ExecutionPlan *plan);

#endif