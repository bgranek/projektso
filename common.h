#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>

#include "rejestr.h"

#define KOLOR_RESET    "\033[0m"
#define KOLOR_CZERWONY "\033[31m"
#define KOLOR_ZIELONY  "\033[32m"
#define KOLOR_ZOLTY    "\033[33m"
#define KOLOR_NIEBIESKI "\033[34m"
#define KOLOR_MAGENTA  "\033[35m"
#define KOLOR_CYAN     "\033[36m"
#define KOLOR_BOLD     "\033[1m"

#define ID_PROJEKTU 'H'
#define KLUCZ_SHM 100
#define KLUCZ_SEM 101
#define KLUCZ_MSG 102

#define POJEMNOSC_DOMYSLNA 1600
#define POJEMNOSC_MIN 80
#define POJEMNOSC_MAX 100000

#define LICZBA_SEKTOROW 8
#define SEKTOR_VIP 8
#define LICZBA_WSZYSTKICH_SEKTOROW 9

#define LICZBA_KAS 10
#define SZANSA_NA_PRZEDMIOT 5
#define MAX_BILETOW_NA_KIBICA 2

#define FIFO_PRACOWNIK_KIEROWNIK "/tmp/hala_fifo_ewakuacja"

#define SOCKET_MONITOR_PORT 9999

#define DRUZYNA_A 1
#define DRUZYNA_B 2

#define SZANSA_RODZINY 15
#define WIEK_DZIECKA_MAX 14
#define WIEK_DZIECKA_MIN 5
#define WIEK_RODZICA_MIN 25
#define WIEK_RODZICA_MAX 50

#define SYGNAL_BLOKADA_SEKTORA SIGRTMIN+1
#define SYGNAL_ODBLOKOWANIE_SEKTORA SIGRTMIN+2
#define SYGNAL_EWAKUACJA SIGRTMIN+3

#define TYP_KOMUNIKATU_ZAPYTANIE 1

#define CZAS_DO_MECZU_DOMYSLNY 30
#define CZAS_TRWANIA_MECZU_DOMYSLNY 60
#define CZAS_MIN 10
#define CZAS_MAX 3600

typedef enum {
    FAZA_PRZED_MECZEM = 0,
    FAZA_MECZ = 1,
    FAZA_PO_MECZU = 2
} FazaMeczu;

#define SPRAWDZ(x) \
    do { \
        if ((x) == -1) { \
            perror(#x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define WALIDUJ_ZAKRES(wartosc, min, max, nazwa) \
    do { \
        if ((wartosc) < (min) || (wartosc) > (max)) { \
            fprintf(stderr, "Blad: %s musi byc w zakresie %d-%d (podano: %d)\n", \
                    nazwa, min, max, wartosc); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

typedef struct {
    pid_t pid_kibica;
    int druzyna;
    int ma_przedmiot;
    int wiek;
    int zgoda_na_wejscie;
} MiejscaKolejki;

typedef struct {
    MiejscaKolejki miejsca[3];
    int obecna_druzyna;
    int liczba_oczekujacych;
    pthread_mutex_t mutex;
} Bramka;

#define MAX_RODZIN 50

typedef struct {
    pid_t pid_rodzica;
    pid_t pid_dziecka;
    int sektor;
    int rodzic_przy_bramce;
    int dziecko_przy_bramce;
    int aktywna;
} Rodzina;

typedef struct {
    Rodzina rodziny[MAX_RODZIN];
    int liczba_rodzin;
    pthread_mutex_t mutex;
} RejestrRodzin;

typedef struct {
    int pojemnosc_calkowita;
    int pojemnosc_sektora;
    int pojemnosc_vip;
    int limit_vip;

    int liczniki_sektorow[LICZBA_WSZYSTKICH_SEKTOROW];
    int osoby_w_sektorze[LICZBA_WSZYSTKICH_SEKTOROW];
    int sektor_zablokowany[LICZBA_SEKTOROW];
    int sektor_ewakuowany[LICZBA_SEKTOROW];

    int kasa_aktywna[LICZBA_KAS];
    int kolejka_dlugosc[LICZBA_KAS];
    int wszystkie_bilety_sprzedane;

    pid_t pidy_kasjerow[LICZBA_KAS];
    pid_t pidy_pracownikow[LICZBA_SEKTOROW];
    pid_t pid_kierownika;

    int suma_kibicow_w_hali;
    int liczba_vip;
    int ewakuacja_trwa;

    FazaMeczu faza_meczu;
    time_t czas_startu_symulacji;
    int czas_do_meczu;
    int czas_trwania_meczu;

    Bramka bramki[LICZBA_SEKTOROW][2];

    RejestrRodzin rejestr_rodzin;
} StanHali;

typedef struct {
    long mtype;
    pid_t pid_kibica;
    int id_druzyny;
    int czy_vip;
    int liczba_biletow;
    int nr_sektora;
} KomunikatBilet;

typedef struct {
    long mtype;
    int przydzielony_sektor;
    int liczba_sprzedanych;
    int czy_sukces;
} OdpowiedzBilet;

typedef struct {
    int typ;
    int nr_sektora;
    pid_t pid_pracownika;
    char wiadomosc[128];
} KomunikatFifo;

static inline int parsuj_int(const char *str, const char *nazwa, int min, int max) {
    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0) {
        perror("strtol");
        fprintf(stderr, "Blad parsowania %s\n", nazwa);
        exit(EXIT_FAILURE);
    }

    if (endptr == str || *endptr != '\0') {
        fprintf(stderr, "Blad: %s musi byc liczba calkowita (podano: '%s')\n", nazwa, str);
        exit(EXIT_FAILURE);
    }

    if (val < min || val > max) {
        fprintf(stderr, "Blad: %s musi byc w zakresie %d-%d (podano: %ld)\n", nazwa, min, max, val);
        exit(EXIT_FAILURE);
    }

    return (int)val;
}

static inline int bezpieczny_scanf_int(const char *prompt, int min, int max) {
    int wartosc;
    char bufor[256];

    while (1) {
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(bufor, sizeof(bufor), stdin) == NULL) {
            return -1;
        }

        bufor[strcspn(bufor, "\n")] = 0;

        if (strlen(bufor) == 0) {
            printf("Blad: Wprowadz liczbe.\n");
            continue;
        }

        char *endptr;
        errno = 0;
        long val = strtol(bufor, &endptr, 10);

        if (errno != 0 || *endptr != '\0') {
            printf("Blad: '%s' nie jest poprawna liczba.\n", bufor);
            continue;
        }

        if (val < min || val > max) {
            printf("Blad: Wartosc musi byc w zakresie %d-%d.\n", min, max);
            continue;
        }

        wartosc = (int)val;
        break;
    }

    return wartosc;
}

#endif