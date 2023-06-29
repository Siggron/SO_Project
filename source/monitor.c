#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include "program_info.h"

#define BUFFER_SIZE 124
long int getCurTime() {
     long int res;

     //tv vai guardar em tv_sec o tempo em segundos desde 1/1/1970 00:00 e em tv_usec os microssegundos adicionais
     struct timeval tv;
     gettimeofday(&tv, NULL);
     //armazenamos na variável res a soma de tv_sec e tv_usec, ambos convertidos em milissegundos
     res = tv.tv_sec * 1000 + tv.tv_usec / 1000;

     return res;
}

int main(int argc, char* argv[]) {
     // O programa monitor deve ter obrigatoriamente 1 argumento para além do próprio executável.
     // O argumento (argv[1]) corresponde ao nome da pasta onde vão ser guardados
     //   ficheiros com informações sobre programas cuja execução já terminou
     if (argc != 2) {
          fprintf(stderr, "Erro: Nº de argumentos inválido.\n");
          return 1;
     }

     // Armazenamento do nome da pasta e criação da mesma caso ainda não exista
     char *folder_path = malloc(strlen(argv[1])+1);
     strcpy(folder_path, argv[1]);
     folder_path[strlen(argv[1])] = '\0';

     mkdir(folder_path, 0777);

     int fifofdr, fifofdw; // descritores do fifo
     int max_progs = 5; // capacidade (variável) de programas a correr ao mesmo tempo
     int num_run_progs = 0; // número de programas em execução
     program_info* run_progs = (program_info*) malloc(max_progs * sizeof(program_info)); // guarda os programas em execução


     //Criação do fifo
     if (mkfifo("fifo",0666) == -1) {
          if (errno != EEXIST) { // Só deu erro se o fifo ainda não existir
               perror("Erro ao criar o pipe com nome.");
               exit(1);
          }
     }

     // abrir o fifo para leitura
     if ((fifofdr = open("fifo", O_RDONLY)) == -1) {
          perror("Erro ao abrir o fifo para leitura.");
          exit(1);
     }

     // abrir o fifo para escrita - evita que o monitor continue a tentar ler do fifo quando não há nenhum cliente a tentar escrever
     if ((fifofdw = open("fifo", O_WRONLY)) == -1) {
          perror("Erro ao abrir o fifo para escrita.");
          exit(1);
     }

     while(1) {
          //ler o 1º byte da mensagem para descobrir o tipo de mensagem a receber
          char type;
          if (read(fifofdr,&type,1) == -1) {
               perror("Erro ao ler o 1º byte do fifo.");
               exit(1);
          }

          switch(type) {
               case '1': // mensagem de início da execução de um programa
                    program_info tmp_info;

                    // lê do fifo e guarda na variável tmp_info os dados do programa cuja execução começou
                    if(read(fifofdr,&tmp_info,sizeof(program_info)) == -1) {
                         perror("Erro ao ler a mensagem do fifo.");
                         exit(1);
                    }

                    // adiciona a variável tmp_info ao array dos programas em execução
                    if (num_run_progs < max_progs) {
                         run_progs[num_run_progs] = tmp_info;
                         num_run_progs++;
                    } else {
                         max_progs *= 2;
                         run_progs = realloc(run_progs, max_progs * sizeof(program_info));
                         run_progs[num_run_progs] = tmp_info;
                         num_run_progs++;
                    }

                    printf("[EXEC START] %d;%s;%ld\n", tmp_info.pid, tmp_info.name, tmp_info.time);
                    break;
               case '2': // mensagem de fim da execução de um programa
                    end_info tmp_end;

                    if(read(fifofdr,&tmp_end,sizeof(end_info)) == -1) {
                         perror("Erro ao ler a mensagem do fifo.");
                         exit(1);
                    }

                    // Criação de um processo para criar um ficheiro com a info sobre o programa terminado, enquanto o pai pode atender outros clientes
                    if (fork() == 0) {
                         close(fifofdr); // O filho não precisa (nem deve) ler do fifo
                         int z; // z vai guardar a posição do array de running programs que tem a info do programa pretendido
                         for (z=0; z < num_run_progs && run_progs[z].pid != tmp_end.pid; z++);
                         long int prog_exec_time = tmp_end.time-run_progs[z].time;

                         // Cria-se e abre-se um ficheiro cujo path é "folder/pid"
                         char pid_str[20];
                         sprintf(pid_str, "%d", tmp_end.pid);
                         char *file_path = malloc(strlen(folder_path) + strlen(pid_str) + 2);
                         sprintf(file_path, "%s/%s", folder_path, pid_str);
                         file_path[strlen(folder_path) + strlen(pid_str) + 1] = '\0';

                         int fd = open(file_path, O_WRONLY | O_CREAT | O_EXCL, 0666);

                         // Se o ficheiro foi criado e aberto com êxito escreve-se no ficheiro o nome do programa terminado
                         // e o seu tempo total de execução em milissegundos, separados por um ';'. A linha termina com um '.'
                         if (fd == -1) {
                              perror("Erro ao criar/abrir o ficheiro.");
                         } else {
                              int tbuf_size = snprintf(NULL, 0, "%s;%ld.", run_progs[z].name, prog_exec_time);
                              char tbuf[tbuf_size + 1];
                              snprintf(tbuf, tbuf_size + 1, "%s;%ld.", run_progs[z].name, prog_exec_time);
                              tbuf[tbuf_size] = '\0';

                              write(fd, tbuf, tbuf_size);
                         }

                         free(file_path);
                         write(fifofdw,"G",1);
                         close(fifofdw);
                         _exit(0);
                    }

                    // remove o programa do array de programas em execução
                    int i, j;
                    for(i=0; i < num_run_progs; i++) {
                         if(run_progs[i].pid == tmp_end.pid) {
                              for (j=i; j < num_run_progs-1; j++) {
                                   run_progs[j] = run_progs[j+1];
                              }
                              num_run_progs--;
                              break;
                         }
                    }

                    printf("[EXEC END] %d;%ld\n", tmp_end.pid, tmp_end.time);

                    break;
               case '3': // mensagem que pede o status dos programas em execução
                    // Leitura via fifo "geral" do nome do fifo "próprio" do cliente
                    char st_name[25];
                    if(read(fifofdr,&st_name,sizeof(st_name)) == -1) {
                         perror("Erro ao ler do fifo o nome do fifo \"próprio\" do cliente.");
                         exit(1);
                    }

                    // Criação de um novo processo para lidar com a tarefa, enquanto o pai pode atender outros clientes
                    if (fork() == 0) {
                         close(fifofdr); // O filho não precisa (nem deve) ler do fifo "geral"

                         // Abertura do fifo "próprio" do cliente para escrita
                         int st_fd;
                         if ((st_fd = open(st_name, O_WRONLY)) == -1) {
                              perror("Erro ao abrir o fifo \"próprio\" do cliente para escrita.");
                              exit(1);
                         }

                         // Construção da mensagem de status a enviar para o cliente que fez o pedido
                         long int cur_time = getCurTime();
                         int l;
                         char* status_msg = NULL;

                         printf("[STATUS] num_run_progs: %d\n", num_run_progs);
                         for(l=0; l < num_run_progs; l++) {
                              int lsize = snprintf(NULL, 0, "%d %s %ld ms\n", run_progs[l].pid, run_progs[l].name, cur_time-run_progs[l].time);
                              char* tmp = malloc(lsize+1);
                              snprintf(tmp, lsize+1, "%d %s %ld ms\n", run_progs[l].pid, run_progs[l].name, cur_time-run_progs[l].time);

                              if (l == 0) status_msg = calloc(lsize+1,sizeof(char));
                              else status_msg = realloc(status_msg, strlen(status_msg) + lsize + 1);

                              strcat(status_msg,tmp);
                              free(tmp);
                         }

                         // Envio da mensagem de status para o cliente, caso haja programas em execução
                         if (status_msg != NULL) {
                              write(st_fd, status_msg, strlen(status_msg));
                         } else {
                              write(st_fd, "Não existem programas em execução.\n", 38);
                         }

                         free(status_msg);
                         close(st_fd);

                         write(fifofdw,"F",1);
                         close(fifofdw);
                         _exit(0);
                    }

                    break;
               case 'F':
                    printf("Terminou a execução de um filho que estava a processar um pedido de status.\n");
                    break;
               case 'G':
                    printf("Terminou a execução de um filho que criou um ficheiro com informações de término de execução de um programa.\n");
                    break;
               case '4':
                    // Leitura via fifo "geral" e armazenamento dos dados necessários para tratamento do pedido stats-time
                    int name_length;
                    if(read(fifofdr,&name_length,sizeof(name_length)) == -1) {
                         perror("Erro ao ler do fifo o tamanho do nome do fifo \"próprio\" do cliente.");
                         exit(1);
                    }

                    char *time_name = (char *) malloc((name_length + 1) * sizeof(char));
                    if(read(fifofdr,time_name,name_length) == -1) {
                         perror("Erro ao ler do fifo o nome do fifo \"próprio\" do cliente.");
                         exit(1);
                    }
                    time_name[name_length] = '\0';


                    int n_pids;
                    if(read(fifofdr,&n_pids,sizeof(n_pids)) == -1) {
                         perror("Erro ao ler do fifo o n_pids.");
                         exit(1);
                    }

                    int *pids_array = malloc(n_pids * sizeof(int));
                    if(read(fifofdr,pids_array,n_pids * sizeof(int)) == -1) {
                         perror("Erro ao ler do fifo o array de pids");
                         exit(1);
                    }

                    // Criação de um novo processo para lidar com a tarefa, enquanto o pai pode atender outros clientes
                    if (fork() == 0) {
                         close(fifofdr); // O filho não precisa (nem deve) ler do fifo "geral"
                         int total_exec_time = 0; // armazena a soma de todos os tempos de exeução dos programas terminados com os pids existentes no pids_array
                         // Criação de um pipe anónimo para este processo receber cada tempo de execução de cada filho que vai criar
                         int fildes[2];

                         if(pipe(fildes) < 0) {
                              perror("pipe");
                              exit(1);
                         }

                         int c, d, status;
                         for(c = 0; c < n_pids; c++) {
                              if(fork() == 0) {
                                   close(fildes[0]); // fecha o descritor de leitura
                                   int exec_time; // variável que vai guardar o tempo de execução do programa

                                   // Construção do pid_path usado para abrir o ficheiro do pid pretendido
                                   int tmp = snprintf(NULL, 0, "%d", pids_array[c]);
                                   char pid_string[tmp+1];
                                   snprintf(pid_string, tmp+1, "%d", pids_array[c]);
                                   pid_string[tmp] = '\0';

                                   char pid_path[strlen(folder_path)+tmp+2];
                                   strcpy(pid_path,folder_path);
                                   strcat(pid_path,"/");
                                   strcat(pid_path,pid_string);
                                   pid_path[tmp+strlen(folder_path)+1] = '\0';

                                   // Abre o ficheiro
                                   int pid_fd;
                                   if((pid_fd = open(pid_path, O_RDONLY)) == -1) {
                                        fprintf(stderr,"[STATS-TIME] Erro ao abrir o ficheiro com o pid_path %s.\n", pid_path);
                                        int zero = 0;
                                        if (write(fildes[1], &zero, sizeof(zero)) == -1) {
                                            perror("Erro na escrita no pipe anónimo.");
                                            exit(1);
                                        }
                                        exit(1);
                                   }

                                   char buffer[BUFFER_SIZE];
                                   char ex_tm_str[BUFFER_SIZE];
                                   int bytesr, it;

                                   while((bytesr = read(pid_fd, buffer, BUFFER_SIZE)) > 0) {
                                        for (it = 0; it < bytesr; it++) {
                                             if(buffer[it] == ';') {
                                                  it++;
                                                  int it2 = 0;
                                                  while(buffer[it] != '.' && it2 < BUFFER_SIZE-1) {
                                                       ex_tm_str[it2] = buffer[it];
                                                       it++;
                                                       it2++;
                                                  }
                                                  ex_tm_str[it2] = '\0';
                                                  break;
                                             }
                                        }
                                   }

                                   close(pid_fd);

                                   exec_time = atoi(ex_tm_str);

                                   if (write(fildes[1], &exec_time, sizeof(exec_time)) == -1) {
                                       perror("Erro na escrita no pipe anónimo.");
                                       exit(1);
                                   }

                                   close(fildes[1]);

                                   _exit(0);
                              }
                         }

                         close(fildes[1]);

                         for(d = 0; d < n_pids; d++) {
                              int each_time;

                              if (read(fildes[0], &each_time, sizeof(each_time)) == -1) {
                                  perror("Erro ao ler do pipe anónimo.");
                                  exit(1);
                              }

                              total_exec_time += each_time;

                              if (wait(&status) > 0) {
                                   if (WIFEXITED(status)) {
                                        printf("[STATS-TIME] Filho saiu com o valor: %d\n", WEXITSTATUS(status));
                                   } else {
                                        printf("[STATS-TIME] bad exit!\n");
                                   }
                              }
                         }

                         close(fildes[0]);

                         printf("[STATS-TIME] Tempo total de execução: %d\n", total_exec_time);

                         // abrir o pipe com nome do cliente para escrita
                         int clnt_fd;
                         if ((clnt_fd = open(time_name, O_WRONLY)) == -1) {
                              perror("Erro ao abrir o fifo \"próprio\" do cliente para escrita.");
                              exit(1);
                         }

                         if((write(clnt_fd, &total_exec_time, sizeof(total_exec_time))) == -1) {
                              perror("Erro ao escrever no fifo \"próprio\" do cliente o tempo total de execução dos programas.");
                              exit(1);
                         }

                         close(clnt_fd);

                         write(fifofdw,"S",1);
                         close(fifofdw);

                         _exit(0);
                    }

                    free(time_name);
                    free(pids_array);
                    break;
               case 'S':
                    printf("Terminou a execução de um filho que estava a processar um pedido de stats-time.\n");
                    break;
               case '5':
                    // Leitura via fifo "geral" e armazenamento dos dados necessários para tratamento do pedido stats-uniq
                    int uniq_length;
                    if(read(fifofdr,&uniq_length,sizeof(uniq_length)) == -1) {
                         perror("Erro ao ler do fifo o tamanho do nome do fifo \"próprio\" do cliente.");
                         exit(1);
                    }

                    char *uniq_name = (char *) malloc((uniq_length + 1) * sizeof(char));
                    if(read(fifofdr,uniq_name,uniq_length) == -1) {
                         perror("Erro ao ler do fifo o nome do fifo \"próprio\" do cliente.");
                         exit(1);
                    }
                    uniq_name[uniq_length] = '\0';

                    int u_pids;
                    if(read(fifofdr,&u_pids,sizeof(u_pids)) == -1) {
                         perror("Erro ao ler do fifo o número de pids.");
                         exit(1);
                    }

                    int *upids_array = malloc(u_pids * sizeof(int));
                    if(read(fifofdr,upids_array,u_pids * sizeof(int)) == -1) {
                         perror("Erro ao ler do fifo o array de pids");
                         exit(1);
                    }

                    // Criação de um novo processo para lidar com a tarefa, enquanto o pai pode atender outros clientes
                    if (fork() == 0) {
                         close(fifofdr); // O filho não precisa (nem deve) ler do fifo "geral"
                         char *prog_names_array[u_pids]; // Guardar os nomes dos programas dos PIDS pretendidos
                         // Criação de um pipe anónimo para este processo receber cada nome de programa de cada filho que vai criar
                         int fdesc[2];

                         if(pipe(fdesc) < 0) {
                              perror("pipe");
                              exit(1);
                         }

                         int e, f, st4tus;
                         for(e = 0; e < u_pids; e++) {
                              if(fork() == 0) {
                                   close(fdesc[0]); // fecha o descritor de leitura
                                   uniq_msg u_msg;
                                   u_msg.array_index = e;

                                   // Construção do pid_path usado para abrir o ficheiro do pid pretendido
                                   int temp = snprintf(NULL, 0, "%d", upids_array[e]);
                                   char pid_str[temp+1];
                                   snprintf(pid_str, temp+1, "%d", upids_array[e]);
                                   pid_str[temp] = '\0';

                                   char upid_path[strlen(folder_path)+temp+2];
                                   strcpy(upid_path,folder_path);
                                   strcat(upid_path,"/");
                                   strcat(upid_path,pid_str);
                                   upid_path[temp+strlen(folder_path)+1] = '\0';

                                   // Abre o ficheiro
                                   int upid_fd;
                                   if((upid_fd = open(upid_path, O_RDONLY)) == -1) {
                                        fprintf(stderr,"[STATS-TIME] Erro ao abrir o ficheiro com o pid_path %s.\n", upid_path);
                                        strcpy(u_msg.uniq_name,"");

                                        if (write(fdesc[1], &u_msg, sizeof(u_msg)) == -1) {
                                             perror("Erro na escrita no pipe anónimo.");
                                             exit(1);
                                        }

                                        exit(1);
                                   }

                                   char buf[101];
                                   int byt_read;
                                   int iter = 0;

                                   while(iter < 100 && (byt_read = read(upid_fd, buf+iter, 1)) > 0 && buf[iter] != ';') {
                                        iter++;
                                   }

                                   buf[iter] = '\0';

                                   close(upid_fd);

                                   strcpy(u_msg.uniq_name,buf);

                                   char u_msg_str[sizeof(u_msg)];
                                   memcpy(u_msg_str, &u_msg, sizeof(uniq_msg));


                                   if (write(fdesc[1], u_msg_str, sizeof(u_msg_str)) == -1) {
                                       perror("Erro na escrita no pipe anónimo.");
                                       exit(1);
                                   }

                                   close(fdesc[1]);

                                   _exit(0);
                              }
                         }

                         close(fdesc[1]);

                         for(f = 0; f < u_pids; f++) {
                              uniq_msg um;

                              if (read(fdesc[0], &um, sizeof(um)) == -1) {
                                  perror("Erro ao ler do pipe anónimo.");
                                  exit(1);
                              }

                              printf("%d: %s\n", um.array_index,um.uniq_name);

                              if (strcmp(um.uniq_name,"") != 0) {
                                   prog_names_array[um.array_index] = (char *) malloc((strlen(um.uniq_name) + 1) * sizeof(char));
                                   strcpy(prog_names_array[um.array_index],um.uniq_name);
                              } else {
                                   strcpy(prog_names_array[um.array_index],"");
                              }

                              if (wait(&st4tus) > 0) {
                                   if (WIFEXITED(st4tus)) {
                                        printf("[STATS-UNIQ] Filho saiu com o valor: %d\n", WEXITSTATUS(st4tus));
                                   } else {
                                        printf("[STATS-UNIQ] bad exit!\n");
                                   }
                              }
                         }

                         close(fdesc[0]);

                         // Eliminar nomes repetidos
                         int r, r2;
                         for (r = 0; r < u_pids-1; r++) {
                              for (r2 = r+1; r2 < u_pids; r2++) {
                                   if (strcmp(prog_names_array[r],prog_names_array[r2]) == 0) {
                                        strcpy(prog_names_array[r2],"");
                                   }
                              }
                         }

                         char send_array[u_pids][MAX_NAME_LENGTH]; // "static copy" of prog_names_array
                         for (r = 0; r < u_pids; r++) {
                              strcpy(send_array[r],prog_names_array[r]);
                         }

                         // abrir o pipe com nome do cliente para escrita
                         int client_fd;
                         if ((client_fd = open(uniq_name, O_WRONLY)) == -1) {
                              perror("Erro ao abrir o fifo \"próprio\" do cliente para escrita.");
                              exit(1);
                         }

                         if((write(client_fd, &send_array, sizeof(send_array))) == -1) {
                              perror("Erro ao escrever no fifo \"próprio\" do cliente o tempo total de execução dos programas.");
                              exit(1);
                         }

                         close(client_fd);

                         for (r = 0; r < u_pids; r++) {
                              free(prog_names_array[r]);
                         }

                         write(fifofdw,"T",1);
                         close(fifofdw);

                         _exit(0);

                    }

                    break;
               case 'T':
                    printf("Terminou a execução de um filho que estava a processar um pedido de stats-uniq.\n");
                    break;
               default:
                    printf("DEFAULT.\n");
                    break;
          }

     }

     close(fifofdr);
     close(fifofdw);
     unlink("fifo");

     return 0;
}
