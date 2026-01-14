#include "common.h"
#include <time.h>

int id_kasjera = -1;
int shm_id = -1;
int msg_id = -1;
int sem_id = -1;
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
    key_t klucz_sem = ftok(".", KLUCZ_SEM);
    SPRAWDZ(klucz_sem);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

    msg_id = msgget(klucz_msg, 0600);
    SPRAWDZ(msg_id);

    sem_id = semget(klucz_sem, 2, 0600);
    SPRAWDZ(sem_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

void sprawdz_kolejke() {
    KomunikatBilet zapytanie;
    
    ssize_t status = msgrcv(msg_id, &zapytanie, sizeof(KomunikatBilet) - sizeof(long), TYP_KOMUNIKATU_ZAPYTANIE, IPC_NOWAIT);
    
    if (status != -1) {
        printf("Kasjer %d: Otrzymano prosbe o bilet od PID %d (Druzyna: %d, VIP: %d)\n", 
               id_kasjera, zapytanie.pid_kibica, zapytanie.id_druzyny, zapytanie.czy_vip);
    } else {
        if (errno != ENOMSG) {
            perror("msgrcv");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_kasjera>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    id_kasjera = atoi(argv[1]);
    if (id_kasjera < 0 || id_kasjera >= LICZBA_KAS) {
        fprintf(stderr, "Bledny numer kasjera: %d\n", id_kasjera);
        exit(EXIT_FAILURE);
    }

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();

    stan_hali->pidy_kasjerow[id_kasjera] = getpid();
    stan_hali->kasa_aktywna[id_kasjera] = 1;

    printf("Kasjer nr %d gotowy (PID: %d). Nasluchuje...\n", id_kasjera, getpid());

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja! Zamykam kase.\n", id_kasjera);
            break;
        }
        
        sprawdz_kolejke();
        usleep(100000);
    }

    return 0;
}