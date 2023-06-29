#ifndef PROGRAM_INFO_H
#define PROGRAM_INFO_H

#define MAX_NAME_LENGTH 100

typedef struct program_info {
     pid_t pid; // guarda o PID do programa.
     char name[MAX_NAME_LENGTH]; // guarda o nome do programa.
     long int time; // guarda o start_time
} program_info;

typedef struct end_info {
     pid_t pid; // guarda o PID do programa.
     long int time; // guarda o end_time
} end_info;

typedef struct uniq_msg {
     int array_index; // guarda o índice do array (de nomes de programas) correspondente a este nome
     char uniq_name[MAX_NAME_LENGTH]; // guarda o nome do programa
} uniq_msg;

#endif
