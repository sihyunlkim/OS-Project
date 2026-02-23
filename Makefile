# Compiler to use
CC = gcc

# Compile options
CFLAGS = -Wall -g

# Name of the ultimate executable file 
TARGET = myshell

#  All source files needed for the compilation
SRCS = main.c executor.c parser.c

# build instruction
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

# when you want to erase all the files and restart 
clean:
	rm -f $(TARGET)