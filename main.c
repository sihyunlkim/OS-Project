#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "common.h"

extern ExecutionPlan *parse_line(char *line); 

// function to catch the keyboard interruptor 
void catch_interrupt(int sig){
    // display the new input line when ctrl + c is pressed 
    printf("\n$ "); 
    fflush(stdout); 
}

int main() {
    char line[1024]; 

    // call catch_interrupt when the OS receives SIGINT
    signal(SIGINT, catch_interrupt); 

    while (1){
        // immediately show user input (flush out the user input stored in the buffer)
        printf("$ "); 
        fflush(stdout); 

        // check if there is any data in the user input stream
        if (!fgets(line, sizeof(line), stdin)){
            break; 
        }

        // remove the new line character 
        line[strcspn(line, "\n")] = 0;

        // if user inputs exit command, we exit the shell
        if (strcmp(line, "exit") == 0){
            break; 
        }

        // skip if the input is empty 
        if (strlen(line) == 0){
            continue; 
        }

        // start parsing the line 
        ExecutionPlan *new_plan = parse_line(line); 
        if (new_plan){
            execute_plan(new_plan); 
            free_plan(new_plan); 
            
        }
    }

    return 0; 
}