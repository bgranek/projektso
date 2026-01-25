#include "common.h"

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
StanHali *stan_hali = NULL;
int pojemnosc_K = POJEMNOSC_DOMYSLNA;
int czas_do_meczu = CZAS_DO_MECZU_DOMYSLNY;
int czas_trwania_meczu = CZAS_TRWANIA_MECZU_DOMYSLNY;

void usun_fifo() {
    unlink(FIFO_PRACOWNIK_KIEROWNIK);
}

void sprzataj_zasoby() {
    rejestr_log("MAIN", "Rozpoczynam sprzatanie zasobow");

    if (stan_hali != NULL) {
        rejestr_statystyki(
            stan_hali->pojemnosc_calkowita,
            stan_hali->suma_kibicow_w_hali,
            stan_hali->liczba_vip,
            stan_hali->limit_vip
        );
        shmdt(stan_hali);
    }
    if (shm_id != -1) {
        shmctl(shm_id, IPC_RMID, NULL);
        printf("MAIN: Usunieto pamiec dzielona.\n");
        rejestr_log("MAIN", "Usunieto pamiec dzielona");
    }
    if (sem_id != -1) {
        semctl(sem_id, 0, IPC_RMID);
        printf("MAIN: Usunieto semafory.\n");
        rejestr_log("MAIN", "Usunieto semafory");
    }
    if (msg_id != -1) {
        msgctl(msg_id, IPC_RMID, NULL);
        printf("MAIN: Usunieto kolejke komunikatow.\n");
        rejestr_log("MAIN", "Usunieto kolejke komunikatow");
    }
    usun_fifo();
    printf("MAIN: Usunieto FIFO.\n");
    rejestr_log("MAIN", "Usunieto FIFO");

    rejestr_zamknij();
}

void obsluga_sygnalow(int sig) {
    (void)sig;
    printf("\nMAIN: Otrzymano sygnal zakonczenia. Sprzatanie...\n");
    rejestr_log("MAIN", "Otrzymano sygnal zakonczenia");
    signal(SIGTERM, SIG_IGN);
    kill(0, SIGTERM);
    while (wait(NULL) > 0);
    sprzataj_zasoby();
    printf("MAIN: Symulacja zakonczona. Zasoby posprzatane.\n");
    exit(0);
}

void utworz_fifo() {
    unlink(FIFO_PRACOWNIK_KIEROWNIK);

    if (mkfifo(FIFO_PRACOWNIK_KIEROWNIK, 0666) == -1) {
        if (errno != EEXIST) {
            perror("mkfifo");
            exit(EXIT_FAILURE);
        }
    }
    printf("MAIN: Utworzono FIFO: %s\n", FIFO_PRACOWNIK_KIEROWNIK);
    rejestr_log("MAIN", "Utworzono FIFO: %s", FIFO_PRACOWNIK_KIEROWNIK);
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
    stan_hali->pojemnosc_vip = stan_hali->limit_vip;

    stan_hali->pid_kierownika = getpid();
    stan_hali->liczba_vip = 0;
    stan_hali->wszystkie_bilety_sprzedane = 0;

    stan_hali->faza_meczu = FAZA_PRZED_MECZEM;
    stan_hali->czas_startu_symulacji = time(NULL);
    stan_hali->czas_do_meczu = czas_do_meczu;
    stan_hali->czas_trwania_meczu = czas_trwania_meczu;

    printf("MAIN: Parametry hali:\n");
    printf("  - Pojemnosc calkowita (K): %d\n", stan_hali->pojemnosc_calkowita);
    printf("  - Pojemnosc sektora (K/8): %d\n", stan_hali->pojemnosc_sektora);
    printf("  - Pojemnosc VIP: %d\n", stan_hali->pojemnosc_vip);
    printf("  - Limit VIP (<0.3%% * K): %d\n", stan_hali->limit_vip);
    printf("  - Czas do meczu: %d sekund\n", stan_hali->czas_do_meczu);
    printf("  - Czas trwania meczu: %d sekund\n", stan_hali->czas_trwania_meczu);

    rejestr_log("MAIN", "Pojemnosc calkowita: %d", stan_hali->pojemnosc_calkowita);
    rejestr_log("MAIN", "Pojemnosc sektora: %d", stan_hali->pojemnosc_sektora);
    rejestr_log("MAIN", "Pojemnosc VIP: %d", stan_hali->pojemnosc_vip);
    rejestr_log("MAIN", "Limit VIP: %d", stan_hali->limit_vip);
    rejestr_log("MAIN", "Czas do meczu: %d s, czas trwania: %d s",
                stan_hali->czas_do_meczu, stan_hali->czas_trwania_meczu);

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

    utworz_fifo();

    rejestr_log("MAIN", "Zasoby IPC zainicjalizowane");
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
    rejestr_log("MAIN", "Uruchomiono kierownika PID: %d", pid);
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
    rejestr_log("MAIN", "Uruchomiono %d kasjerow", LICZBA_KAS);
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
    rejestr_log("MAIN", "Uruchomiono %d pracownikow", LICZBA_SEKTOROW);
}

