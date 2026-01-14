#include "common.h"

int id_sektora = -1;
int shm_id = -1;
StanHali *stan_hali = NULL;

void obsluga_wyjscia() {
    if (stan_hali != NULL) {
        shmdt(stan_hali);
    }
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

    printf("Pracownik sektora %d uruchomiony (PID: %d). Oczekiwanie...\n", id_sektora, getpid());

    while (1) {
        sleep(1);
    }

    return 0;
}