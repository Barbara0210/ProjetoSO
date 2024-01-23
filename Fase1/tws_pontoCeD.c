///////////  André Almeida,   nº 36761  ///////////
///////////  Bárbara Almeida, nº 41403  ///////////

// Fase 1, pontos C e D

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/mman.h>

#define PORT 8080
#define IP "127.0.0.1"
#define BUFSIZE 8096
#define TIMER_START() gettimeofday(&tv1, NULL)
#define TIMER_STOP() \
gettimeofday(&tv2, NULL);    \
timersub(&tv2, &tv1, &tv);   \
time_delta = (float)tv.tv_sec + tv.tv_usec / 1000000.0

/*
float tempo_total = 0, tempo_medio = 0, tempo_minimo = 0, tempo_maximo = 0;
int num_pedido = -1;
struct timespec* start_time;
struct timespec* stop_time;
*/
struct SharedData {
    double tempo_total;
    double tempo_medio;
    double tempo_minimo;
    double tempo_maximo;
    int num_pedidos;
    struct timespec start_time[1];
    struct timespec stop_time[1];
};

struct SharedData* shared_data;

int pexit(char* msg) {
    perror(msg);
    exit(1);
}

pid_t** childPid;
int valor_N;
int numFilhos;

int fd_global;
int pai_global;


void sigintHandler(int sig_num) {
    printf("\nSinal de terminação recebido. A terminar processos filhos...\n");
    printf("-----------------------------------\n");
    close(fd_global);
    for (int j = 0; j < valor_N / numFilhos; j++) {
        for (int i = 0; i < numFilhos; i++) {
            if (childPid[j][i] == pai_global) {
            }
            kill(childPid[j][i], SIGTERM);      // Envia o sinal SIGTERM para o processo filho
        }
    }
    // Wait for all children to terminate
    for (int k = 0; k < valor_N; k++) {
        waitpid(-1, NULL, 0);
    }

    // Abre share_file.txt que contém as estatísticas recolhidas até ao momento
    FILE* file = fopen("shared_file.txt", "r");
    if (file == NULL) {
        perror("Erro a abrir ficheiro");
        exit(EXIT_FAILURE);
    }

    // Lê ficheiro e imprime para ecrã
    char ch;
    while ((ch = fgetc(file)) != EOF)
        putchar(ch);

    fclose(file);

    // calcular tempo médio
    shared_data->tempo_medio = shared_data->tempo_total / (float) shared_data->num_pedidos;

    char aEnviar[200];
    printf("-----------------------------------\n");
    sprintf(aEnviar, "tempo_total: %f\ntempo médio: %f\ntempo mínimo: %f\ntempo máximo: %f\nnúmero pedidos: %d\n",
            shared_data->tempo_total,
            shared_data->tempo_medio,
            shared_data->tempo_minimo, shared_data->tempo_maximo, shared_data->num_pedidos);
    write(STDOUT_FILENO, aEnviar, strlen(aEnviar));

    munmap(shared_data, sizeof(struct SharedData));
    exit(0);
}

