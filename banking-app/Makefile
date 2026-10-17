CC = gcc
CFLAGS = -Wall -Wextra -I. -pthread -g

BIN_DIR = bin
DATA_DIR = data

SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client
SEED_BIN   = $(BIN_DIR)/seed_db
TEST_LOCKS = $(BIN_DIR)/test_locks
TEST_AUTH  = $(BIN_DIR)/test_auth
TEST_CUST  = $(BIN_DIR)/test_customer

OBJS_COMMON = $(BIN_DIR)/network.o
OBJS_SERVER = $(BIN_DIR)/db.o $(BIN_DIR)/lock.o $(BIN_DIR)/auth.o $(BIN_DIR)/customer_ops.o $(BIN_DIR)/connection_handler.o

all: directories $(SERVER_BIN) $(CLIENT_BIN) $(SEED_BIN) $(TEST_LOCKS) $(TEST_AUTH) $(TEST_CUST)

directories:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(DATA_DIR)

# Common objects
$(BIN_DIR)/network.o: common/network.c common/network.h common/protocol.h
	$(CC) $(CFLAGS) -c common/network.c -o $(BIN_DIR)/network.o

# Server objects
$(BIN_DIR)/db.o: server/db.c server/db.h common/models.h
	$(CC) $(CFLAGS) -c server/db.c -o $(BIN_DIR)/db.o

$(BIN_DIR)/lock.o: server/lock.c server/lock.h server/db.h common/models.h
	$(CC) $(CFLAGS) -c server/lock.c -o $(BIN_DIR)/lock.o

$(BIN_DIR)/auth.o: server/auth.c server/auth.h server/db.h server/lock.h common/models.h common/protocol.h
	$(CC) $(CFLAGS) -c server/auth.c -o $(BIN_DIR)/auth.o

$(BIN_DIR)/customer_ops.o: server/customer_ops.c server/customer_ops.h server/db.h server/lock.h common/models.h common/protocol.h
	$(CC) $(CFLAGS) -c server/customer_ops.c -o $(BIN_DIR)/customer_ops.o

$(BIN_DIR)/connection_handler.o: server/connection_handler.c server/connection_handler.h server/auth.h server/customer_ops.h server/lock.h common/protocol.h common/network.h
	$(CC) $(CFLAGS) -c server/connection_handler.c -o $(BIN_DIR)/connection_handler.o

# Binaries
$(SERVER_BIN): server/main.c $(OBJS_COMMON) $(OBJS_SERVER) common/models.h common/protocol.h
	$(CC) $(CFLAGS) server/main.c $(OBJS_COMMON) $(OBJS_SERVER) -o $(SERVER_BIN)

$(CLIENT_BIN): client/main.c $(OBJS_COMMON) common/models.h common/protocol.h
	$(CC) $(CFLAGS) client/main.c $(OBJS_COMMON) -o $(CLIENT_BIN)

$(SEED_BIN): scripts/seed_db.c $(BIN_DIR)/db.o common/models.h
	$(CC) $(CFLAGS) scripts/seed_db.c $(BIN_DIR)/db.o -o $(SEED_BIN)

$(TEST_LOCKS): scripts/test_locks.c $(BIN_DIR)/db.o $(BIN_DIR)/lock.o common/models.h
	$(CC) $(CFLAGS) scripts/test_locks.c $(BIN_DIR)/db.o $(BIN_DIR)/lock.o -o $(TEST_LOCKS)

$(TEST_AUTH): scripts/test_auth.c $(OBJS_COMMON) $(OBJS_SERVER) common/models.h common/protocol.h
	$(CC) $(CFLAGS) scripts/test_auth.c $(OBJS_COMMON) $(OBJS_SERVER) -o $(TEST_AUTH)

$(TEST_CUST): scripts/test_customer.c $(OBJS_COMMON) $(OBJS_SERVER) common/models.h common/protocol.h
	$(CC) $(CFLAGS) scripts/test_customer.c $(OBJS_COMMON) $(OBJS_SERVER) -o $(TEST_CUST)

seed: directories $(SEED_BIN)
	@./$(SEED_BIN)

test-locks: directories $(TEST_LOCKS)
	@./$(TEST_LOCKS)

test-auth: directories $(TEST_AUTH)
	@./$(TEST_AUTH)

test-customer: directories $(TEST_CUST)
	@./$(TEST_CUST)

clean:
	rm -rf $(BIN_DIR)

clean-data:
	rm -f $(DATA_DIR)/*.dat

.PHONY: all directories clean clean-data seed test-locks test-auth test-customer
