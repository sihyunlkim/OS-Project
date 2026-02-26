# Compiler to use
CC = gcc

# Compile options
CFLAGS = -Wall -g

# Name of the ultimate executable file
TARGET = myshell

# Object files (one per source file — this is separate compilation)
OBJS = main.o executor.o parser.o

# Link all object files into the final executable
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile each .c file into its own .o file independently
main.o: main.c common.h
	$(CC) $(CFLAGS) -c main.c

executor.o: executor.c common.h
	$(CC) $(CFLAGS) -c executor.c

parser.o: parser.c common.h
	$(CC) $(CFLAGS) -c parser.c

# Remove all generated files for a clean rebuild
clean:
	rm -f $(TARGET) $(OBJS)