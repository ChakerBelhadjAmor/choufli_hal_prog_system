CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra
TARGET  = client

all: $(TARGET)

$(TARGET): client.c
	$(CC) $(CFLAGS) client.c -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
