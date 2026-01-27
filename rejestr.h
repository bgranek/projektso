#ifndef REJESTR_H
#define REJESTR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <pthread.h>

#define REJESTR_PLIK "symulacja.log"
#define REJESTR_BUFOR 512

static int rejestr_fd = -1;
static pthread_mutex_t rejestr_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline int rejestr_init(const char *nazwa_pliku, int truncate) {
    if (nazwa_pliku == NULL) {
        nazwa_pliku = REJESTR_PLIK;
    }

    if (truncate) {
        int tmp_fd = creat(nazwa_pliku, 0644);
        if (tmp_fd != -1) {
            close(tmp_fd);
        }
    }

    rejestr_fd = open(nazwa_pliku, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (rejestr_fd == -1) {
        perror("open rejestr");
        return -1;
    }

    if (truncate) {
        char naglowek[REJESTR_BUFOR];
        time_t teraz = time(NULL);
        struct tm *t = localtime(&teraz);

        int len = snprintf(naglowek, sizeof(naglowek),
            "\n========================================\n"
            "  ROZPOCZECIE SYMULACJI: %04d-%02d-%02d %02d:%02d:%02d\n"
            "========================================\n\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

        write(rejestr_fd, naglowek, len);
    }

    return 0;
}

static inline void rejestr_zamknij() {
    if (rejestr_fd != -1) {
        char stopka[REJESTR_BUFOR];
        time_t teraz = time(NULL);
        struct tm *t = localtime(&teraz);
        
        int len = snprintf(stopka, sizeof(stopka),
            "\n========================================\n"
            "  ZAKONCZENIE SYMULACJI: %04d-%02d-%02d %02d:%02d:%02d\n"
            "========================================\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
        
        write(rejestr_fd, stopka, len);
        close(rejestr_fd);
        rejestr_fd = -1;
    }
}

static inline void rejestr_log(const char *kategoria, const char *format, ...) {
    if (rejestr_fd == -1) return;
    
    pthread_mutex_lock(&rejestr_mutex);
    
    char bufor[REJESTR_BUFOR];
    char wiadomosc[REJESTR_BUFOR];
    char znacznik[64];
    
    time_t teraz = time(NULL);
    struct tm *t = localtime(&teraz);
    
    snprintf(znacznik, sizeof(znacznik), "[%02d:%02d:%02d]",
             t->tm_hour, t->tm_min, t->tm_sec);
    
    va_list args;
    va_start(args, format);
    vsnprintf(wiadomosc, sizeof(wiadomosc), format, args);
    va_end(args);
    
    int len = snprintf(bufor, sizeof(bufor), "%s [%-10s] [PID:%-6d] %s\n",
                       znacznik, kategoria, getpid(), wiadomosc);
    
    write(rejestr_fd, bufor, len);
    
    pthread_mutex_unlock(&rejestr_mutex);
}

static inline void rejestr_statystyki(int pojemnosc, int kibicow, int vip, int limit_vip) {
    if (rejestr_fd == -1) return;
    
    pthread_mutex_lock(&rejestr_mutex);
    
    char bufor[REJESTR_BUFOR];
    time_t teraz = time(NULL);
    struct tm *t = localtime(&teraz);
    
    int len = snprintf(bufor, sizeof(bufor),
        "[%02d:%02d:%02d] [STATYSTYKI] Pojemnosc: %d | Kibicow: %d | VIP: %d/%d\n",
        t->tm_hour, t->tm_min, t->tm_sec,
        pojemnosc, kibicow, vip, limit_vip);
    
    write(rejestr_fd, bufor, len);
    
    pthread_mutex_unlock(&rejestr_mutex);
}

static inline void rejestr_sektor(int nr, int zajete, int pojemnosc, int zablokowany) {
    if (rejestr_fd == -1) return;
    
    pthread_mutex_lock(&rejestr_mutex);
    
    char bufor[REJESTR_BUFOR];
    time_t teraz = time(NULL);
    struct tm *t = localtime(&teraz);
    
    int len = snprintf(bufor, sizeof(bufor),
        "[%02d:%02d:%02d] [SEKTOR %d  ] Zajete: %d/%d | Status: %s\n",
        t->tm_hour, t->tm_min, t->tm_sec,
        nr, zajete, pojemnosc,
        zablokowany ? "ZABLOKOWANY" : "AKTYWNY");
    
    write(rejestr_fd, bufor, len);
    
    pthread_mutex_unlock(&rejestr_mutex);
}

#endif