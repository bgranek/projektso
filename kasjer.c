#include "common.h"

int id_kasjera = -1;
int shm_id = -1;
int msg_id = -1;
StanHali *stan_hali = NULL;

void obsluga_wyjscia() {
    if (stan_hali != NULL && id_kasjera >= 0) {
        stan_hali->pidy_kasjerow[id_kasjera] = 0;
        stan_hali->kasa_aktywna[id_kasjera] = 0;
        shmdt(stan_hali);
    }
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);
    key_t klucz_msg = ftok(".", KLUCZ_MSG);
    SPRAWDZ(klucz_msg);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

    msg_id = msgget(klucz_msg, 0600);
    SPRAWDZ(msg_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_kasjera>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    id_kasjera = atoi(argv[1]);
    if (id_kasjera < 0 || id_kasjera >= LICZBA_KAS) {
        fprintf(stderr, "Bledny numer kasjera: %d (musi byc 0-%d)\n", id_kasjera, LICZBA_KAS - 1);
        exit(EXIT_FAILURE);
    }

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();

    stan_hali->pidy_kasjerow[id_kasjera] = getpid();
    stan_hali->kasa_aktywna[id_kasjera] = 1;

    printf("Kasjer nr %d gotowy (PID: %d). Oczekiwanie na klientow...\n", id_kasjera, getpid());

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja! Zamykam kase.\n", id_kasjera);
            break;
        }
        sleep(1);
    }

    return 0;
}