#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

// helper function to tokenize a command string while respecting quoted strings.
// unlike strtok, this function treats content inside single or double quotes as
// a single token, preserving spaces within quotes. it also strips the surrounding
// quote characters from the token before returning it.
// returns the next token, or NULL if no more tokens are found.
static char *next_token(char **cursor) {
    char *start = *cursor;

    // skip leading whitespace
    while (*start == ' ' || *start == '\t') start++;

    // if we've reached the end of the string, no more tokens
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    // handle quoted strings (single or double quotes)
    if (*start == '"' || *start == '\'') {
        char quote = *start;
        char *end = strchr(start + 1, quote);
        if (end) {
            // strip the surrounding quotes by duplicating only the inner content
            int len = end - start - 1;
            char *token = malloc(len + 1);
            strncpy(token, start + 1, len);
            token[len] = '\0';
            *cursor = end + 1;
            return token;
        }
    }

    // handle unquoted tokens — read until whitespace or end of string
    char *end = start;
    while (*end && *end != ' ' && *end != '\t') end++;
    int len = end - start;
    char *token = malloc(len + 1);
    strncpy(token, start, len);
    token[len] = '\0';
    *cursor = end;
    return token;
}

// function to count the number of argument tokens in a command segment,
// skipping redirection operators (< > >> 2>) and their associated filenames.
// this is used to correctly allocate memory for the argv array.
static int count_num_tokens(char *str) {
    int count = 0;
    char *cursor = str;
    char *token;
    while ((token = next_token(&cursor)) != NULL) {
        // if token is a redirection operator, skip it and the next token (filename)
        if (strcmp(token, "<") == 0 || strcmp(token, ">") == 0 ||
            strcmp(token, ">>") == 0 || strcmp(token, "2>") == 0) {
            free(token);
            // skip the filename token as well
            char *filename = next_token(&cursor);
            if (filename) free(filename);
        } else {
            count++;
            free(token);
        }
    }
    return count;
}

// function to parse the line
ExecutionPlan* parse_line(char *line) {

    // check whether user input has useful information or not
    if (!line || strlen(line) < 1) {
        return NULL;
    }

    // create a pointer to the memory address
    // where we store the information about ExecutionPlan
    ExecutionPlan *new_plan = malloc(sizeof(ExecutionPlan));
    // initialize the number of commands to 0
    new_plan->num_cmds = 0;

    int len = strlen(line);

    // manually scan for a leading pipe (e.g., "| pwd")
    // strtok skips empty tokens so we must check this before splitting
    for (int k = 0; k < len; k++) {
        if (line[k] == ' ' || line[k] == '\t' || line[k] == '\n') continue;
        if (line[k] == '|') {
            fprintf(stderr, "Command missing before pipe.\n");
            free(new_plan);
            return NULL;
        }
        break;
    }

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
            // bounds check before accessing line[j] to avoid out-of-bounds read
            if (j < len && line[j] == '|') {
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
    while (step) {
        // store the memory address of the first character of the command segment
        steps[new_plan->num_cmds] = step;
        new_plan->num_cmds++;
        // keep looking for the rest of |
        step = strtok(NULL, "|");
    }

    // allocate memory space to store all the commands in the user input
    new_plan->cmds = malloc(sizeof(Command) * new_plan->num_cmds);

    for (int i = 0; i < new_plan->num_cmds; i++) {
        // data cleansing by initializing them to null
        new_plan->cmds[i].argv = NULL;
        new_plan->cmds[i].input_file = NULL;
        new_plan->cmds[i].output_file = NULL;
        new_plan->cmds[i].append_file = NULL;
        new_plan->cmds[i].error_file = NULL;

        // get the number of tokens (excluding redirection operators and filenames)
        int numOfTokens = count_num_tokens(steps[i]);

        // creates address space for each command segment to be executed, including NULL
        new_plan->cmds[i].argv = malloc(sizeof(char*) * (numOfTokens + 1));

        int arg_idx = 0;
        char *cursor = steps[i];
        char *token;

        // use next_token instead of strtok so that quoted strings with spaces
        // are treated as a single token and quotes are stripped automatically
        while ((token = next_token(&cursor)) != NULL) {
            if (strcmp(token, "<") == 0) {
                free(token);
                token = next_token(&cursor);
                if (!token) {
                    fprintf(stderr, "Input file is not specified.\n");
                    new_plan->num_cmds = i + 1;
                    free_plan(new_plan);
                    return NULL;
                }
                // store the file name into the input file
                new_plan->cmds[i].input_file = token;
            } else if (strcmp(token, ">>") == 0) {
                free(token);
                // >> must be checked before > to avoid partial match
                token = next_token(&cursor);
                if (!token) {
                    fprintf(stderr, "Append output file not specified.\n");
                    new_plan->num_cmds = i + 1;
                    free_plan(new_plan);
                    return NULL;
                }
                // store the file name into the append file
                new_plan->cmds[i].append_file = token;
            } else if (strcmp(token, ">") == 0) {
                free(token);
                token = next_token(&cursor);
                if (!token) {
                    fprintf(stderr, "Output file not specified.\n");
                    new_plan->num_cmds = i + 1;
                    free_plan(new_plan);
                    return NULL;
                }
                // store the file name into the output file
                new_plan->cmds[i].output_file = token;
            } else if (strcmp(token, "2>") == 0) {
                free(token);
                token = next_token(&cursor);
                if (!token) {
                    fprintf(stderr, "Error output file not specified.\n");
                    new_plan->num_cmds = i + 1;
                    free_plan(new_plan);
                    return NULL;
                }
                // store the name of the file where user wants to store errors
                new_plan->cmds[i].error_file = token;
            } else {
                // store the argument into argv (quotes already stripped by next_token)
                new_plan->cmds[i].argv[arg_idx] = token;
                arg_idx++;
            }
        }
        // signal the end of the command
        new_plan->cmds[i].argv[arg_idx] = NULL;
    }

    return new_plan;
}