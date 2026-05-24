CC      = cc
CFLAGS  = -std=c99 -Wall -Wextra -pedantic -O2
LDFLAGS = -lcurl

SRCDIR  = src
VENDIR  = src/vendor

SRCS    = $(SRCDIR)/jb.c \
          $(SRCDIR)/config.c \
          $(SRCDIR)/session.c \
          $(SRCDIR)/api.c \
          $(SRCDIR)/tools.c \
          $(SRCDIR)/prompt.c \
          $(VENDIR)/cJSON.c

OBJS    = $(SRCS:.c=.o)

TARGET  = jb

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -I$(VENDIR) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

test: $(TARGET)
	@chmod +x tests/run.sh && sh tests/run.sh
