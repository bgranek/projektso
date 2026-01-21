#include "common.h"

int id_sektora = -1;
int shm_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;

void obsluga_wyjscia() {
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->pidy_pracownikow[id_sektora] = 0;
        shmdt(stan_hali);
    }
}

void handler_blokada(int sig) {
    (void)sig;
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->sektor_zablokowany[id_sektora] = 1;
        const char *msg = "[BLOKADA] SEKTOR ZABLOKOWANY\n";
        if (write(STDOUT_FILENO, msg, strlen(msg)) == -1) {}
    }
}

void handler_odblokowanie(int sig) {
    (void)sig;
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->sektor_zablokowany[id_sektora] = 0;
        const char *msg = "[ODBLOKOWANIE] SEKTOR ODBLOKOWANY\n";
        if (write(STDOUT_FILENO, msg, strlen(msg)) == -1) {}
    }
}

void handler_ewakuacja(int sig) {
    (void)sig;
    const char *msg = "[EWAKUACJA] Otwieram bramki awaryjne.\n";
    if (write(STDOUT_FILENO, msg, strlen(msg)) == -1) {}
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);
    key_t klucz_sem = ftok(".", KLUCZ_SEM);
    SPRAWDZ(klucz_sem);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);
    
    sem_id = semget(klucz_sem, 2, 0600);
    SPRAWDZ(sem_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

void rejestruj_sygnaly() {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = handler_blokada;
    sigaction(SYGNAL_BLOKADA_SEKTORA, &sa, NULL);

    sa.sa_handler = handler_odblokowanie;
    sigaction(SYGNAL_ODBLOKOWANIE_SEKTORA, &sa, NULL);

    sa.sa_handler = handler_ewakuacja;
    sigaction(SYGNAL_EWAKUACJA, &sa, NULL);
}

void obsluguj_bramki() {
    if (stan_hali->ewakuacja_trwa) {
        struct sembuf operacje[1];
        operacje[0].sem_num = 0; operacje[0].sem_op = -1; operacje[0].sem_flg = 0;
        semop(sem_id, operacje, 1);
        
        int liczba_osob = stan_hali->liczniki_sektorow[id_sektora];
        
        operacje[0].sem_op = 1;
        semop(sem_id, operacje, 1);

        if (liczba_osob == 0) {
            printf("Pracownik %d: Sektor ewakuowany. Zglaszam do kierownika.\n", id_sektora);
            usleep(1000000); 
        }
        return;
    }
    
    if (stan_hali->sektor_zablokowany[id_sektora]) return;

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    if (semop(sem_id, operacje, 1) == -1) return;

    for (int nr_stanowiska = 0; nr_stanowiska < 2; nr_stanowiska++) {
        Bramka *b = &stan_hali->bramki[id_sektora][nr_stanowiska];

        int liczba_zajetych = 0;
        for (int i = 0; i < 3; i++) {
            if (b->miejsca[i].pid_kibica != 0) {
                liczba_zajetych++;
            }
        }

        if (liczba_zajetych == 0) {
            b->obecna_druzyna = 0;
        }

        for (int i = 0; i < 3; i++) {
            if (b->miejsca[i].pid_kibica != 0 && b->miejsca[i].zgoda_na_wejscie == 0) {
                if (b->miejsca[i].ma_przedmiot) {
                    printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Posiada noz!%s\n", 
                           KOLOR_CZERWONY, id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, KOLOR_RESET);
                    b->miejsca[i].zgoda_na_wejscie = 2; 
                    continue;
                }
                
                if (b->miejsca[i].wiek < 15) {
                    if ((rand() % 100) < 10) {
                         printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Wiek %d < 15 bez opiekuna%s\n", 
                           KOLOR_ZOLTY, id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, b->miejsca[i].wiek, KOLOR_RESET);
                         b->miejsca[i].zgoda_na_wejscie = 3; 
                         continue;
                    }
                }

                if (b->obecna_druzyna == 0 || b->obecna_druzyna == b->miejsca[i].druzyna) {
                    b->obecna_druzyna = b->miejsca[i].druzyna;
                    b->miejsca[i].zgoda_na_wejscie = 1; 
                    
                    printf("%sPracownik %d (Stanowisko %d): Wpuszczam PID %d (Druzyna %c, Wiek %d)%s\n", 
                           KOLOR_ZIELONY, id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, 
                           (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B', b->miejsca[i].wiek, KOLOR_RESET);
                }
            }
        }
    }

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_sektora>\n", argv[0]);
        fprintf(stderr, "  nr_sektora: 0-%d\n", LICZBA_SEKTOROW - 1);
        exit(EXIT_FAILURE);
    }

    id_sektora = parsuj_int(argv[1], "numer sektora", 0, LICZBA_SEKTOROW - 1);
    
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_nsec ^ getpid()));

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();
    rejestruj_sygnaly();

    stan_hali->pidy_pracownikow[id_sektora] = getpid();

    printf("Pracownik sektora %d gotowy (PID: %d). Obsluguje 2 stanowiska kontroli.\n", id_sektora, getpid());

    while (1) {
        obsluguj_bramki();
        usleep(200000);
    }

    return 0;
}