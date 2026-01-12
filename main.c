#include "common.h"

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
StanHali *stan_hali = NULL;

void sprzataj_zasoby() {
    if (stan_hali != NULL) {
        shmdt(stan_hali);
    }
    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
    }
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
    }
    if (msg_id != -1) {
        msgctl(msg_id, IPC_RMID, NULL);
    }
}

void obsluga_sigint(int sig) {
    (void)sig;
    exit(0);
}

void inicjalizuj_zasoby() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);
    key_t klucz_sem = ftok(".", KLUCZ_SEM);
    SPRAWDZ(klucz_sem);
    key_t klucz_msg = ftok(".", KLUCZ_MSG);
    SPRAWDZ(klucz_msg);

    shm_id = shmget(klucz_shm, sizeof(StanHali), IPC_CREAT | 0600);
    SPRAWDZ(shm_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    memset(stan_hali, 0, sizeof(StanHali));
    stan_hali->pid_kierownika = getpid();

    for (int i = 0; i < LICZBA_KAS; i++) {
        if (i < 2) {
            stan_hali->kasa_aktywna[i] = 1;
        } else {
            stan_hali->kasa_aktywna[i] = 0;
        }
    }

    sem_id = semget(klucz_sem, 2, IPC_CREAT | 0600);
    SPRAWDZ(sem_id);

    if (semctl(sem_id, 0, SETVAL, 1) == -1) {
        perror("semctl init");
        exit(EXIT_FAILURE);
    }

    msg_id = msgget(klucz_msg, IPC_CREAT | 0600);
    SPRAWDZ(msg_id);
}

int main() {
    if (atexit(sprzataj_zasoby) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = obsluga_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    inicjalizuj_zasoby();

    printf("System uruchomiony. PID: %d\n", getpid());
    printf("Pojemnosc calkowita: %d\n", POJEMNOSC_CALKOWITA);
    printf("Ctrl+C konczy program.\n");

    while (1) {
        sleep(1);
    }

    return 0;
}