CC = gcc
CFLAGS = -I./include -pthread

SRC_DIR = src
BIN_DIR = bin

SERVER_SRC = $(SRC_DIR)/server.c $(SRC_DIR)/file_handler.c $(SRC_DIR)/user_handler.c $(SRC_DIR)/session.c $(SRC_DIR)/item_handler.c $(SRC_DIR)/logger.c
CLIENT_SRC = $(SRC_DIR)/client.c

all: init_dirs server client init_db

server: $(SERVER_SRC)
	$(CC) $(CFLAGS) $(SERVER_SRC) -o $(BIN_DIR)/server

client: $(CLIENT_SRC)
	$(CC) $(CFLAGS) $(CLIENT_SRC) -o $(BIN_DIR)/client

# Create required directories
init_dirs:
	mkdir -p $(BIN_DIR) logs

# Create empty binary data files if they don't exist
init_db:
	mkdir -p data
	touch data/users.dat
	touch data/items.dat

clean:
	rm -f $(BIN_DIR)/server $(BIN_DIR)/client
	rm -rf data logs

# ---- Docker Targets ----

# Pull the image from DockerHub and start the server container
docker-up:
	docker-compose up -d
	@echo "Server is running on port 8085"
	@echo "Run 'make init_dirs client && ./bin/client' to connect"

# Stop the server container
docker-down:
	docker-compose down

# Stop and remove all persisted data
docker-clean:
	docker-compose down -v
