export PATH := /usr/bin:/bin

CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -DDEBUG_MODE=1
LDFLAGS = -lyaml -lsndfile -lm
INSTALL_DIR = /usr/local/bin
# for macos
#CFLAGS += -I/opt/homebrew/include
#LDFLAGS += -L/opt/homebrew/lib

SRCS = emgetdata.c debug.h
OBJS = emgetdata.o
TARGET = emgetdata

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install:
	install -m 755 -s $(TARGET) $(INSTALL_DIR)
