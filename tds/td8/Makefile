# TD8 · agentic OS with a micro-LLM.  Normally invoked from tds/ (make -C tds).
#   make            builds tinyllm, agentd, agent and agent_exec into ../bin/td8 (and links the weights)
#   make train      retrains the model (NumPy, ≈ 80 min on a CPU) → weights/tinyllm.bin
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu11 -Wall -Wextra -Werror -D_GNU_SOURCE -pthread -I../common
LDLIBS  += -L../bin -lutils -lm
B := ../bin/td8

all: $(B)/tinyllm $(B)/agentd $(B)/agent $(B)/agent_exec $(B)/weights

$(B)/tinyllm: tinyllm_cli.c tinyllm.c tinyllm.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) tinyllm_cli.c tinyllm.c -o $@ $(LDLIBS)

$(B)/agentd: agentd.c tinyllm.c tinyllm.h
	@mkdir -p $(B)
	$(CC) $(CFLAGS) agentd.c tinyllm.c -o $@ $(LDLIBS)

$(B)/agent: agent.c
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

$(B)/agent_exec: agent_exec.c
	@mkdir -p $(B)
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

# agentd looks for weights/ next to its executable
$(B)/weights:
	@mkdir -p $(B)
	ln -sfn $(CURDIR)/weights $@

train:
	cd train && python3 train.py

clean:
	rm -rf $(B)

.PHONY: all train clean
