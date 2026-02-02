/*
 * COMMON.H - Wspolne definicje dla wszystkich modulow symulatora
 *
 * Zawiera:
 * - Stale konfiguracyjne hali
 * - Definicje semaforow (146 semaforow)
 * - Struktury danych IPC
 * - Makra pomocnicze
 */

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

/* === KOLORY TERMINALA (ANSI escape codes) === */
#define KOLOR_RESET    "\033[0m"
#define KOLOR_CZERWONY "\033[31m"
#define KOLOR_ZIELONY  "\033[32m"
#define KOLOR_ZOLTY    "\033[33m"
#define KOLOR_NIEBIESKI "\033[34m"
#define KOLOR_MAGENTA  "\033[35m"
#define KOLOR_CYAN     "\033[36m"
#define KOLOR_BOLD     "\033[1m"

/* === KLUCZE IPC === */
#define ID_PROJEKTU 'H'
#define KLUCZ_SHM 100   // Klucz pamieci dzielonej
#define KLUCZ_SEM 101   // Klucz semaforow
#define KLUCZ_MSG 102   // Klucz kolejki komunikatow

/* === PARAMETRY HALI === */
#define POJEMNOSC_DOMYSLNA 1600 // Domyslna pojemnosc hali
#define POJEMNOSC_MIN 80         // Minimalna dozwolona pojemnosc
#define POJEMNOSC_MAX 100000     // Maksymalna dozwolona pojemnosc

#define LICZBA_SEKTOROW 8              // Sektory zwykle (0-7)
#define SEKTOR_VIP 8                   // Indeks sektora VIP
#define LICZBA_WSZYSTKICH_SEKTOROW 9   // 8 zwyklych + 1 VIP

#define LICZBA_KAS 10                  // Ilosc kas biletowych
#define SZANSA_NA_PRZEDMIOT 5          // % szansy na noz (5%)
#define MAX_BILETOW_NA_KIBICA 2        // Max biletow na osobe

/* === FIFO === */
#define FIFO_PRACOWNIK_KIEROWNIK "/tmp/hala_fifo_ewakuacja"

/* === SOCKET === */
#define SOCKET_MONITOR_PORT 9999  // Port TCP dla monitora

/*
 * === SEMAFORY (146 semaforow) ===
 *
 * Organizacja:
 * [0]       - Mutex glowny
 * [1]       - Mutex pomocniczy
 * [2-49]    - SEM_SLOT: 8 sektorow * 2 stanowiska * 3 miejsca = 48
 * [50-57]   - SEM_SEKTOR: blokada wejscia do sektora
 * [58-107]  - SEM_RODZINA: synchronizacja rodzic-dziecko (50 rodzin)
 * [108]     - SEM_FAZA_MECZU: kibice czekaja na koniec meczu
 * [109-116] - SEM_PRACA: pracownik czeka na kibica
 * [117-132] - SEM_BRAMKA: synchronizacja zajmowania stanowisk
 * [133-142] - SEM_KASA: kasjer czeka na aktywacje
 * [143]     - SEM_KIBIC_WYSZEDL: notyfikacja o wyjsciu kibica
 * [144]     - SEM_EWAKUACJA_KONIEC: notyfikacja o koncu ewakuacji
 * [145]     - SEM_START_MECZU: kibice czekaja na start (0 = mecz trwa)
 */
#define SEM_SLOT_BASE 2
#define SEM_SEKTOR_BASE 50
#define SEM_RODZINA_BASE 58
#define SEM_FAZA_MECZU 108
#define SEM_PRACA_BASE 109
#define SEM_BRAMKA_BASE 117
#define SEM_KASA_BASE 133
#define SEM_KIBIC_WYSZEDL 143
#define SEM_EWAKUACJA_KONIEC 144
#define SEM_START_MECZU 145
#define SEM_TOTAL 146

/* Makra obliczajace indeks semafora */
#define SEM_SLOT(s,st,m) (SEM_SLOT_BASE + (s)*6 + (st)*3 + (m))  // sektor, stanowisko, miejsce
#define SEM_SEKTOR(s) (SEM_SEKTOR_BASE + (s))                     // sektor
#define SEM_RODZINA(id) (SEM_RODZINA_BASE + (id))                 // id rodziny
#define SEM_PRACA(s) (SEM_PRACA_BASE + (s))                       // sektor
#define SEM_BRAMKA(s,st) (SEM_BRAMKA_BASE + (s)*2 + (st))         // sektor, stanowisko
#define SEM_KASA(id) (SEM_KASA_BASE + (id))                       // id kasy

