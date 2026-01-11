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
    int liczniki_sektorow[LICZBA_SEKTOROW];
    int sektor_zablokowany[LICZBA_SEKTOROW];
    int kasa_aktywna[LICZBA_KAS];
    int kolejka_dlugosc[LICZBA_KAS];
    int suma_kibicow_w_hali;
    int ewakuacja_trwa;
    
    struct {
        int liczba_osob;
        int obecna_druzyna;
        pid_t pid_obslugiwanego;
        int czy_ma_przedmiot;
        int czy_agresywny;
    } bramki[LICZBA_SEKTOROW][2];

    pid_t pidy_pracownikow[LICZBA_SEKTOROW];
    pid_t pidy_kasjerow[LICZBA_KAS];
    pid_t pid_kierownika;
} StanHali;

typedef struct {
    long mtype;
    pid_t pid_kibica;
    int id_druzyny;
    int nr_sektora;
    int czy_vip;
} KomunikatBilet;

typedef struct {
    long mtype;
    int czy_sukces;
    int przydzielony_sektor;
} OdpowiedzBilet;

#endif