CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -g -O0
CPPFLAGS := -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Isrc -Ivendor
LDLIBS   := $(shell pkg-config --libs libcurl)
CFLAGS   += $(shell pkg-config --cflags libcurl)

PREFIX   ?= /usr/local
BINDIR   := $(PREFIX)/bin

SRC := $(wildcard src/*.c) vendor/cJSON.c
OBJ := $(SRC:.c=.o)
BIN := goget

.PHONY: all clean install debug

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

debug: CFLAGS += -fsanitize=address,undefined -DDEBUG
debug: LDLIBS += -fsanitize=address,undefined
debug: clean all

install: $(BIN)
	install -Dm755 $(BIN) $(BINDIR)/$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
