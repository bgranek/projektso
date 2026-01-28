#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SOCKET_MONITOR_PORT 9999
#define BUFOR_SIZE 1024

int main(int argc, char *argv[]) {
    int interwal = 2;
    char *host = "127.0.0.1";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            interwal = atoi(argv[++i]);
            if (interwal < 1) interwal = 1;
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            host = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("Uzycie: %s [-i interwal] [-s serwer]\n", argv[0]);
            printf("  -i interwal  Odswiezanie co N sekund (domyslnie: 2)\n");
            printf("  -s serwer    Adres serwera (domyslnie: 127.0.0.1)\n");
            printf("  -h           Wyswietl pomoc\n");
            return 0;
        }
    }

    printf("\033[2J");
    printf("Monitor hali - laczenie z %s:%d (Ctrl+C aby zakonczyc)\n\n", host, SOCKET_MONITOR_PORT);

    while (1) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
            perror("socket");
            sleep(interwal);
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(SOCKET_MONITOR_PORT);

        if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
            fprintf(stderr, "Nieprawidlowy adres: %s\n", host);
            close(sock);
            return 1;
        }

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            printf("\r\033[K[OFFLINE] Brak polaczenia z hala...");
            fflush(stdout);
            close(sock);
            sleep(interwal);
            continue;
        }

        char bufor[BUFOR_SIZE];
        memset(bufor, 0, sizeof(bufor));
        int n = recv(sock, bufor, sizeof(bufor) - 1, 0);
        close(sock);

        if (n <= 0) {
            sleep(interwal);
            continue;
        }

        printf("\033[H\033[2J");
        printf("+==========================================+\n");
        printf("|         MONITOR HALI (SOCKET)            |\n");
        printf("+==========================================+\n\n");

        char *token = strtok(bufor, "|");
        while (token != NULL) {
            if (strncmp(token, "HALA", 4) == 0) {
                printf("Status: ONLINE\n\n");
            } else if (strncmp(token, "OSOB_W_HALI:", 12) == 0) {
                printf("Osob w hali: %s\n", token + 12);
            } else if (strncmp(token, "SUMA_BILETOW:", 13) == 0) {
                printf("Biletow sprzedanych: %s\n", token + 13);
            } else if (strncmp(token, "VIP:", 4) == 0) {
                printf("VIP: %s\n", token + 4);
            } else if (strncmp(token, "POJEMNOSC:", 10) == 0) {
                printf("Pojemnosc: %s\n", token + 10);
            } else if (strncmp(token, "BILETY:", 7) == 0) {
                printf("Bilety: %s\n", token + 7);
            } else if (strncmp(token, "EWAKUACJA:", 10) == 0) {
                char *val = token + 10;
                if (strcmp(val, "TAK") == 0) {
                    printf("\033[31mEWAKUACJA W TOKU!\033[0m\n");
                } else {
                    printf("Ewakuacja: NIE\n");
                }
            } else if (strncmp(token, "FAZA:", 5) == 0) {
                int faza = atoi(token + 5);
                const char *fazy[] = {"PRZED MECZEM", "MECZ TRWA", "PO MECZU"};
                printf("Faza: %s\n", fazy[faza]);
            } else if (token[0] == 'S' && token[1] >= '0' && token[1] <= '7') {
                if (token[1] == '0') printf("\nSektory (bilety/pojemnosc | osoby w srodku):\n");
                char *dane = strchr(token, ':');
                if (dane) {
                    int nr = token[1] - '0';
                    char *blok = strstr(token, "[B]");
                    char *osob = strstr(token, "(osob:");
                    int osoby = 0;
                    if (osob) osoby = atoi(osob + 6);
                    char bilety_info[32];
                    strncpy(bilety_info, dane + 1, sizeof(bilety_info) - 1);
                    char *paren = strchr(bilety_info, '(');
                    if (paren) *paren = '\0';
                    printf("  [%d] bilety: %s | osoby: %d %s\n", nr, bilety_info, osoby,
                           blok ? "\033[33m(ZABLOKOWANY)\033[0m" : "");
                }
            }
            token = strtok(NULL, "|");
        }

        printf("\n------------------------------------------\n");
        printf("Odswiezanie co %d sek. | Ctrl+C = wyjscie\n", interwal);

        sleep(interwal);
    }

    return 0;
}
