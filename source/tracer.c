#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "program_info.h"

long int getCurTime() {
     long int res;

     //tv vai guardar em tv_sec o tempo em segundos desde 1/1/1970 00:00 e em tv_usec os microssegundos adicionais
     struct timeval tv;
     gettimeofday(&tv, NULL);
     //armazenamos na variável res a soma de tv_sec e tv_usec, ambos convertidos em milissegundos
     res = tv.tv_sec * 1000 + tv.tv_usec / 1000;

     return res;
}

void execute_program(char** program_args, int argc) {
     program_args[argc] = NULL;

     // Instância da struct program_info que guarda o pid do programa, o nome e o start_time
     program_info info;
     info.pid = getpid();
     strcpy(info.name,program_args[0]);
     info.time = getCurTime();

     char buffer[sizeof(program_info)+1]; // buffer a ser enviado na 1ª notificação - início da execução do programa
     buffer[0] = '1'; // o 1º byte contém o tipo de mensagem. Neste caso 1 é uma mensagem de início da execução do programa
     memcpy(buffer+1, &info, sizeof(program_info));

     // Notificação ao servidor do start_time associado ao pid e ao nome do programa
     int fifofd;
     if ((fifofd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     if (write(fifofd, buffer, sizeof(buffer)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifofd);
          exit(1);
     }

     close(fifofd);

     printf("Running PID %d\n", info.pid);

     // Execução do programa
     pid_t child;
     int status;
     if ((child = fork()) == 0) {
          execvp(program_args[0],program_args);
          _exit(64); // encerrar o processo filho em caso de erro no execvp
     } else {
          wait(&status);
          if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
               fprintf(stderr, "Erro: não foi possível executar o programa. Código de saída: %d.\n", WEXITSTATUS(status));
          }
     }

     // Notificação ao servidor do end_time associado ao pid do programa
     end_info end;
     end.pid = info.pid;
     end.time = getCurTime();

     char buffer2[sizeof(end_info)+1]; // buffer a ser enviado na 2ª notificação - fim da execução do programa
     buffer2[0] = '2'; //o 1º byte contém o tipo de mensagem. Neste caso 2 é uma mensagem de fim da execução do programa
     memcpy(buffer2+1, &end, sizeof(end_info));

     if ((fifofd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     if (write(fifofd, buffer2, sizeof(buffer2)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifofd);
          exit(1);
     }

     close(fifofd);

     // Notificação ao utilizador do tempo de execução utilizado pelo programa
     printf("Ended in %ld ms\n", end.time-info.time);
}

void getCurrentStatus() {
     // Uso do pid para obter um nome único para criação do pipe com nome "próprio"
     pid_t pid = getpid();
     char pid_str[20];
     sprintf(pid_str, "%d", pid);
     char fifo_name[25] = "own_";
     strcat(fifo_name,pid_str);

     // Criação do pipe com nome "próprio" no qual o monitor vai escrever o conteúdo do status e do qual o programa tracer vai ler
     if (mkfifo(fifo_name,0666) == -1) {
          if (errno != EEXIST) { // Só deu erro se o fifo ainda não existir
               perror("Erro ao criar o pipe com nome.");
               exit(1);
          }
     }

     // Envio do nome do fifo "próprio" para o monitor via fifo "geral" ("fifo")
     int fifo_fd;
     if ((fifo_fd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     char send_msg[strlen(fifo_name)+1]; // mensagem a ser enviada (tipo de mensagem (1 byte) e nome do fifo (strlen bytes))
     send_msg[0] = '3'; // o 1º byte contém o tipo de mensagem. Neste caso 3 é uma mensagem de pedido de status
     memcpy(send_msg+1, fifo_name, strlen(fifo_name));

     if (write(fifo_fd, send_msg, sizeof(send_msg)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifo_fd);
          exit(1);
     }

     close(fifo_fd);

     // abrir o próprio pipe com nome para leitura
     int ownfd;
     if ((ownfd = open(fifo_name, O_RDONLY)) == -1) {
          perror("Erro ao abrir o fifo para leitura.");
          exit(1);
     }

     // Leitura da mensagem do monitor no fifo "próprio" e escrita no stdout
     size_t msg_size = 0;
     size_t max_msg_size = 100;
     char *rcv_msg = malloc(max_msg_size * sizeof(char));
     ssize_t bytes_read = 0;

     while((bytes_read = read(ownfd,rcv_msg + msg_size, 100)) > 0) {
          msg_size += bytes_read;

          if (msg_size >= max_msg_size) {
               max_msg_size *= 2;
               rcv_msg = realloc(rcv_msg, max_msg_size);
          }
     }
     rcv_msg[msg_size] = '\0';

     if (bytes_read == -1) {
          perror("Erro ao ler do fifo \"próprio\".");
          exit(1);
     }

     write(STDOUT_FILENO, rcv_msg, strlen(rcv_msg));

     free(rcv_msg);
     close(ownfd);
     unlink(fifo_name);
}

void statsTimeUniq(char type, pid_t *array, int n_pids) {
     int i;
     int pids_array[n_pids];
     for(i=0;i<n_pids;i++) pids_array[i] = array[i];

     // Uso do pid para obter um nome único para criação do pipe com nome "próprio"
     pid_t pid = getpid();
     char pid_str[20];
     sprintf(pid_str, "%d", pid);
     char fifo_name[25] = "stats_";
     strcat(fifo_name,pid_str);
     fifo_name[strlen(fifo_name)] = '\0';
     int name_length = strlen(fifo_name);

     // Criação do pipe com nome "próprio" no qual o monitor vai escrever a mensagem de stats-time e do qual o programa tracer vai ler
     if (mkfifo(fifo_name,0666) == -1) {
          if (errno != EEXIST) { // Só deu erro se o fifo ainda não existir
               perror("Erro ao criar o pipe com nome.");
               exit(1);
          }
     }

     // Criação de uma mensagem com: Tipo de mensagem (1º byte), tamanho do nome do fifo, nome do fifo, nº de pids e o array com os pids
     char send_msg[1 + strlen(fifo_name) + sizeof(name_length) + sizeof(n_pids) + sizeof(pids_array)]; // mensagem a enviar (tipo de mensagem (1 byte) + nome do fifo (strlen bytes) + name_length (sizeof bytes) + n_pids (sizeof bytes) + pids_array (sizeof bytes))
     send_msg[0] = type; // o 1º byte contém o tipo de mensagem. Neste caso a variável type, que ou é '4' ou '5'
     memcpy(send_msg+1, &name_length, sizeof(name_length));
     memcpy(send_msg+1+sizeof(name_length), fifo_name, strlen(fifo_name));
     memcpy(send_msg+1+sizeof(name_length)+strlen(fifo_name), &n_pids, sizeof(n_pids));
     memcpy(send_msg+1+sizeof(name_length)+strlen(fifo_name)+sizeof(n_pids), &pids_array, sizeof(pids_array));

     int fifo_fd;
     if ((fifo_fd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     if (write(fifo_fd, send_msg, sizeof(send_msg)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifo_fd);
          exit(1);
     }

     close(fifo_fd);

     // abrir o próprio pipe com nome para leitura
     int ownfd;
     if ((ownfd = open(fifo_name, O_RDONLY)) == -1) {
          perror("Erro ao abrir o fifo \"próprio\" para leitura.");
          exit(1);
     }

     if (type == '4') { // stats-time
          int exec_times_sum;
          // Leitura do próprio pipe com nome do inteiro correspondente ao tempo total de execução dos programas
          if ((read(ownfd,&exec_times_sum,sizeof(exec_times_sum))) == -1) {
               perror("Erro ao ler do fifo \"próprio\" o tempo total de execução dos programas.");
               exit(1);
          }

          printf("Total execution time is %d ms\n", exec_times_sum);
     } else if (type == '5') { // stats-uniq
          char prog_names_array[n_pids][MAX_NAME_LENGTH]; // Guardar os nomes dos programas dos PIDS pretendidos

          // Leitura do próprio pipe com nome do array de nomes de programas
          if ((read(ownfd,&prog_names_array,sizeof(prog_names_array))) == -1) {
               perror("Erro ao ler do fifo \"próprio\" o tempo total de execução dos programas.");
               exit(1);
          }

          int it;
          for (it = 0; it < n_pids; it++) {
               if (strcmp(prog_names_array[it],"") != 0) {
                    printf("%s\n", prog_names_array[it]);
               }
          }
     }

     close(ownfd);
     unlink(fifo_name);
}

void execute_program2(char* program) {
     // Instância da struct program_info que guarda o pid do programa, o nome e o start_time
     program_info info;
     info.pid = getpid();
     strcpy(info.name,program);
     info.time = getCurTime();

     char buffer[sizeof(program_info)+1]; // buffer a ser enviado na 1ª notificação - início da execução do programa
     buffer[0] = '1'; // o 1º byte contém o tipo de mensagem. Neste caso 1 é uma mensagem de início da execução do programa
     memcpy(buffer+1, &info, sizeof(program_info));

     // Notificação ao servidor do start_time associado ao pid e ao nome do programa
     int fifofd;
     if ((fifofd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     if (write(fifofd, buffer, sizeof(buffer)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifofd);
          exit(1);
     }

     close(fifofd);

     printf("Running PID %d\n", info.pid);

     // Divide o programa inteiro em sub_programas (que estão na string original divididos por '|')
     char **sub_programs = (char **) malloc(strlen(program) * sizeof(char *));
     int nr_sub_progs = 0;
     int i;
     int j = 0;
     for(i=0;i<strlen(program);i++) {
          if(program[i] == '|') {
               sub_programs[nr_sub_progs] = malloc((i-2-j+1)* sizeof(char));
               strncpy(sub_programs[nr_sub_progs],&program[j],i-2-j+1);
               nr_sub_progs++;
               i++;
               j=i+1;
          }
     }
     sub_programs[nr_sub_progs] = malloc((i-j)* sizeof(char));
     strncpy(sub_programs[nr_sub_progs],&program[j],i-j);
     nr_sub_progs++;
     for(i=0;i<nr_sub_progs;i++) sub_programs[i][strlen(sub_programs[i])] = '\0';

     // Criação dos pipes anónimos responsáveis por encadear os inputs e outputs dos sub_programas entre processos-filhos
     int pipe_fd[nr_sub_progs-1][2];
     for(i=0;i<nr_sub_progs-1;i++) {
          if (pipe(pipe_fd[i]) == -1) {
               perror("Erro ao criar o pipe");
               exit(1);
          }
     }


     // Criação de um primeiro processo-filho que redireciona o descritor do STDOUT_FILENO para o descritor de escrita do pipe
     if(fork() == 0) {
          char **args = NULL;
          int n_args = 0;
          char *token;
          token = strtok(sub_programs[0], " ");
          while(token != NULL) {
               n_args++;
               args = realloc(args, n_args * sizeof(char *));
               args[n_args-1] = token;

               token = strtok(NULL, " ");
          }
          n_args++;
          args = realloc(args, n_args * sizeof(char *));
          args[n_args-1] = NULL;

          int iter;
          for (iter = 0; iter < (nr_sub_progs-1);iter++) {
               close(pipe_fd[iter][0]);
               if (iter != 0) {
                    close(pipe_fd[iter][1]);
               }
          }
          dup2(pipe_fd[0][1],STDOUT_FILENO);
          close(pipe_fd[0][1]);

          execvp(args[0],args);
          free(args); // liberta a memória alocada em caso de falha do exec, caso contrário, o término do exec faz este free "automaticamente"
          exit(1); // se o código do processo-filho chegar até aqui é porque o exec teve algum problema
     }

     // Criação de nr_sub_progs-2 (sem contar com o 1º e o último) processos-filho que encadeiam os seus inputs e outputs através dos pipes anónimos
     for (i=1;i<nr_sub_progs-1;i++) {
          if (fork() == 0) {
               char **args = NULL;
               int n_args = 0;
               char *token;
               token = strtok(sub_programs[i], " ");
               while(token != NULL) {
                    n_args++;
                    args = realloc(args, n_args * sizeof(char *));
                    args[n_args-1] = token;

                    token = strtok(NULL, " ");
               }
               n_args++;
               args = realloc(args, n_args * sizeof(char *));
               args[n_args-1] = NULL;

               int iter;
               for (iter = 0; iter < (nr_sub_progs-1);iter++) {
                    if (iter != i-1) {
                         close(pipe_fd[iter][0]);
                    }
                    if (iter != i) {
                         close(pipe_fd[iter][1]);
                    }
               }

               dup2(pipe_fd[i-1][0],STDIN_FILENO);
               close(pipe_fd[i-1][0]);
               dup2(pipe_fd[i][1],STDOUT_FILENO);
               close(pipe_fd[i][1]);

               execvp(args[0],args);
               free(args); // liberta a memória alocada em caso de falha do exec, caso contrário, o término do exec faz este free "automaticamente"
               exit(1); // se o código do processo-filho chegar até aqui é porque o exec teve algum problema
          }
     }

     // Último processo-filho que vai fazer o último exec que imprime no STDOUT
     if(fork() == 0) {
          char **args = NULL;
          int n_args = 0;
          char *token;
          token = strtok(sub_programs[nr_sub_progs-1], " ");
          while(token != NULL) {
               n_args++;
               args = realloc(args, n_args * sizeof(char *));
               args[n_args-1] = token;

               token = strtok(NULL, " ");
          }
          n_args++;
          args = realloc(args, n_args * sizeof(char *));
          args[n_args-1] = NULL;

          int iter;
          for (iter = 0; iter < (nr_sub_progs-1);iter++) {
               close(pipe_fd[iter][1]);
               if (iter != (nr_sub_progs-2)) {
                    close(pipe_fd[iter][0]);
               }
          }
          dup2(pipe_fd[nr_sub_progs-2][0],STDIN_FILENO);
          close(pipe_fd[nr_sub_progs-2][0]);

          execvp(args[0],args);
          free(args); // liberta a memória alocada em caso de falha do exec, caso contrário, o término do exec faz este free "automaticamente"
          exit(1); // se o código do processo-filho chegar até aqui é porque o exec teve algum problema
     }

     for (i=0;i<nr_sub_progs-1;i++) {
          close(pipe_fd[i][0]);
     }
     for (i=0;i<nr_sub_progs-1;i++) {
          close(pipe_fd[i][1]);
     }

     for (i=0;i<nr_sub_progs;i++) {
          wait(NULL);
     }

     /* ALTERNATIVA PARA A EXECUÇÃO ENCADEADA
     for(i=0;i<nr_sub_progs;i++) {
          if (fork() == 0) {
               char **args = NULL;
               int n_args = 0;
               char *token;
               token = strtok(sub_programs[i], " ");
               while(token != NULL) {
                    n_args++;
                    args = realloc(args, n_args * sizeof(char *));
                    args[n_args-1] = token;

                    token = strtok(NULL, " ");
               }
               n_args++;
               args = realloc(args, n_args * sizeof(char *));
               args[n_args-1] = NULL;

               printf("%d: %s.\n", i, args[2]);

               if (i == 0) {
                    close(pipe_fd[i][0]);
                    dup2(pipe_fd[i][1], STDOUT_FILENO);
                    close(pipe_fd[i][1]);
               }
               else if (i == nr_sub_progs - 1) {
                    close(pipe_fd[i - 1][1]);
                    dup2(pipe_fd[i - 1][0], STDIN_FILENO);
                    close(pipe_fd[i - 1][0]);
               }
               else {
                    close(pipe_fd[i - 1][1]);
                    close(pipe_fd[i][0]);
                    dup2(pipe_fd[i - 1][0], STDIN_FILENO);
                    close(pipe_fd[i - 1][0]);
                    dup2(pipe_fd[i][1], STDOUT_FILENO);
                    close(pipe_fd[i][1]);
               }

               if (execvp(args[0], args) == -1) {
                    perror("execvp");
                    exit(EXIT_FAILURE);
               }
          }
     }

     for(i=0;i<nr_sub_progs-1;i++) {
          close(pipe_fd[i][0]);
          close(pipe_fd[i][1]);
     }

     for(i=0;i<nr_sub_progs;i++) {
          wait(NULL);
     }*/

     for(i=0;i<nr_sub_progs;i++) free(sub_programs[i]);
     free(sub_programs);

     // Notificação ao servidor do end_time associado ao pid do programa
     end_info end;
     end.pid = info.pid;
     end.time = getCurTime();

     char buffer2[sizeof(end_info)+1]; // buffer a ser enviado na 2ª notificação - fim da execução do programa
     buffer2[0] = '2'; //o 1º byte contém o tipo de mensagem. Neste caso 2 é uma mensagem de fim da execução do programa
     memcpy(buffer2+1, &end, sizeof(end_info));

     if ((fifofd = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     if (write(fifofd, buffer2, sizeof(buffer2)) == -1) {
          perror("Erro ao escrever no fifo.");
          close(fifofd);
          exit(1);
     }

     close(fifofd);

     // Notificação ao utilizador do tempo de execução utilizado pelo programa
     printf("Ended in %ld ms\n", end.time-info.time);
}

int main(int argc, char* argv[]) {
     // É necessário, no mínimo, um argumento para além do executável
     if (argc < 2) {
          fprintf(stderr, "Erro: faltou especificar uma opção.\n");
          fprintf(stderr, "Opções disponíveis: execute, status, stats-command, stats-uniq.\n");
          return 1;
     }

     // Tratamento e execução das funcionalidades
     if(!strcmp(argv[1],"execute")) {
          // A funcionalidade "execute" tem, pelo menos, 3 argumentos após o executável:
          // "execute"; flag (-u ou -p); nome do programa a executar + possíveis argumentos do programa
          if (argc < 4) {
               fprintf(stderr, "Erro: Invocação inválida da opção \"execute\". Formato correto:\n");
               fprintf(stderr, "./tracer execute -[u/p] \"(progs e args)\"\n");
               return 1;
          }

          //verificação da flag utilizada
          if (!strcmp(argv[2], "-u")) {
               execute_program(&argv[3], argc-3);
          } else if (!strcmp(argv[2], "-p")) {
               execute_program2(argv[3]);
          } else {
               fprintf(stderr, "Erro: flag inválida.\n");
          }
     } else if(!strcmp(argv[1],"status")) {
          // A funcionaliade "status" tem, obrigatoriamente, apenas 1 argumento para além do executável (o próprio "status")
          if (argc != 2) {
               fprintf(stderr, "Erro: Nº de argumentos inválido para o comando \"status\".\n");
          } else {
               getCurrentStatus();
          }
     } else if(!strcmp(argv[1],"stats-time")) {
          // A funcionaliade "stats-time" tem de ser executada com, pelo menos, um PID
          if (argc < 3) {
               fprintf(stderr, "Erro: Nº de argumentos inválido para o comando \"stats-time\". Insira, pelo menos, um PID.\n");
          } else {
               pid_t pids[argc-2];
               int i;
               for(i=2; i<argc; i++) {
                    pids[i-2] = atoi(argv[i]);
               }

               statsTimeUniq('4', pids, argc-2);
          }
     } else if(!strcmp(argv[1],"stats-command")) {
          printf("Funcionalidade não implementada..\n");
     } else if(!strcmp(argv[1],"stats-uniq")) {
          // A funcionaliade "stats-uniq" tem de ser executada com, pelo menos, um PID
          if (argc < 3) {
               fprintf(stderr, "Erro: Nº de argumentos inválido para o comando \"stats-uniq\". Insira, pelo menos, um PID.\n");
          } else {
               pid_t pids[argc-2];
               int i;
               for(i=2; i<argc; i++) {
                    pids[i-2] = atoi(argv[i]);
               }

               statsTimeUniq('5', pids, argc-2);
          }
     } else {
          printf("Opção inválida. Opções disponíveis:\n");
          printf("./tracer execute -u \"prog-a arg-1 (...) arg-n\"\n");
          printf("./tracer execute -p \"prog-a arg-1 (...) arg-n | prog-b arg-1 (...) arg-n | (...)\"\n");
          printf("./tracer status\n");
          printf("./tracer stats-time PID-1 PID-2 (...) PID-N\n");
          printf("./tracer stats-command prog-a PID-1 PID-2 (...) PID-N\n");
          printf("./tracer stats-uniq PID-1 PID-2 (...) PID-N\n");
          return 1;
     }

     return 0;
}
