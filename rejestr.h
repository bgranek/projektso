/*
 * REJESTR.H - Prosty rejestr zdarzen do pliku symulacja.log
 *
 * Funkcje inline uzywane przez wszystkie procesy.
 */
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
#include <sys/file.h>

#define REJESTR_PLIK "symulacja.log" // Domyslny plik logu
#define REJESTR_BUFOR 512             // Rozmiar bufora wpisu

static int rejestr_fd = -1; // Wspolny deskryptor pliku logu
static pthread_mutex_t rejestr_mutex = PTHREAD_MUTEX_INITIALIZER; // Ochrona wpisow

/* Otwarcie rejestru (opcjonalnie z wyczyszczeniem) */
static inline int rejestr_init(const char *nazwa_pliku, int truncate) {
    if (nazwa_pliku == NULL) {
        nazwa_pliku = REJESTR_PLIK;
    }

    if (truncate) {
        // Tworzenie nowego pliku logu
        int tmp_fd = creat(nazwa_pliku, 0600);
        if (tmp_fd == -1) {
            perror("creat rejestr");
        } else {
            if (close(tmp_fd) == -1) {
                perror("close rejestr tmp");
            }
        }
    }

    rejestr_fd = open(nazwa_pliku, O_WRONLY | O_APPEND | O_CREAT, 0600);
    if (rejestr_fd == -1) {
        perror("open rejestr");
        return -1;
    }

    if (truncate) {
        // Naglowek nowej symulacji
        char naglowek[REJESTR_BUFOR];
        time_t teraz = time(NULL);
        struct tm *t = localtime(&teraz);

        int len = snprintf(naglowek, sizeof(naglowek),
            "\n========================================\n"
            "  ROZPOCZECIE SYMULACJI: %04d-%02d-%02d %02d:%02d:%02d\n"
            "========================================\n\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

        if (flock(rejestr_fd, LOCK_EX) == -1) {
            perror("flock lock rejestr");
        }
        if (write(rejestr_fd, naglowek, len) == -1) {
            perror("write rejestr");
        }
        if (flock(rejestr_fd, LOCK_UN) == -1) {
            perror("flock unlock rejestr");
        }
    }

    return 0;
}

/* Zamkniecie rejestru i zapis stopki */
static inline void rejestr_zamknij() {
    if (rejestr_fd != -1) {
        // Stopka z czasem zakonczenia
        char stopka[REJESTR_BUFOR];
        time_t teraz = time(NULL);
        struct tm *t = localtime(&teraz);
        
        int len = snprintf(stopka, sizeof(stopka),
            "\n========================================\n"
            "  ZAKONCZENIE SYMULACJI: %04d-%02d-%02d %02d:%02d:%02d\n"
            "========================================\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);

        if (flock(rejestr_fd, LOCK_EX) == -1) {
            perror("flock lock rejestr");
        }
        if (write(rejestr_fd, stopka, len) == -1) {
            perror("write rejestr");
        }
        if (flock(rejestr_fd, LOCK_UN) == -1) {
            perror("flock unlock rejestr");
        }
        if (close(rejestr_fd) == -1) {
            perror("close rejestr");
        }
        rejestr_fd = -1;
    }
}

/* Wpis zdarzenia do rejestru */
static inline void rejestr_log(const char *kategoria, const char *format, ...) {
    if (rejestr_fd == -1) return;
    
    // Mutex chroni przed jednoczesnym formatowaniem
    pthread_mutex_lock(&rejestr_mutex);
    if (flock(rejestr_fd, LOCK_EX) == -1) {
        perror("flock lock rejestr");
    }
    
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
    
    if (write(rejestr_fd, bufor, len) == -1) {
        perror("write rejestr");
    }

    if (flock(rejestr_fd, LOCK_UN) == -1) {
        perror("flock unlock rejestr");
    }
    
    pthread_mutex_unlock(&rejestr_mutex);
}

/* Wpis cyklicznych statystyk */
static inline void rejestr_statystyki(int pojemnosc, int kibicow, int vip, int limit_vip) {
    if (rejestr_fd == -1) return;
    
    // Osobny wpis statystyk
    pthread_mutex_lock(&rejestr_mutex);
    if (flock(rejestr_fd, LOCK_EX) == -1) {
        perror("flock lock rejestr");
    }
    
    char bufor[REJESTR_BUFOR];
    time_t teraz = time(NULL);
    struct tm *t = localtime(&teraz);
    
    int len = snprintf(bufor, sizeof(bufor),
        "[%02d:%02d:%02d] [STATYSTYKI] Pojemnosc: %d | Kibicow: %d | VIP: %d/%d\n",
        t->tm_hour, t->tm_min, t->tm_sec,
        pojemnosc, kibicow, vip, limit_vip);
    
    if (write(rejestr_fd, bufor, len) == -1) {
        perror("write rejestr");
    }

    if (flock(rejestr_fd, LOCK_UN) == -1) {
        perror("flock unlock rejestr");
    }
    
    pthread_mutex_unlock(&rejestr_mutex);
}

/* Wpis stanu pojedynczego sektora */
static inline void rejestr_sektor(int nr, int zajete, int pojemnosc, int zablokowany) {
    if (rejestr_fd == -1) return;
    
    // Wpis stanu sektora
    pthread_mutex_lock(&rejestr_mutex);
    if (flock(rejestr_fd, LOCK_EX) == -1) {
        perror("flock lock rejestr");
    }
    
    char bufor[REJESTR_BUFOR];
    time_t teraz = time(NULL);
    struct tm *t = localtime(&teraz);
    
    int len = snprintf(bufor, sizeof(bufor),
        "[%02d:%02d:%02d] [SEKTOR %d  ] Zajete: %d/%d | Status: %s\n",
        t->tm_hour, t->tm_min, t->tm_sec,
        nr, zajete, pojemnosc,
        zablokowany ? "ZABLOKOWANY" : "AKTYWNY");
    
    if (write(rejestr_fd, bufor, len) == -1) {
        perror("write rejestr");
    }

    if (flock(rejestr_fd, LOCK_UN) == -1) {
        perror("flock unlock rejestr");
    }
    
    pthread_mutex_unlock(&rejestr_mutex);
}

#endif