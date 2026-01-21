#include "common.h"

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
StanHali *stan_hali = NULL;
int pojemnosc_K = POJEMNOSC_DOMYSLNA;

void sprzataj_zasoby() {
    if (stan_hali != NULL) {
        shmdt(stan_hali);
    }
    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        printf("MAIN: Usunieto pamiec dzielona.\n");
    }
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        printf("MAIN: Usunieto semafory.\n");
    }
    if (msg_id != -1) {
        msgctl(msg_id, IPC_RMID, NULL);
        printf("MAIN: Usunieto kolejke komunikatow.\n");
    }
}

void obsluga_sygnalow(int sig) {
    (void)sig;
    printf("\nMAIN: Otrzymano sygnal zakonczenia. Sprzatanie...\n");
    signal(SIGTERM, SIG_IGN);
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    sprzataj_zasoby();
    printf("MAIN: Symulacja zakonczona. Zasoby posprzatane.\n");
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

    stan_hali->pojemnosc_calkowita = pojemnosc_K;
    stan_hali->pojemnosc_sektora = pojemnosc_K / LICZBA_SEKTOROW;
    stan_hali->limit_vip = (pojemnosc_K * 3) / 1000;
    if (stan_hali->limit_vip < 1) stan_hali->limit_vip = 1;

    stan_hali->pid_kierownika = getpid();
    stan_hali->liczba_vip = 0;

    printf("MAIN: Parametry hali:\n");
    printf("  - Pojemnosc calkowita (K): %d\n", stan_hali->pojemnosc_calkowita);
    printf("  - Pojemnosc sektora (K/8): %d\n", stan_hali->pojemnosc_sektora);
    printf("  - Limit VIP (<0.3%% * K): %d\n", stan_hali->limit_vip);

    for (int i = 0; i < LICZBA_KAS; i++) {
        stan_hali->kasa_aktywna[i] = (i < 2) ? 1 : 0;
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
    } else if (pid < 0) {
        perror("fork kierownik");
        exit(EXIT_FAILURE);
    }
    printf("MAIN: Uruchomiono kierownika (PID: %d)\n", pid);
}

void uruchom_kasjerow() {
    for (int i = 0; i < LICZBA_KAS; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char bufor[10];
            snprintf(bufor, sizeof(bufor), "%d", i);
            execl("./kasjer", "kasjer", bufor, NULL);
            perror("execl kasjer");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork kasjer");
        }
    }
    printf("MAIN: Uruchomiono %d kasjerow\n", LICZBA_KAS);
}

void uruchom_pracownikow() {
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            char bufor[10];
            snprintf(bufor, sizeof(bufor), "%d", i);
            execl("./pracownik", "pracownik", bufor, NULL);
            perror("execl pracownik");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork pracownik");
        }
    }
    printf("MAIN: Uruchomiono %d pracownikow\n", LICZBA_SEKTOROW);
}

void wyswietl_pomoc(const char *nazwa_programu) {
    printf("Uzycie: %s [OPCJE]\n", nazwa_programu);
    printf("\nOpcje:\n");
    printf("  -k <liczba>   Pojemnosc hali K (domyslnie: %d)\n", POJEMNOSC_DOMYSLNA);
    printf("                Zakres: %d - %d\n", POJEMNOSC_MIN, POJEMNOSC_MAX);
    printf("                Musi byc podzielne przez 8\n");
    printf("  -h            Wyswietl te pomoc\n");
    printf("\nPrzyklady:\n");
    printf("  %s              # Uruchom z domyslna pojemnoscia %d\n", nazwa_programu, POJEMNOSC_DOMYSLNA);
    printf("  %s -k 800       # Uruchom z pojemnoscia 800\n", nazwa_programu);
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "k:h")) != -1) {
        switch (opt) {
            case 'k':
                pojemnosc_K = parsuj_int(optarg, "pojemnosc K", POJEMNOSC_MIN, POJEMNOSC_MAX);
                if (pojemnosc_K % 8 != 0) {
                    fprintf(stderr, "Blad: Pojemnosc K musi byc podzielna przez 8 (podano: %d)\n", pojemnosc_K);
                    exit(EXIT_FAILURE);
                }
                break;
            case 'h':
                wyswietl_pomoc(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                wyswietl_pomoc(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    printf("===========================================\n");
    printf("    SYMULATOR HALI WIDOWISKOWO-SPORTOWEJ\n");
    printf("===========================================\n\n");

    signal(SIGINT, obsluga_sygnalow);
    signal(SIGTERM, obsluga_sygnalow);
    signal(SIGCHLD, SIG_IGN);

    srand(time(NULL));
    inicjalizuj_zasoby();

    printf("\nMAIN: Uruchamianie systemu...\n");

    uruchom_kierownika();
    sleep(1);

    uruchom_kasjerow();
    uruchom_pracownikow();
    sleep(1);

    printf("\nMAIN: System gotowy. Generowanie kibicow...\n");
    printf("(Nacisnij Ctrl+C aby zakonczyc)\n\n");

    while(1) {
        pid_t pid = fork();
        if (pid == 0) {
            execl("./kibic", "kibic", NULL);
            perror("execl kibic");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork kibic");
        }
        usleep((rand() % 400000) + 100000);
    }

    return 0;
}