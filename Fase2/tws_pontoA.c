///////////  André Almeida,   nº 36761  ///////////
///////////  Bárbara Almeida, nº 41403  ///////////

// Fase 2, ponto A

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <wait.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <pthread.h>


#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define VERSION 1
#define NUM_WORKERS 10

struct {
    char* ext;
    char* filetype;
} extensions[] = {
        {"gif", "image/gif"},
        {"jpg", "image/jpg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"ico", "image/ico"},
        {"zip", "image/zip"},
        {"gz", "image/gz"},
        {"tar", "image/tar"},
        {"htm", "text/html"},
        {"html", "text/html"},
        {0, 0}};

void logger(int type, char* s1, char* s2, int socket_fd) {
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

void* worker(void* arg) {
    int listenfd = *(int*)arg;
    while (1) {
        int socketfd = accept(listenfd, NULL, NULL);
        if (socketfd < 0)
            logger(ERROR, "system call", "accept", 0);
        else
            web(socketfd, i);
    }
    return NULL;
}

int main() {
    int i, port, pid, listenfd, socketfd;
    size_t length;
    static struct sockaddr_in cli_addr;
    static struct sockaddr_in serv_addr;

    if (fork() != 0)
        return 0;
    (void) signal(SIGCLD, SIG_IGN);
    (void) signal(SIGHUP, SIG_IGN);
    for (i = 0; i < 32; i++)
        (void) close(i);
    (void) setpgrp();

    logger(LOG, "tiny web server starting", argv[1], getpid());

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        logger(ERROR, "system call", "socket", 0);
    port = atoi(argv[1]);

    if (port < 0 || port > 60000)
        logger(ERROR, "Invalid port number try [1,60000]", argv[1], 0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
        logger(ERROR, "system call", "bind", 0);

    if (listen(listenfd, 64) < 0)
        logger(ERROR, "system call", "listen", 0);

    pthread_t threads[NUM_WORKERS];
    for (i = 0; i < NUM_WORKERS; i++) {
        if (pthread_create(&threads[i], NULL, worker, &listenfd) < 0)
            logger(ERROR, "system call", "pthread_create", 0);
    }

    for (i = 0; i < NUM_WORKERS; i++) {
        if (pthread_join(threads[i], NULL) < 0)
            logger(ERROR, "system call", "pthread_join", 0);
    }

    return 0;
}