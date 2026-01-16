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
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <fcntl.h>

#define ID_PROJEKTU 'H'
#define KLUCZ_SHM 100
#define KLUCZ_SEM 101
#define KLUCZ_MSG 102

#define POJEMNOSC_CALKOWITA 1600
#define LICZBA_SEKTOROW 8
#define POJEMNOSC_SEKTORA (POJEMNOSC_CALKOWITA / LICZBA_SEKTOROW)
#define LICZBA_KAS 10
#define LIMIT_VIP 5
#define SZANSA_NA_PRZEDMIOT 1

#define DRUZYNA_A 1
#define DRUZYNA_B 2

#define SYGNAL_BLOKADA_SEKTORA SIGRTMIN+1
#define SYGNAL_ODBLOKOWANIE_SEKTORA SIGRTMIN+2
#define SYGNAL_EWAKUACJA SIGRTMIN+3

#define TYP_KOMUNIKATU_ZAPYTANIE 1

#define SPRAWDZ(x) \
    do { \
        if ((x) == -1) { \
            perror(#x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

typedef struct {
    pid_t pid_kibica;
    int druzyna;
    int zgoda_na_wejscie; 
} MiejscaKolejki;

typedef struct {
    MiejscaKolejki miejsca[3];
    int obecna_druzyna;
    int liczba_oczekujacych;
} Bramka;

typedef struct {
    int liczniki_sektorow[LICZBA_SEKTOROW];
    int sektor_zablokowany[LICZBA_SEKTOROW];
    int kasa_aktywna[LICZBA_KAS];
    int kolejka_dlugosc[LICZBA_KAS];
    
    pid_t pidy_kasjerow[LICZBA_KAS];
    pid_t pidy_pracownikow[LICZBA_SEKTOROW];
    pid_t pid_kierownika;
    
    int suma_kibicow_w_hali;
    int ewakuacja_trwa;

    Bramka bramki[LICZBA_SEKTOROW][2];
} StanHali;

typedef struct {
    long mtype;
    pid_t pid_kibica;
    int id_druzyny;
    int czy_vip;
    int nr_sektora; 
} KomunikatBilet;

typedef struct {
    long mtype;
    int przydzielony_sektor;
    int czy_sukces;
} OdpowiedzBilet;

#endif