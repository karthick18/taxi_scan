CC := gcc
LINK_FLAGS :=
DEBUG_FLAGS := -g
CFLAGS := -Wall -g -D_GNU_SOURCE -std=c99
SRCS := rbtree.c taxi_scan.c taxi_pack.c taxi_test.c
OBJS := $(SRCS:%.c=%.o)
TARGET := test_taxi

all: $(TARGET)

test_taxi: $(OBJS)
	$(CC) -o $@ $^ $(LINK_FLAGS) $(DEBUG_FLAGS)

%.o:%.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS) *~