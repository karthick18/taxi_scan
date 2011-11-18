CC := gcc
LINK_FLAGS :=
DEBUG_FLAGS := -g
CFLAGS := -Wall -g -D_GNU_SOURCE -std=c99
SERVER_SRCS := rbtree.c taxi_scan.c taxi_server.c taxi_pack.c taxi_utils.c
CLIENT_SRCS := taxi_client.c taxi_utils.c taxi_pack.c 
TEST_SRCS := taxi_test.c
TEST_OBJS := $(TEST_SRCS:%.c=%.o)
SERVER_OBJS := $(SERVER_SRCS:%.c=%.o)
CLIENT_OBJS := $(CLIENT_SRCS:%.c=%.o)
CLIENT_LIBS := libtaxi_client.a
TEST_LIBS := -ltaxi_client
TARGETS := taxi_server $(CLIENT_LIBS) taxi_test
OBJS := $(SERVER_OBJS) $(CLIENT_OBJS) $(TEST_OBJS)

all: $(TARGETS)

taxi_server: $(SERVER_OBJS)
	$(CC) -o $@ $^ $(LINK_FLAGS) $(DEBUG_FLAGS)

libtaxi_client.a: $(CLIENT_OBJS)
	@(\
		ar cr $@ $^;\
		ranlib $@;\
	)

taxi_test: $(TEST_OBJS)
	$(CC) -o $@ $^ $(LINK_FLAGS) $(DEBUG_FLAGS) -L./. $(TEST_LIBS)

%.o:%.c
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) $(OBJS) *~