/* === DRUZYNY === */
#define DRUZYNA_A 1  // Druzyna gospodarzy
#define DRUZYNA_B 2  // Druzyna gosci

/* === RODZINY === */
#define SZANSA_RODZINY 15      // % szansy ze kibic jest rodzicem
#define WIEK_DZIECKA_MAX 14    // Dzieci <15 lat
#define WIEK_DZIECKA_MIN 5
#define WIEK_RODZICA_MIN 25
#define WIEK_RODZICA_MAX 50

/* === SYGNALY OD KIEROWNIKA === */
#define SYGNAL_BLOKADA_SEKTORA SIGRTMIN+1      // Zablokuj wejscie do sektora
#define SYGNAL_ODBLOKOWANIE_SEKTORA SIGRTMIN+2 // Odblokuj wejscie
#define SYGNAL_EWAKUACJA SIGRTMIN+3            // Ewakuacja calej hali

/* === KOLEJKI KOMUNIKATOW === */
#define TYP_KOMUNIKATU_ZAPYTANIE 1  // mtype dla zapytan o bilet

/* === CZASY === */
#define CZAS_DO_MECZU_DOMYSLNY 30      // Sekundy do startu meczu
#define CZAS_TRWANIA_MECZU_DOMYSLNY 60 // Czas trwania meczu
#define CZAS_MIN 10                    // Minimalny czas w sekundach
#define CZAS_MAX 3600                  // Maksymalny czas w sekundach

/* Fazy meczu - kontroluja zachowanie kibicow */
typedef enum {
    FAZA_PRZED_MECZEM = 0,  // Kibice wchodza, czekaja
    FAZA_MECZ = 1,          // Mecz trwa
    FAZA_PO_MECZU = 2       // Kibice wychodza
} FazaMeczu;

/*
 * Makro SPRAWDZ - sprawdza czy funkcja zwrocila -1 (blad).
 * Jesli tak, wypisuje blad i konczy program.
 */
