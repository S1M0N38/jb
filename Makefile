CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS = -lcurl

SRCDIR  = src
VENDIR  = src/vendor

SRCS    = $(SRCDIR)/jb.c \
          $(SRCDIR)/config.c \
          $(SRCDIR)/commit.c \
          $(SRCDIR)/meta.c \
          $(SRCDIR)/session.c \
          $(SRCDIR)/api.c \
          $(SRCDIR)/tools.c \
          $(SRCDIR)/prompt.c \
          $(VENDIR)/cJSON.c

OBJS    = $(SRCS:.c=.o)

TARGET  = jb

PREFIX ?= /usr/local

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -I$(VENDIR) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	mkdir -p $(PREFIX)/bin
	mkdir -p $(PREFIX)/share/man/man1
	cp $(TARGET) $(PREFIX)/bin/$(TARGET)
	cp jb.1 $(PREFIX)/share/man/man1/jb.1

test: $(TARGET)
	@chmod +x tests/run.sh && sh tests/run.sh

test-%: $(TARGET)
	@chmod +x tests/run.sh && TEST_FILTER="$*" sh tests/run.sh
