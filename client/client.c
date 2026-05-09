

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT     8080
#define BUF_SIZE 2048

static const char *extraire(const char *buf) {
    const char *p = strchr(buf, '|');
    return p ? p + 1 : buf;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <SERVER_IP>\n", argv[0]);
        exit(1);
    }
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT)
    };
    if (inet_pton(AF_INET, argv[1], &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Adresse IP invalide: %s\n", argv[1]);
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    char buf[BUF_SIZE];
    int  n;

    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); exit(1); }
    buf[n] = '\0';
    printf("\n%s\n", extraire(buf));

    char nom[64];
    printf("Votre prénom: ");
    fflush(stdout);
    if (!fgets(nom, sizeof(nom), stdin)) { close(sock); exit(1); }
    send(sock, nom, strlen(nom), 0);

    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); exit(1); }
    buf[n] = '\0';
    printf("\n%s\n", extraire(buf));
    printf("(En attente du médecin…)\n");
    fflush(stdout);

    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); exit(1); }
    buf[n] = '\0';
    printf("\n%s\n", extraire(buf));

    char symptoms[BUF_SIZE];
    printf("Décrivez vos symptômes: ");
    fflush(stdout);
    if (!fgets(symptoms, sizeof(symptoms), stdin)) { close(sock); exit(1); }
    send(sock, symptoms, strlen(symptoms), 0);

    n = recv(sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(sock); exit(1); }
    buf[n] = '\0';
    printf("\n══════════════════════════════════════\n");
    printf("%s\n", extraire(buf));
    printf("══════════════════════════════════════\n\n");

    close(sock);
    return 0;
}
