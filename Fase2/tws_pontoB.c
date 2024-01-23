///////////  André Almeida,   nº 36761  ///////////
///////////  Bárbara Almeida, nº 41403  ///////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <wait.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>


#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define VERSION 1

struct {
    char *ext;
    char *filetype;
} extensions [] = {
        {"gif", "image/gif"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"ico", "image/ico"},
        {"zip", "image/zip"},
        {"gz", "image/gz"},
        {"tar", "image/tar"},
        {"htm", "text/html"},
        {"html", "text/html"},
        {0,0} };

int *buffer;
int buffer_size;
int buffer_start = 0;
int buffer_end = 0;
int buffer_count = 0;
pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buffer_not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t buffer_not_empty = PTHREAD_COND_INITIALIZER;

void logger(int type, char *s1, char *s2, int socket_fd) {
    int fd;
    char logbuffer[BUFSIZE * 2];

    switch (type) {
        case ERROR:
            (void) sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d", s1, s2, errno, getpid());
            break;
        case FORBIDDEN:
            (void) write(socket_fd,
                         "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",
                         271);
            (void) sprintf(logbuffer, "FORBIDDEN: %s:%s", s1, s2);
            break;
        case NOTFOUND:
            (void) write(socket_fd,
                         "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",
                         224);
            (void) sprintf(logbuffer, "NOT FOUND: %s:%s", s1, s2);
            break;
        case LOG:
            (void) sprintf(logbuffer, " INFO: %s:%s:%d", s1, s2, socket_fd);
            break;
    }

    if ((fd = open("tws.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0) {
        (void) write(fd, logbuffer, strlen(logbuffer));
        (void) write(fd, "\n", 1);
        (void) close(fd);
    }
}

int web(int fd, int hit) {
    int j, file_fd, buflen;
    long i, ret, len;
    char* fstr;
    static char buffer[BUFSIZE + 1];

    ret = read(fd, buffer, BUFSIZE);

    if (ret == 0 || ret == -1) {
        logger(FORBIDDEN, "failed to read browser request", "", fd);
        close(fd);
        return 1;
    }
    if (ret > 0 && ret < BUFSIZE)
        buffer[ret] = 0;
    else buffer[0] = 0;
    for (i = 0; i < ret; i++)
        if (buffer[i] == '\r' || buffer[i] == '\n')
            buffer[i] = '*';

    logger(LOG, "request", buffer, hit);

    if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4)) {
        logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
        close(fd);
        return 1;
    }

    for (i = 4; i < BUFSIZE; i++) {
        if (buffer[i] == ' ') {
            buffer[i] = 0;
            break;
        }
    }

    for (j = 0; j < i - 1; j++)
        if (buffer[j] == '.' && buffer[j + 1] == '.') {
            logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
            close(fd);
            return 1;
        }

    if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6))
        (void) strcpy(buffer, "GET /index.html");

    buflen = strlen(buffer);
    fstr = (char*) 0;

    for (i = 0; extensions[i].ext != 0; i++) {
        len = strlen(extensions[i].ext);
        if (!strncmp(&buffer[buflen - len], extensions[i].ext, len)) {
            fstr = extensions[i].filetype;
            break;
        }
    }

    if (fstr == 0)
        logger(FORBIDDEN, "file extension type not supported", buffer, fd);

    if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) {
        logger(NOTFOUND, "failed to open file", &buffer[5], fd);
    }
    else {
        logger(LOG, "SEND", &buffer[5], hit);
        (void) sprintf(buffer, "HTTP/1.1 200 OK\nContent-Type: %s\nConnection: close\n\n", fstr);
        (void) write(fd, buffer, strlen(buffer));

        while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) {
            (void) write(fd, buffer, ret);
        }
    }

    close(fd);
    return 1;
}


void *monitor(void *arg) {
    while (1) {
        pthread_mutex_lock(&buffer_mutex);
        printf("Buffer %d/%d\n", buffer_count, buffer_size);
        pthread_mutex_unlock(&buffer_mutex);
        sleep(1);
    }
    return NULL;
}

void *worker(void *arg) {
    while (1) {
        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == 0) {
            pthread_cond_wait(&buffer_not_empty, &buffer_mutex);
        }
        int socketfd = buffer[buffer_start];
        buffer_start = (buffer_start + 1) % buffer_size;
        buffer_count--;
        pthread_cond_signal(&buffer_not_full);
        pthread_mutex_unlock(&buffer_mutex);

        web(socketfd, socketfd);
    }
    return NULL;
}

int main(int argc, char **argv) {
    int i, port, listenfd, socketfd;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;

    pthread_mutex_init(&buffer_mutex, NULL);
    pthread_cond_init(&buffer_not_full, NULL);
    pthread_cond_init(&buffer_not_empty, NULL);

    if (argc < 3) {
        printf("Usage: %s <port> <buffer-size>\n", argv[0]);
        exit(0);
    }

    port = atoi(argv[1]);
    buffer_size = atoi(argv[2]);
    buffer = (int*) malloc(buffer_size * sizeof(int));

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) <0)
        logger(ERROR, "system call", "socket", 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <0)
        logger(ERROR, "system call", "bind", 0);

    if (listen(listenfd, 64) <0)
        logger(ERROR, "system call", "listen", 0);

    pthread_t thread_id;
    pthread_create(&thread_id, NULL, &monitor, NULL);

    for (i = 0; i < 64; i++) {
        pthread_create(&thread_id, NULL, &worker, NULL);
    }
    pthread_exit
    while (1) {
        int length = sizeof(cli_addr);
        socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, (socklen_t *)&length);
        if (socketfd < 0)
            logger(ERROR, "system call", "accept", 0);

        pthread_mutex_lock(&buffer_mutex);
        while (buffer_count == buffer_size) {
            pthread_cond_wait(&buffer_not_full, &buffer_mutex);
        }
        buffer[buffer_end] = socketfd;
        buffer_end = (buffer_end + 1) % buffer_size;
        buffer_count++;
        pthread_cond_signal(&buffer_not_empty);
        pthread_mutex_unlock(&buffer_mutex);
    }
}