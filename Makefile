CC       = gcc
CFLAGS   = -Wall -Wextra -g -pthread
SRC_DIR  = src/c-agent
BIN_DIR  = bin
BUILD_DIR = build
TARGET   = $(BIN_DIR)/main

ERL_DIR  = src/erlang-scheduler
ERL_SRCS = loggerScheduler.erl tcpClient.erl manejoRecursos.erl \
           jobWorker.erl scheduler.erl main.erl

SRCS = $(shell find $(SRC_DIR) -name '*.c')
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

.PHONY: all erlang run valgrind test clean

# ── Compilación C ──────────────────────────────────────────────────────────────
all: $(BUILD_DIR) $(BIN_DIR) $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -I$(SRC_DIR) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# ── Compilación Erlang ─────────────────────────────────────────────────────────
erlang:
	cd $(ERL_DIR) && erlc -I../../include $(ERL_SRCS)

# ── Ejecución ──────────────────────────────────────────────────────────────────
run: all
	./$(TARGET)

valgrind: all
	valgrind -s --leak-check=full --track-origins=yes --show-leak-kinds=all ./$(TARGET)

# ── Tests ──────────────────────────────────────────────────────────────────────
test:
	bash test/test_deadlock_simple.sh

# ── Limpieza ───────────────────────────────────────────────────────────────────
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(TARGET)
	rm -f $(ERL_DIR)/*.beam
