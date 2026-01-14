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

void obsluz_klienta() {
    KomunikatBilet zapytanie;
    
    if (msgrcv(msg_id, &zapytanie, sizeof(KomunikatBilet) - sizeof(long), TYP_KOMUNIKATU_ZAPYTANIE, IPC_NOWAIT) == -1) {
        if (errno != ENOMSG) {
            perror("msgrcv");
        }
        return;
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    
    if (semop(sem_id, operacje, 1) == -1) {
        perror("semop P");
        return;
    }

    int znaleziono_sektor = -1;
    int start_sektor = rand() % LICZBA_SEKTOROW;
    
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        int idx = (start_sektor + i) % LICZBA_SEKTOROW;
        
        if (stan_hali->liczniki_sektorow[idx] < POJEMNOSC_SEKTORA) {
            stan_hali->liczniki_sektorow[idx]++;
            znaleziono_sektor = idx;
            break;
        }
    }

    operacje[0].sem_op = 1;
    if (semop(sem_id, operacje, 1) == -1) {
        perror("semop V");
    }

    OdpowiedzBilet odpowiedz;
    odpowiedz.mtype = zapytanie.pid_kibica;
    odpowiedz.przydzielony_sektor = znaleziono_sektor;
    odpowiedz.czy_sukces = (znaleziono_sektor != -1) ? 1 : 0;

    if (msgsnd(msg_id, &odpowiedz, sizeof(OdpowiedzBilet) - sizeof(long), 0) == -1) {
        perror("msgsnd");
    } else {
        if (odpowiedz.czy_sukces) {
            printf("Kasjer %d: Sprzedano bilet (Sektor %d) dla PID %d.\n", 
                   id_kasjera, odpowiedz.przydzielony_sektor, zapytanie.pid_kibica);
        } else {
            printf("Kasjer %d: Brak miejsc dla PID %d.\n", id_kasjera, zapytanie.pid_kibica);
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

    srand(time(NULL) ^ (getpid() << 16));
    inicjalizuj();

    stan_hali->pidy_kasjerow[id_kasjera] = getpid();
    stan_hali->kasa_aktywna[id_kasjera] = 1;

    printf("Kasjer nr %d gotowy (PID: %d). Oczekiwanie na klientow...\n", id_kasjera, getpid());

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja! Zamykam kase.\n", id_kasjera);
            break;
        }
        
        obsluz_klienta();
        usleep(100000); 
    }

    return 0;
}