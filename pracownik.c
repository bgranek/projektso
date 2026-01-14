#include "common.h"

int id_sektora = -1;
int shm_id = -1;
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
        const char *msg = "SEKTOR ZABLOKOWANY\n";
        write(STDOUT_FILENO, msg, strlen(msg));
    }
}

void handler_odblokowanie(int sig) {
    (void)sig;
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->sektor_zablokowany[id_sektora] = 0;
        const char *msg = "SEKTOR ODBLOKOWANY\n";
        write(STDOUT_FILENO, msg, strlen(msg));
    }
}

void handler_ewakuacja(int sig) {
    (void)sig;
    const char *msg = "EWAKUACJA! Otwieram bramki awaryjne.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    exit(0);
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

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

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_sektora>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    id_sektora = atoi(argv[1]);
    if (id_sektora < 0 || id_sektora >= LICZBA_SEKTOROW) {
        fprintf(stderr, "Bledny numer sektora: %d (musi byc 0-%d)\n", id_sektora, LICZBA_SEKTOROW - 1);
        exit(EXIT_FAILURE);
    }

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();
    rejestruj_sygnaly();

    stan_hali->pidy_pracownikow[id_sektora] = getpid();
    stan_hali->sektor_zablokowany[id_sektora] = 0;

    printf("Pracownik sektora %d gotowy (PID: %d).\n", id_sektora, getpid());

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            handler_ewakuacja(0);
        }
        sleep(1);
    }

    return 0;
}