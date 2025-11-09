# Makefile ISY Messagerie

CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
SRC = src
OBJ = obj
BIN = bin

all: directories $(BIN)/ServeurISY $(BIN)/GroupeISY $(BIN)/ClientISY $(BIN)/AffichageISY
	@echo "=== Compilation terminee ==="

directories:
	@mkdir -p $(OBJ) $(BIN)

$(BIN)/ServeurISY: $(OBJ)/ServeurISY.o $(OBJ)/Utilitaires.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/GroupeISY: $(OBJ)/GroupeISY.o $(OBJ)/Utilitaires.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/ClientISY: $(OBJ)/ClientISY.o $(OBJ)/Utilitaires.o
	$(CC) $(CFLAGS) -o $@ $^

$(BIN)/AffichageISY: $(OBJ)/AffichageISY.o $(OBJ)/Utilitaires.o
	$(CC) $(CFLAGS) -o $@ $^

$(OBJ)/%.o: $(SRC)/%.c include/Commun.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ)/*.o

mrproper: clean
	rm -rf $(BIN)/*

.PHONY: all clean mrproper directories