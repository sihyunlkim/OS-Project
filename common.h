#ifndef COMMON_H
#define COMMON_H

/**
 * Information for an individual command.
 * This structure stores the command, its arguments, and any redirection targets.
 */
typedef struct {
    char **argv;        // Array of strings (e.g., {"ls", "-l", NULL})
    char *input_file;   // Filename for input redirection (<)
    char *output_file;  // Filename for output redirection (>)
    char *append_file;  // Filename for append redirection (>>)
    char *error_file;   // Filename for error redirection (2>)
} Command;

/**
 * The complete execution plan for a single user input line.
 * This includes all commands separated by pipes (|).
 */
typedef struct {
    Command *cmds;      // Array of Command structures in the pipeline
    int num_cmds;       // Total number of commands (to determine pipe count)
} ExecutionPlan;

ExecutionPlan* parse_line(char *line);
void free_plan(ExecutionPlan *plan);
void execute_plan(ExecutionPlan *plan);

#endif