int main(int argc, char* argv[]) {

    int i, sockfd;
    char buffer[BUFSIZE];
    static struct sockaddr_in serv_addr;
    struct timeval tv1, tv2, tv;
    float time_delta;


    if (argc != 3 && argc != 4) {
        printf("Usage: ./client <SERVER IP ADDRESS> <LISTENING PORT>\n");
        printf("Example: ./client 127.0.0.1 8141\n");
        exit(1);
    }
    if (argc == 3) {
        printf("client trying to connect to IP = %s PORT = %d\n", IP, PORT);
        sprintf(buffer, "GET /index.html HTTP/1.0 \r\n\r\n");
        /* Note: spaces are delimiters and VERY important */
    } else {
        printf("client trying to connect to IP = %s PORT = %d retrieving FILE= %s\n", IP, PORT, argv[3]);
        sprintf(buffer, "GET /%s HTTP/1.0 \r\n\r\n", argv[3]);
        /* Note: spaces are delimiters and VERY important */
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(IP);
    serv_addr.sin_port = htons(PORT);

    // atribui os argumentos recebidos às variáveis N e M
    int N = atoi(argv[1]);  // N -> nº de pedidos
    int M = atoi(argv[2]);  // M -> tamanho do batch
    struct timespec start_time_1[N * M + 20];
    struct timespec stop_time_1[N * M + 20];
    valor_N = N;
    numFilhos = M;

    /* Now the sockfd can be used to communicate to the server the GET request */
    printf("Send bytes=%d %s\n", (int) strlen(buffer), buffer);
    //write(sockfd, buffer, strlen(buffer));
    ///////////////////////////////////

    // Criar memória partilhada para a estrutura ShareData
    int shm_fd = shm_open("/shared_memory", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        pexit("shm_open() failed");
    }
    if (ftruncate(shm_fd, sizeof(struct SharedData)) == -1) {
        pexit("ftruncate() failed");
    }
    shared_data = (struct SharedData*) mmap(NULL, sizeof(struct SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        pexit("mmap() failed");
    }
    shared_data->tempo_total = 0;
    shared_data->tempo_medio = 0;
    shared_data->tempo_minimo = 0;
    shared_data->tempo_maximo = 0;
    shared_data->num_pedidos = 0;

    int fd;

    // abrir ficheiro em posix:
    fd = open("shared_file.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

    int r;
    int fds[M][2]; //array que vai guardar M pipes

    // criar pipes:
    for (i = 0; i < M; i++) {
        if (pipe(fds[i]) == -1) {
            printf("error a abrir pipe\n");
            return 1;
        }
    }

    int pai = getpid();
    pai_global = pai;
    fd_global = fd;

    // cria um array que vai guardar os pid's dos filhos N criados
    //pid_t childPid[N / M][M];

    // Alocar memória para as linhas (N/M * apontadores de tipo pid_t):
    pid_t** childPids = (pid_t**) malloc((N / M) * sizeof(pid_t*));

    // Alocar memória para cada linha (M colunas):
    for (i = 0; i < (N / M); i++) {
        childPids[i] = (pid_t*) calloc(M, sizeof(pid_t));
    }

    // aponta variável global para array 2D de pid's dos filhos
    childPid = childPids;

    // float aenviar[M][5];
    char aenviar[50];

    //for (int num_pedido = 0; num_pedido < N; num_pedido++) {
    for (i = 0; i < N / M; i++) {

        // percorre M childs e M pipes
        for (int j = 0; j < M; j++) {
            // criar pipe
            if (pipe(fds[j]) == -1) {
                printf("error a abrir pipe\n");
                return 1;
            }
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_IGN);
                close(fds[j][0]);
                //for (int k = 0; k < N / M; k++) {
                TIMER_START();
                if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                    pexit("socket() failed");

                /* Connect to the socket offered by the web server */
                if (connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
                    pexit("connect() failed");

                /* Now the sockfd can be used to communicate to the server the GET request */
                //////////////////////
                //printf("Send bytes=%d %s\n", (int) strlen(buffer), buffer);
                write(sockfd, buffer, strlen(buffer));

                int response_code = 0;
                int header_parsed = 0;
                while ((r = read(sockfd, buffer, BUFSIZE)) > 0) {
                    if (!header_parsed) {
                        sscanf(buffer, "HTTP/1.%*d %d", &response_code);
                        header_parsed = 1;
                    }
                    write(1, buffer, r);
                    //write(fds[1], aenviar, sizeof(aenviar));
                }
                TIMER_STOP();
                // calcular tempo total dos pedidos
                shared_data->tempo_total = shared_data->tempo_total + time_delta;

                shared_data->num_pedidos++;
                if (shared_data->num_pedidos == 1) {
                    shared_data->tempo_minimo = time_delta;
                    shared_data->tempo_maximo = time_delta;
                }
                // atribuir tempo mínimo e máximo:
                if (time_delta < shared_data->tempo_minimo)
                    shared_data->tempo_minimo = time_delta;
                else if (time_delta > shared_data->tempo_maximo)
                    shared_data->tempo_maximo = time_delta;

                //char aEnviar[50];
                //sprintf(aEnviar, "%d;%d;%d;%d;%f\n", getpid(), i + 1, j + 1, response_code, time_delta);
                //write(fd, aEnviar, strlen(aEnviar));
                ////////////////////////////////////////////////////////////////////
                sprintf(aenviar, "%d;%d;%d;%d;%f\n", getpid(), i + 1, j + 1, response_code, time_delta);
                write(fds[j][1], aenviar, sizeof(aenviar));
                if (i > 0) {
                    printf("i = %d | opaa\n", i);
                }
                close(sockfd);
                //}
                close(fds[j][1]);
                exit(0);
            } else if (pid < 0) {
                pexit("fork() failed");
                exit(1);
            } else {
                signal(SIGINT, sigintHandler);      // Instala o manipulador de sinal
                childPid[i][j] = pid;
            }
        }
        //close(fd);
        // Aguarda todos os filhos neste batch terminarem
        for (int k = 0; k < M; k++) {
            int status;
            pid_t pid = waitpid(childPid[i][k], &status, 0); // espera por cada filho do batch
            if (pid < 0) {
                pexit("waitpid() failed");
            } else if (WIFEXITED(status)) {
                int a = a;
                /*int exitStatus = WEXITSTATUS(status);
                printf("Processo filho terminou com o estado: %d\n", exitStatus);*/
            } else {
                printf("Processo filho terminou de maneira anormal.\n");
            }
        }
        // lê dados dos M pipes
        for (int j = 0; j < M; j++) {
            close(fds[j][1]);
            if (i >= 0)
                printf("i = %d, FUNCIONOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOU\n", i);
            while (read(fds[j][0], aenviar, 50) > 0) {
                //sprintf(aEnviar_resultado, "%d;%d;%d;%d;%f\n", getpid(), i + 1, j + 1, response_code, time_delta);
                printf("i = %d, FUNCIONOOOOOOOOOOOO222222222222222222222222222222222\n", i);
                puts(aenviar);
                write(fd_global, aenviar, strlen(aenviar));
            }
            close(fds[j][0]);
        }
    }
    close(fd_global);
    // abrir ficheiro em posix:
    fd = open("relatorio.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

    // calcular tempo médio
    shared_data->tempo_medio = shared_data->tempo_total / (float) shared_data->num_pedidos;

    char aEnviar[200];
    sprintf(aEnviar, "tempo_total: %f\ntempo médio: %f\ntempo mínimo: %f\ntempo máximo: %f\nnúmero pedidos: %d\n",
            shared_data->tempo_total,
            shared_data->tempo_medio,
            shared_data->tempo_minimo, shared_data->tempo_maximo, shared_data->num_pedidos);
    write(fd, aEnviar, strlen(aEnviar));
    printf("tempo total: %f\n", shared_data->tempo_total);

    //TIMER_STOP();
    //fprintf(stderr, "%f secs\n", time_delta);
    close(fd);
}
