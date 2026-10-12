CC = gcc
CFLAGS = -Wall -Wextra -I. -pthread -g

BIN_DIR = bin
DATA_DIR = data

SERVER_BIN = $(BIN_DIR)/server
CLIENT_BIN = $(BIN_DIR)/client
SEED_BIN   = $(BIN_DIR)/seed_db

all: directories $(SERVER_BIN) $(CLIENT_BIN)

directories:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(DATA_DIR)

$(SERVER_BIN): server/main.c common/models.h common/protocol.h
	$(CC) $(CFLAGS) server/main.c -o $(SERVER_BIN)

$(CLIENT_BIN): client/main.c common/models.h common/protocol.h
	$(CC) $(CFLAGS) client/main.c -o $(CLIENT_BIN)

clean:
	rm -rf $(BIN_DIR)

clean-data:
	rm -f $(DATA_DIR)/*.dat

.PHONY: all directories clean clean-data
