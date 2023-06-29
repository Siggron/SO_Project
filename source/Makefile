TMP_DIR := temporary_files
OBJ_DIR := object_files
EXE_DIR := executable_files

all: ficheiros servidor cliente # Primeira instrução

ficheiros: # Cria as diretorias na pasta do projecto
	@mkdir -p $(TMP_DIR) $(OBJ_DIR) $(EXE_DIR)

servidor: $(EXE_DIR)/monitor # Makefile para a parte do servidor

$(EXE_DIR)/monitor: $(OBJ_DIR)/monitor.o # Criação do ficheiro executável do servidor (monitor)
	gcc -g $(OBJ_DIR)/monitor.o -o $(EXE_DIR)/monitor

$(OBJ_DIR)/monitor.o: monitor.c  # Criação do ficheiro objeto do servidor (monitor.o)
	gcc -Wall -g -c monitor.c -o $(OBJ_DIR)/monitor.o

cliente: $(EXE_DIR)/tracer  # Makefile para a parte do cliente

$(EXE_DIR)/tracer: $(OBJ_DIR)/tracer.o # Criação do ficheiro executável do cliente (tracer)
	gcc -g $(OBJ_DIR)/tracer.o -o $(EXE_DIR)/tracer

$(OBJ_DIR)/tracer.o: tracer.c # Criação do ficheiro objeto do cliente (tracer.o)
	gcc -Wall -g -c tracer.c -o $(OBJ_DIR)/tracer.o

clean: # Instrução para limpar as pastas criadas com o Makefile.
	rm -rf $(TMP_DIR) $(OBJ_DIR) $(EXE_DIR)
