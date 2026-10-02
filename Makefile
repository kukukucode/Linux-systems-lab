CC ?= cc
CPPFLAGS ?= -D_POSIX_C_SOURCE=200809L
CFLAGS ?= -std=c17 -Wall -Wextra -Wpedantic -Werror -g3 -O0

BUILD_DIR := build

# Intentionally broken programs are excluded from normal CI.
EXCLUDED_SOURCES := \
	src/memory/ownership_uaf.c \
	src/memory/ownership_double_free.c

SOURCES := $(filter-out $(EXCLUDED_SOURCES),$(wildcard src/*.c))
TARGETS := $(patsubst src/%.c,$(BUILD_DIR)/%,$(SOURCES))

PROCESS_SOURCES := $(wildcard src/process/*.c)
PROCESS_TARGETS := $(patsubst src/process/%.c,$(BUILD_DIR)/%,$(PROCESS_SOURCES))
TARGETS += $(PROCESS_TARGETS)

MEMORY_SOURCES := $(filter-out $(EXCLUDED_SOURCES),$(wildcard src/memory/*.c))
MEMORY_TARGETS := $(patsubst src/memory/%.c,$(BUILD_DIR)/%,$(MEMORY_SOURCES))
TARGETS += $(MEMORY_TARGETS)

IO_SOURCES := $(wildcard src/io/*.c)
IO_TARGETS := $(patsubst src/io/%.c,$(BUILD_DIR)/%,$(IO_SOURCES))
TARGETS += $(IO_TARGETS)

IPC_SOURCES := $(wildcard src/ipc/*.c)
IPC_TARGETS := $(patsubst src/ipc/%.c,$(BUILD_DIR)/%,$(IPC_SOURCES))
TARGETS += $(IPC_TARGETS)

THREAD_SOURCES := $(wildcard src/threads/*.c)
THREAD_TARGETS := $(patsubst src/threads/%.c,$(BUILD_DIR)/%,$(THREAD_SOURCES))
TARGETS += $(THREAD_TARGETS)

NETWORK_SOURCES := $(wildcard src/network/*.c)
NETWORK_TARGETS := $(patsubst src/network/%.c,$(BUILD_DIR)/%,$(NETWORK_SOURCES))
TARGETS += $(NETWORK_TARGETS)

MINI_SHELL_SOURCES := \
	src/mini_shell/main.c \
	src/mini_shell/jobs.c \
	src/mini_shell/exec.c \
	src/mini_shell/parser.c
MINI_SHELL_HEADERS := src/mini_shell/mini_shell.h
TARGETS += $(BUILD_DIR)/mini_shell

.PHONY: all clean test sanitizer-test

all: $(TARGETS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(PROCESS_TARGETS): $(BUILD_DIR)/%: src/process/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(MEMORY_TARGETS): $(BUILD_DIR)/%: src/memory/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(IO_TARGETS): $(BUILD_DIR)/%: src/io/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(IPC_TARGETS): $(BUILD_DIR)/%: src/ipc/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(THREAD_TARGETS): $(BUILD_DIR)/%: src/threads/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread $< -o $@

$(NETWORK_TARGETS): $(BUILD_DIR)/%: src/network/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD_DIR)/mini_shell: $(MINI_SHELL_SOURCES) $(MINI_SHELL_HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(MINI_SHELL_SOURCES) -o $@

test: all
	@output=`$(BUILD_DIR)/hello`; \
		test "$$output" = "Linux systems project is ready."

	@$(BUILD_DIR)/ownership >/dev/null

	@output=`$(BUILD_DIR)/exec_basic`; \
		printf '%s\n' "$$output" | grep -q "hello from exec"

	@output=`$(BUILD_DIR)/pipe_basic`; \
		printf '%s\n' "$$output" | grep -q "hello through pipe"

	@output=`$(BUILD_DIR)/pipe_fork`; \
		printf '%s\n' "$$output" | grep -q "message from child"

	@output=`$(BUILD_DIR)/dup2_basic 2>&1`; \
		printf '%s\n' "$$output" | grep -q "hello through redirected stdout"

	@output=`$(BUILD_DIR)/pipe_exec`; \
		printf '%s\n' "$$output" | grep -q "hello from child through pipe"

	@output=`$(BUILD_DIR)/pipeline_two`; \
		printf '%s\n' "$$output" | grep -q '^6$$'

	@python3 tests/test_mini_shell.py

	@echo "All checks passed."

sanitizer-test:
	$(MAKE) clean
	ASAN_OPTIONS='detect_leaks=1:halt_on_error=1' \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
	$(MAKE) \
		CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Werror -g3 -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' \
		test

clean:
	rm -rf $(BUILD_DIR)
