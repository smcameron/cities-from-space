# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lpng -lm

# Target executable name
TARGET = cities-from-space

# Object files
OBJS = cities-from-space.o bline.o png_utils.o

# Default rule
all: $(TARGET)

# Link the object files into the final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDFLAGS)

# Compile the main program
cities-from-space.o: cities-from-space.c bline.h png_utils.h
	$(CC) $(CFLAGS) -c cities-from-space.c

# Compile the line drawing utility
bline.o: bline.c bline.h
	$(CC) $(CFLAGS) -c bline.c

# Compile the PNG utility
png_utils.o: png_utils.c png_utils.h
	$(CC) $(CFLAGS) -c png_utils.c

# Clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)
