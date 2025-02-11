CC = gcc
CFLAGS = -Wall -Werror

# The target executable
TARGET = download

# Object files
OBJS = main.o helper.o

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compile main.c
main.o: main.c helper.h
	$(CC) $(CFLAGS) -c $<

# Compile helper.c
helper.o: helper.c helper.h
	$(CC) $(CFLAGS) -c $<

# Clean up
.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)
