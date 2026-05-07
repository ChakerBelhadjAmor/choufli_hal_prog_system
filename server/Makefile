CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -pthread
TARGET  = server

all: $(TARGET)

$(TARGET): server.c
	$(CC) $(CFLAGS) server.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
