CC      ?= gcc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra
BIN      = bin/srtfix
SRCS     = main.c srt.c encoding.c timing.c fsutil.c
OBJS     = $(SRCS:%.c=obj/%.o)

all: $(BIN)

$(BIN): $(OBJS) | bin
	$(CC) $(CFLAGS) -o $@ $(OBJS)

obj/%.o: %.c | obj
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

bin obj:
	mkdir -p $@

clean:
	rm -f $(BIN) $(OBJS) $(OBJS:.o=.d)
	-rmdir bin obj 2>/dev/null

-include $(OBJS:.o=.d)

.PHONY: all clean