#define SPRAWDZ(x) \
    do { \
        if ((x) == -1) { \
            perror(#x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* Makro walidacji zakresu wartosci */
#define WALIDUJ_ZAKRES(wartosc, min, max, nazwa) \
    do { \
        if ((wartosc) < (min) || (wartosc) > (max)) { \
            fprintf(stderr, "Blad: %s musi byc w zakresie %d-%d (podano: %d)\n", \
                    nazwa, min, max, wartosc); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/*
 * Struktura opisujaca kibica na stanowisku kontroli.
 * Max 3 kibicow na stanowisku jednoczesnie.
 */
typedef struct {
    pid_t pid_kibica;      // PID kibica (0 = miejsce wolne)
    int druzyna;           // DRUZYNA_A lub DRUZYNA_B
    int ma_przedmiot;      // 1 = ma noz
    int wiek;              // Wiek kibica
    int zgoda_na_wejscie;  // 1 = kontrola zakonczona pozytywnie
} MiejscaKolejki;

/*
 * Bramka kontroli bezpieczenstwa.
 * Kazdy sektor ma 2 bramki, kazda obsluguje max 3 kibicow.
 */
typedef struct {
    MiejscaKolejki miejsca[3];   // 3 miejsca na stanowisku
    int obecna_druzyna;          // Druzyna aktualnie kontrolowana (0 = brak)
    int liczba_oczekujacych;     // Ile osob czeka na kontrole
    pid_t pid_agresora;          // PID agresywnego kibica (jesli byl)
    pthread_mutex_t mutex;       // Mutex do synchronizacji dostepu
} Bramka;

#define MAX_RODZIN 50  // Max liczba rodzin jednoczesnie

/*
 * Struktura rodziny (rodzic + dziecko).
 * Dzieci <15 lat musza wchodzic z opiekunem.
 */
typedef struct {
    pid_t pid_rodzica;       // PID procesu rodzica
    pid_t pid_dziecka;       // PID procesu dziecka
    int sektor;              // Przydzielony sektor
    int rodzic_przy_bramce;  // 1 = rodzic dotarl do bramki
    int dziecko_przy_bramce; // 1 = dziecko dotarlo do bramki
    int aktywna;             // 1 = rodzina aktywna
} Rodzina;

/* Rejestr wszystkich rodzin w pamieci dzielonej */
typedef struct {
    Rodzina rodziny[MAX_RODZIN];
    int liczba_rodzin;      // Aktualna liczba aktywnych rodzin
    pthread_mutex_t mutex;  // Ochrona rejestru rodzin
} RejestrRodzin;

/*
 * STANHALI - Glowna struktura w pamieci dzielonej.
 * Wspoldzielona przez wszystkie procesy symulacji.
 */
typedef struct {
    // Parametry pojemnosci
    int pojemnosc_calkowita;    // K - calkowita pojemnosc
    int pojemnosc_sektora;      // K/8
    int pojemnosc_vip;          // <0.3% * K
    int limit_vip;

    // Stan sektorow
    int liczniki_sektorow[LICZBA_WSZYSTKICH_SEKTOROW];  // Sprzedane bilety
    int osoby_w_sektorze[LICZBA_WSZYSTKICH_SEKTOROW];   // Aktualna liczba osob
    int sektor_zablokowany[LICZBA_SEKTOROW];            // Flagi blokady
    int sektor_ewakuowany[LICZBA_SEKTOROW];             // Flagi ewakuacji

    // Stan kas
    int kasa_aktywna[LICZBA_KAS];       // 1 = kasa aktywna
    int kasa_zamykanie[LICZBA_KAS];     // 1 = kasa w trakcie zamykania
    int kolejka_dlugosc[LICZBA_KAS];    // Dlugosc kolejki do kasy
    int wszystkie_bilety_sprzedane;     // 1 = wyprzedane

    // PID-y procesow
    pid_t pidy_kasjerow[LICZBA_KAS];
    pid_t pidy_pracownikow[LICZBA_SEKTOROW];
    pid_t pid_kierownika;
    pid_t pid_main;

    // Statystyki
    int suma_kibicow_w_hali;    // Aktualna liczba osob w hali
    int liczba_vip;             // Aktualna liczba VIP
    int ewakuacja_trwa;         // 1 = trwa ewakuacja
    int aktywne_kasy;           // Liczba aktywnych kas

    // Czas i faza
    FazaMeczu faza_meczu;
    time_t czas_startu_symulacji;
    int czas_do_meczu;
    int czas_trwania_meczu;

    // Bramki kontroli: 8 sektorow x 2 stanowiska
    Bramka bramki[LICZBA_SEKTOROW][2];

    // Rejestr rodzin
    RejestrRodzin rejestr_rodzin;
} StanHali;

/*
 * Komunikat wysylany przez kibica do kasjera (kolejka komunikatow).
 * mtype = TYP_KOMUNIKATU_ZAPYTANIE (1)
 */
typedef struct {
    long mtype;           // Typ komunikatu (1 = zapytanie)
    pid_t pid_kibica;     // PID kibica (do odpowiedzi)
    int id_druzyny;       // DRUZYNA_A lub DRUZYNA_B
    int czy_vip;          // 1 = kibic VIP
    int liczba_biletow;   // 1 lub 2 bilety
    int nr_sektora;       // -1 = dowolny, 0-7 = konkretny
    int nr_kasy;          // Numer wybranej kasy
} KomunikatBilet;

/*
 * Odpowiedz od kasjera do kibica.
 * mtype = PID kibica (zeby odebrac wlasciwa odpowiedz)
 */
typedef struct {
    long mtype;              // PID kibica
    int przydzielony_sektor; // Numer sektora
    int liczba_sprzedanych;  // Ile biletow sprzedano
    int czy_sukces;          // 1 = sukces, 0 = brak miejsc
} OdpowiedzBilet;

/*
 * Komunikat wysylany przez FIFO (pracownik -> kierownik).
 * Uzywany do zgloszen o zakonczeniu ewakuacji.
 */
typedef struct {
    int typ;                  // Typ komunikatu (1 = ewakuacja zakonczona)
    int nr_sektora;           // Numer sektora
    pid_t pid_pracownika;     // PID pracownika
    char wiadomosc[128];      // Opis tekstowy
} KomunikatFifo;

/* Parsowanie liczby z walidacja zakresu */
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

/* Bezpieczny odczyt liczby z stdin */
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

/* semop z ponawianiem po EINTR */
static inline int semop_retry_ctx(int semid, struct sembuf *ops, size_t nops, const char *ctx) {
    while (semop(semid, ops, nops) == -1) {
        if (errno == EINTR) continue;
        perror(ctx);
        return -1;
    }
    return 0;
}

#endif