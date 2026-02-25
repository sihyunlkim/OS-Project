#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "common.h"

void execute_plan (ExecutionPlan *plan){
    int i; 
    int pipes= plan -> num_cmds-1; //calculating number of pipes
    int pipefds[pipes > 0 ? 2 * pipes : 1];  //array holding pipe file descriptiors

    //creating pipes for n commands:
    for (i=0; i< pipes; i++){
        if (pipe(pipefds + i * 2) < 0) {
                    perror("pipe");
                    exit(1);
                };
        }

    for (i=0; i< plan-> num_cmds; i++){
        pid_t pid = fork(); // creating a child process for each command
        
        if (pid==0){// inside child process
        // PIPES 
        if (i>0){// if not first command, take input from previous pipe 
            dup2(pipefds[(i - 1) * 2], STDIN_FILENO);
            
        } 
        if (i < plan->num_cmds - 1) { // if not the last, send output to next pipe
                dup2(pipefds[i * 2 + 1], STDOUT_FILENO);

            }
        
        // close all pipe copies in child
            for (int j = 0; j < 2 * pipes; j++) close(pipefds[j]);

        // file redirections
        //handle input redirection (<)- replace stdin with a file.
            if (plan->cmds[i].input_file) { 
                int fd = open(plan->cmds[i].input_file, O_RDONLY);
                if (fd < 0) { perror("Input file error"); exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            // handle output redirection (>)- replace stdout with a file.
            // O_TRUNC is used to overwrite the file if it already exists.
            if (plan->cmds[i].output_file) { 
                int fd = open(plan->cmds[i].output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("Output file error"); exit(1); }
                dup2(fd, STDOUT_FILENO); 
                close(fd); 
            }

            // handle Error Redirection (2>)- replace stderr with a file.

            if (plan->cmds[i].error_file) {
                int fd = open(plan->cmds[i].error_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) { perror("Error file error"); exit(1); }
                dup2(fd, STDERR_FILENO); 
                close(fd);
            }


            // execution
            if (plan->cmds[i].argv[0] == NULL) {
            fprintf(stderr, "No command specified.\n");
            exit(1);
                }
            execvp(plan->cmds[i].argv[0], plan->cmds[i].argv); //execvp searches the PATH environment variable for the executable.
            fprintf(stderr, "%s: Command not found.\n", plan->cmds[i].argv[0]); //only prints if execvp fails 
            exit(1);

        
    }


    }

    // parent closes all pipe ends and waits for children to finish before returning to the main shell loop 
    for (i = 0; i < 2 * pipes; i++) close(pipefds[i]);
    for (i = 0; i < plan->num_cmds; i++) wait(NULL); 
}