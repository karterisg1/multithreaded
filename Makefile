CC = gcc
CFLAGS = -pthread -Wextra -O2
BIN = ./bin
BUILD = ./build
SRC = ./src

all: jobExecutorServer jobCommander

jobExecutorServer: $(BUILD)/jobExecutorServer.o
	$(CC) $(CFLAGS) -o $(BIN)/jobExecutorServer $(BUILD)/jobExecutorServer.o

jobCommander: $(BUILD)/jobCommander.o
	$(CC) $(CFLAGS) -o $(BIN)/jobCommander $(BUILD)/jobCommander.o

$(BUILD)/jobExecutorServer.o: $(SRC)/jobExecutorServer.c
	$(CC) $(CFLAGS) -c $(SRC)/jobExecutorServer.c -o $(BUILD)/jobExecutorServer.o

$(BUILD)/jobCommander.o: $(SRC)/jobCommander.c
	$(CC) $(CFLAGS) -c $(SRC)/jobCommander.c -o $(BUILD)/jobCommander.o

clean:
	rm -f $(BUILD)/*.o $(BIN)/jobExecutorServer $(BIN)/jobCommander
