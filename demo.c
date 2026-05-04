#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


int main(int argc, char *argv[]){
    // validate if the user has passed exactly one argument 
    if (argc != 2){
        fprintf(stderr, "Usage: %s <N>\n", argv[0]);
        fprintf(stderr, "  N  number of one-second iterations to run\n");
        return 1;
    }

    // converts the argument into integer 
    int n = atoi(argv[1]);
    // check if the number is positive or not
    if (n <= 0) {
        fprintf(stderr, "Error: N must be a positive integer.\n");
        return 1;
    }

    // use for loop to print one progress line per iteration 
    for (int i = 0; i < n; i++) {
        printf("Demo %d/%d\n", i, n);
        fflush(stdout);
        sleep(1);
    }

    return 0; 
}