CC      = gcc
CFLAGS  = -Wall -Wextra -pedantic -std=c11 -g
TARGET  = myshell
SRCS    = shell.c parser.c
OBJS    = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c parser.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
