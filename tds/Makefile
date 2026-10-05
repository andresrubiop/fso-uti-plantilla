# Fundamentals of Operating Systems labs (UTI) - worked solutions.
#
#   make            builds libutils and every TD into bin/tdN/
#   make td3        builds a single TD
#   make test       builds and runs tests/run.sh
#   make clean
#
# Everything builds with -Wall -Wextra -Werror: a warning is an error.

# The course compiles with gcc. make's built-in default is CC=cc, which "CC ?= gcc" would not change.
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -pthread -Icommon
LDFLAGS += -pthread
LDLIBS  += -Lbin -lutils -lm

TDS  := td0 td1 td2 td3 td4 td5 td6 td7
SRCS := $(foreach t,$(TDS),$(wildcard $(t)/*.c))
BINS := $(patsubst %.c,bin/%,$(SRCS))

# Variants the lab asks to build with different macros (-D...).
VARIANTS := \
  bin/td3/buffer_1 bin/td3/buffer_2 bin/td3/buffer_3 bin/td3/buffer_4 bin/td3/buffer_5 \
  bin/td3/fork_pid_0 bin/td3/fork_pid_1 bin/td3/fork_pid_2 bin/td3/fork_pid_3 \
  bin/td4/mypipe_bug \
  bin/td5/map_none bin/td5/map_read bin/td5/map_write bin/td5/map_exec bin/td5/map_ew \
  bin/td6/philosophers_deadlock bin/td5/shm_race bin/td6/peterson_fence

.PHONY: all test clean $(TDS) td8

all: bin/libutils.a $(BINS) $(VARIANTS) td8

$(TDS): bin/libutils.a
	@$(MAKE) --no-print-directory $(filter bin/$@/%,$(BINS) $(VARIANTS))

bin/libutils.a: common/utils.c common/utils.h
	@mkdir -p bin
	$(CC) $(CFLAGS) -c common/utils.c -o bin/utils.o
	$(AR) rcs $@ bin/utils.o

bin/%: %.c bin/libutils.a common/utils.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@ $(LDLIBS)

# --- TD3 · buffers: STRING1 = "Hello ", STRING2 = "World!\n" -------------------------------
BUF = td3/buffer.c bin/libutils.a
bin/td3/buffer_1: $(BUF) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DUSE_FPRINTF1 $< -o $@ $(LDLIBS)
bin/td3/buffer_2: $(BUF) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DUSE_FPRINTF2 -DUSE_STDERR2 $< -o $@ $(LDLIBS)
bin/td3/buffer_3: $(BUF) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DUSE_FPRINTF1 -DUSE_FPRINTF2 -DUSE_STDERR2 $< -o $@ $(LDLIBS)
bin/td3/buffer_4: $(BUF) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DUSE_FPRINTF1 -DUSE_FPRINTF2 -DUSE_STDERR2 '-DSTRING1="Hello\n"' $< -o $@ $(LDLIBS)
bin/td3/buffer_5: $(BUF) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DUSE_FPRINTF1 -DUSE__EXIT $< -o $@ $(LDLIBS)

FORK = td3/fork_pid.c bin/libutils.a
bin/td3/fork_pid_%: $(FORK) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DSOLUTION=$* $< -o $@ $(LDLIBS)

# --- TD4 · the version with the bug of not closing the read end ------------------------------
bin/td4/mypipe_bug: td4/mypipe.c bin/libutils.a ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DKEEP_READ_END $< -o $@ $(LDLIBS)

# --- TD5 · anonymous mmap protections -------------------------------------------------------
MAP = td5/map_anonymous.c bin/libutils.a
bin/td5/map_none:  $(MAP) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DPROT=PROT_NONE  $< -o $@ $(LDLIBS)
bin/td5/map_read:  $(MAP) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DPROT=PROT_READ  $< -o $@ $(LDLIBS)
bin/td5/map_write: $(MAP) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DPROT=PROT_WRITE $< -o $@ $(LDLIBS)
bin/td5/map_exec:  $(MAP) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DPROT=PROT_EXEC  $< -o $@ $(LDLIBS)
bin/td5/map_ew:    $(MAP) ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) '-DPROT=PROT_EXEC|PROT_WRITE' $< -o $@ $(LDLIBS)

# --- TD6 · deadlocking philosophers and shm without atomics ----------------------------------
bin/td6/philosophers_deadlock: td6/philosophers.c bin/libutils.a ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) $(LDFLAGS) -DDEMO_DEADLOCK $< -o $@ $(LDLIBS)
bin/td6/peterson_fence: td6/peterson.c bin/libutils.a ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DFENCE $< -o $@ $(LDLIBS)
bin/td5/shm_race: td5/shm_fork.c bin/libutils.a ; @mkdir -p $(dir $@) && $(CC) $(CFLAGS) -DNO_ATOMIC $< -o $@ $(LDLIBS)

# --- TD8 · micro-LLM + agentic daemon (own Makefile) ---------------------------------------
td8: bin/libutils.a
	@if [ -f td8/Makefile ]; then $(MAKE) --no-print-directory -C td8; fi

test: all
	./tests/run.sh

clean:
	rm -rf bin tests/tmp
	@if [ -f td8/Makefile ]; then $(MAKE) --no-print-directory -C td8 clean; fi
