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

void obsluga_sygnalow(int sig) {
    (void)sig;
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    sprzataj_zasoby();
    printf("\nSymulacja zakonczona. Zasoby posprzatane.\n");
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
        perror("semctl init 0");
        exit(EXIT_FAILURE);
    }
    if (semctl(sem_id, 1, SETVAL, 1) == -1) {
        perror("semctl init 1");
        exit(EXIT_FAILURE);
    }

    msg_id = msgget(klucz_msg, IPC_CREAT | 0600);
    SPRAWDZ(msg_id);
}

void uruchom_kierownika() {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./kierownik", "kierownik", NULL);
        perror("execl kierownik");
        exit(EXIT_FAILURE);
    }
}

void uruchom_kasjerow() {
    for (int i = 0; i < LICZBA_KAS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char bufor[10];
            sprintf(bufor, "%d", i);
            execl("./kasjer", "kasjer", bufor, NULL);
            perror("execl kasjer");
            exit(EXIT_FAILURE);
        }
    }
}

void uruchom_pracownikow() {
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char bufor[10];
            sprintf(bufor, "%d", i);
            execl("./pracownik", "pracownik", bufor, NULL);
            perror("execl pracownik");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    signal(SIGINT, obsluga_sygnalow);
    
    srand(time(NULL));
    inicjalizuj_zasoby();

    printf("MAIN: Uruchamianie systemu...\n");

    uruchom_kierownika();
    sleep(1); 
    
    uruchom_kasjerow();
    uruchom_pracownikow();
    sleep(1);

    printf("MAIN: Generowanie kibicow...\n");

    while(1) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./kibic", "kibic", NULL);
            perror("execl kibic");
            exit(EXIT_FAILURE);
        }
        usleep((rand() % 400000) + 100000);
    }

    return 0;
}