
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#define BACKLOG 10
#define BUFSIZE 8192

const char *get_mime(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html")==0 || strcmp(ext,".htm")==0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".css")==0) return "text/css";
    if (strcmp(ext, ".js")==0) return "application/javascript";
    if (strcmp(ext, ".jpg")==0 || strcmp(ext,".jpeg")==0) return "image/jpeg";
    if (strcmp(ext, ".png")==0) return "image/png";
    if (strcmp(ext, ".pdf")==0) return "application/pdf";
    return "application/octet-stream";
}

void send_404(int fd) {
    const char *body="<html><body><h1>404 Not Found</h1></body></html>";
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.0 404 Not Found\r\nContent-Length: %zu\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n",
             strlen(body));
    write(fd,hdr,strlen(hdr));
    write(fd,body,strlen(body));
}

void send_file(int fd, const char *path) {
    int f = open(path, O_RDONLY);
    if (f < 0) { send_404(fd); return; }
    struct stat st;
    fstat(f, &st);
    off_t remaining = st.st_size;
    const char *mime = get_mime(path);
    char hdr[512];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.0 200 OK\r\nContent-Length: %lld\r\nContent-Type: %s\r\nConnection: close\r\n\r\n",
             (long long)remaining, mime);
    write(fd, hdr, strlen(hdr));
    char buf[BUFSIZE];
    ssize_t r;
    while ((r = read(f, buf, sizeof(buf))) > 0) {
        write(fd, buf, r);
    }
    close(f);
}

void send_dir_listing(int fd, const char *dirpath, const char *reqpath) {
    DIR *d = opendir(dirpath);
    if (!d) { send_404(fd); return; }
    size_t buflen = 1024;
    char *body = malloc(buflen);
    snprintf(body, buflen, "<html><head><meta charset='utf-8'><title>Index of %s</title></head><body><h1>Index of %s</h1><ul>", reqpath, reqpath);
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0) continue;
        char entry[PATH_MAX];
        snprintf(entry, sizeof(entry), "<li><a href=\"%s/%s\">%s</a></li>", reqpath, ent->d_name, ent->d_name);
        size_t needed = strlen(body) + strlen(entry) + 1;
        if (needed > buflen) {
            buflen = needed + 1024;
            body = realloc(body, buflen);
        }
        strcat(body, entry);
    }
    closedir(d);
    size_t needed = strlen(body) + 64;
    body = realloc(body, needed);
    strcat(body, "</ul></body></html>");
    char hdr[256];
    snprintf(hdr, sizeof(hdr),
             "HTTP/1.0 200 OK\r\nContent-Length: %zu\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n\r\n",
             strlen(body));
    write(fd, hdr, strlen(hdr));
    write(fd, body, strlen(body));
    free(body);
}

int starts_with(const char *s, const char *p) { return strncmp(s,p,strlen(p))==0; }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,"Uso: %s /diretorio/raiz [port]\n", argv[0]);
        return 1;
    }
    char *root = realpath(argv[1], NULL);
    if (!root) { perror("realpath"); return 1; }
    const char *port = (argc >=3) ? argv[2] : "5050";

    struct addrinfo hints, *res;
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res)!=0) { perror("getaddrinfo"); return 1; }
    int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    int yes = 1; setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (bind(sock, res->ai_addr, res->ai_addrlen) < 0) { perror("bind"); return 1; }
    freeaddrinfo(res);
    listen(sock, BACKLOG);
    printf("Servidor rodando na porta %s, servindo %s\n", port, root);

    while (1) {
        struct sockaddr_storage cli;
        socklen_t clilen = sizeof(cli);
        int fd = accept(sock, (struct sockaddr*)&cli, &clilen);
        if (fd < 0) { perror("accept"); continue; }
        char buf[BUFSIZE];
        ssize_t n = read(fd, buf, sizeof(buf)-1);
        if (n <= 0) { close(fd); continue; }
        buf[n]=0;
        char method[16], reqpath[1024];
        if (sscanf(buf, "%15s %1023s", method, reqpath) != 2) { close(fd); continue; }
        if (strcmp(method,"GET") != 0) { send_404(fd); close(fd); continue; }

        char safe[PATH_MAX];
        snprintf(safe, sizeof(safe), "%s", reqpath);
        char *p = safe;
        if (p[0] == '/') p++;
        if (strstr(p, "..")) { send_404(fd); close(fd); continue; }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", root, p);
        char *real = realpath(full, NULL);
        if (!real) { send_404(fd); close(fd); continue; }
        if (!starts_with(real, root)) { free(real); send_404(fd); close(fd); continue; }

        struct stat st;
        if (stat(real, &st) < 0) { free(real); send_404(fd); close(fd); continue; }
        if (S_ISDIR(st.st_mode)) {
            char indexp[PATH_MAX];
            snprintf(indexp, sizeof(indexp), "%s/index.html", real);
            if (stat(indexp, &st) == 0) {
                send_file(fd, indexp);
            } else {
                send_dir_listing(fd, real, reqpath);
            }
        } else if (S_ISREG(st.st_mode)) {
            send_file(fd, real);
        } else {
            send_404(fd);
        }
        free(real);
        close(fd);
    }

    free(root);
    close(sock);
    return 0;
}