void wyswietl_pomoc(const char *nazwa_programu) {
    printf("Uzycie: %s [OPCJE]\n", nazwa_programu);
    printf("\nOpcje:\n");
    printf("  -k <liczba>   Pojemnosc hali K (domyslnie: %d)\n", POJEMNOSC_DOMYSLNA);
    printf("                Zakres: %d - %d\n", POJEMNOSC_MIN, POJEMNOSC_MAX);
    printf("                Musi byc podzielne przez 8\n");
    printf("  -t <sekundy>  Czas do rozpoczecia meczu (domyslnie: %d)\n", CZAS_DO_MECZU_DOMYSLNY);
    printf("  -d <sekundy>  Czas trwania meczu (domyslnie: %d)\n", CZAS_TRWANIA_MECZU_DOMYSLNY);
    printf("                Zakres czasow: %d - %d sekund\n", CZAS_MIN, CZAS_MAX);
    printf("  -h            Wyswietl te pomoc\n");
    printf("\nPrzyklady:\n");
    printf("  %s                    # Uruchom z domyslnymi parametrami\n", nazwa_programu);
    printf("  %s -k 800             # Pojemnosc 800\n", nazwa_programu);
    printf("  %s -k 800 -t 60 -d 120  # Mecz za 60s, trwa 120s\n", nazwa_programu);
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "k:t:d:h")) != -1) {
        switch (opt) {
            case 'k':
                pojemnosc_K = parsuj_int(optarg, "pojemnosc K", POJEMNOSC_MIN, POJEMNOSC_MAX);
                if (pojemnosc_K % 8 != 0) {
                    fprintf(stderr, "Blad: Pojemnosc K musi byc podzielna przez 8 (podano: %d)\n", pojemnosc_K);
                    exit(EXIT_FAILURE);
                }
                break;
            case 't':
                czas_do_meczu = parsuj_int(optarg, "czas do meczu", CZAS_MIN, CZAS_MAX);
                break;
            case 'd':
                czas_trwania_meczu = parsuj_int(optarg, "czas trwania meczu", CZAS_MIN, CZAS_MAX);
                break;
            case 'h':
                wyswietl_pomoc(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                wyswietl_pomoc(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (rejestr_init(NULL) == -1) {
        fprintf(stderr, "Ostrzezenie: Nie udalo sie otworzyc pliku rejestru\n");
    }

    printf("===========================================\n");
    printf("    SYMULATOR HALI WIDOWISKOWO-SPORTOWEJ\n");
    printf("===========================================\n\n");

    rejestr_log("MAIN", "Start symulacji z pojemnoscia K=%d", pojemnosc_K);

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

    rejestr_log("MAIN", "System gotowy - rozpoczynam generowanie kibicow");

    time_t czas_rozpoczecia_meczu = 0;

    while(1) {
        time_t teraz = time(NULL);
        time_t uplynelo = teraz - stan_hali->czas_startu_symulacji;

        if (stan_hali->faza_meczu == FAZA_PRZED_MECZEM) {
            if (uplynelo >= stan_hali->czas_do_meczu) {
                stan_hali->faza_meczu = FAZA_MECZ;
                czas_rozpoczecia_meczu = teraz;
                printf("\n%s", KOLOR_BOLD);
                printf("+===============================================+\n");
                printf("|          MECZ ROZPOCZETY!                     |\n");
                printf("+===============================================+\n");
                printf("%s\n", KOLOR_RESET);
                rejestr_log("MAIN", "MECZ ROZPOCZETY - kibicow w hali: %d",
                           stan_hali->suma_kibicow_w_hali);
            }
        } else if (stan_hali->faza_meczu == FAZA_MECZ) {
            time_t czas_meczu = teraz - czas_rozpoczecia_meczu;
            if (czas_meczu >= stan_hali->czas_trwania_meczu) {
                stan_hali->faza_meczu = FAZA_PO_MECZU;
                printf("\n%s", KOLOR_BOLD);
                printf("+===============================================+\n");
                printf("|          MECZ ZAKONCZONY!                     |\n");
                printf("+===============================================+\n");
                printf("%s\n", KOLOR_RESET);
                rejestr_log("MAIN", "MECZ ZAKONCZONY");

                printf("MAIN: Koniec meczu - zamykam symulacje.\n");
                rejestr_log("MAIN", "Koniec symulacji po zakonczeniu meczu");
                kill(0, SIGTERM);
                while (wait(NULL) > 0);
                sprzataj_zasoby();
                printf("MAIN: Symulacja zakonczona pomyslnie.\n");
                exit(0);
            }
        }

        if (stan_hali->ewakuacja_trwa) {
            printf("MAIN: Ewakuacja w toku - wstrzymano generowanie kibicow.\n");
            rejestr_log("MAIN", "Ewakuacja - wstrzymano generowanie kibicow");
            sleep(2);
            continue;
        }

        if (stan_hali->faza_meczu == FAZA_MECZ) {
            sleep(1);
            continue;
        }

        if (stan_hali->wszystkie_bilety_sprzedane) {
            sleep(1);
            continue;
        }

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