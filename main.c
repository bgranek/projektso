#include "common.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int shm_id = -1;
int sem_id = -1;
int msg_id = -1;
StanHali *stan_hali = NULL;
int pojemnosc_K = POJEMNOSC_DOMYSLNA;
int czas_do_meczu = CZAS_DO_MECZU_DOMYSLNY;
int czas_trwania_meczu = CZAS_TRWANIA_MECZU_DOMYSLNY;
volatile sig_atomic_t main_dziala = 1;
volatile sig_atomic_t shutdown_request = 0;

void usun_fifo() {
    if (unlink(FIFO_PRACOWNIK_KIEROWNIK) == -1) {
        if (errno != ENOENT) {
            perror("unlink FIFO");
        }
    }
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
        if (shmdt(stan_hali) == -1) {
            perror("shmdt main");
        }
    }
    if (shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
            perror("shmctl IPC_RMID");
        }
        printf("MAIN: Usunieto pamiec dzielona.\n");
        rejestr_log("MAIN", "Usunieto pamiec dzielona");
    }
    if (sem_id != -1) {
        if (semctl(sem_id, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID");
        }
        printf("MAIN: Usunieto semafory.\n");
        rejestr_log("MAIN", "Usunieto semafory");
    }
    if (msg_id != -1) {
        if (msgctl(msg_id, IPC_RMID, NULL) == -1) {
            perror("msgctl IPC_RMID");
        }
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
    shutdown_request = 1;
    main_dziala = 0;
}

void utworz_fifo() {
    unlink(FIFO_PRACOWNIK_KIEROWNIK);

    if (mkfifo(FIFO_PRACOWNIK_KIEROWNIK, 0600) == -1) {
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

    shm_id = shmget(klucz_shm, sizeof(StanHali), IPC_CREAT | IPC_EXCL | 0600);
    if (shm_id == -1 && errno == EEXIST) {
        int stary = shmget(klucz_shm, sizeof(StanHali), 0600);
        if (stary != -1) {
            if (shmctl(stary, IPC_RMID, NULL) == -1) {
                perror("shmctl stary");
            }
        }
        shm_id = shmget(klucz_shm, sizeof(StanHali), IPC_CREAT | 0600);
    }
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
    stan_hali->pojemnosc_vip = stan_hali->limit_vip;

    stan_hali->pid_main = getpid();
    stan_hali->pid_kierownika = 0;
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

    stan_hali->aktywne_kasy = 2;
    for (int i = 0; i < LICZBA_KAS; i++) {
        stan_hali->kasa_aktywna[i] = (i < stan_hali->aktywne_kasy) ? 1 : 0;
        stan_hali->kasa_zamykanie[i] = 0;
    }

    sem_id = semget(klucz_sem, SEM_TOTAL, IPC_CREAT | IPC_EXCL | 0600);
    if (sem_id == -1 && errno == EEXIST) {
        int stary = semget(klucz_sem, 0, 0600);
        if (stary != -1) {
            if (semctl(stary, 0, IPC_RMID) == -1) {
                perror("semctl stary");
            }
        }
        sem_id = semget(klucz_sem, SEM_TOTAL, IPC_CREAT | 0600);
    }
    SPRAWDZ(sem_id);

    if (semctl(sem_id, 0, SETVAL, 1) == -1) {
        perror("semctl init 0");
        exit(EXIT_FAILURE);
    }
    if (semctl(sem_id, 1, SETVAL, 1) == -1) {
        perror("semctl init 1");
        exit(EXIT_FAILURE);
    }
    for (int i = 2; i < SEM_TOTAL; i++) {
        if (semctl(sem_id, i, SETVAL, 0) == -1) {
            perror("semctl init notification sem");
            exit(EXIT_FAILURE);
        }
    }

    if (semctl(sem_id, SEM_START_MECZU, SETVAL, 1) == -1) {
        perror("semctl init start meczu");
        exit(EXIT_FAILURE);
    }

    msg_id = msgget(klucz_msg, IPC_CREAT | IPC_EXCL | 0600);
    if (msg_id == -1 && errno == EEXIST) {
        int stary = msgget(klucz_msg, 0600);
        if (stary != -1) {
            if (msgctl(stary, IPC_RMID, NULL) == -1) {
                perror("msgctl stary");
            }
        }
        msg_id = msgget(klucz_msg, IPC_CREAT | 0600);
    }
    SPRAWDZ(msg_id);

    utworz_fifo();

    rejestr_log("MAIN", "Zasoby IPC zainicjalizowane");
}

static void zakoncz_symulacje(const char *powod) {
    if (powod != NULL) {
        printf("MAIN: %s\n", powod);
        rejestr_log("MAIN", "%s", powod);
    }

    signal(SIGTERM, SIG_IGN);
    if (kill(0, SIGTERM) == -1) {
        perror("kill SIGTERM");
    }

    for (int s = 0; s < LICZBA_SEKTOROW; s++) {
        struct sembuf wake_workers = {SEM_PRACA(s), 10, 0};
        if (semop(sem_id, &wake_workers, 1) == -1) {
            perror("semop wake workers");
        }
    }

    for (int k = 0; k < LICZBA_KAS; k++) {
        struct sembuf wake_kasa = {SEM_KASA(k), 10, 0};
        if (semop(sem_id, &wake_kasa, 1) == -1) {
            perror("semop wake kasa");
        }
    }

    struct sembuf wake_mutex = {0, 20, 0};
    if (semop(sem_id, &wake_mutex, 1) == -1) {
        perror("semop wake mutex");
    }

    while (1) {
        pid_t w = wait(NULL);
        if (w > 0) continue;
        if (w == -1 && errno == EINTR) continue;
        if (w == -1 && errno != ECHILD) {
            perror("wait");
        }
        break;
    }
    sprzataj_zasoby();
    printf("MAIN: Symulacja zakonczona.\n");
    exit(0);
}

static void sleep_cale(int sekundy) {
    struct timespec ts;
    ts.tv_sec = sekundy;
    ts.tv_nsec = 0;
    while (nanosleep(&ts, &ts) == -1) {
        if (errno != EINTR) {
            perror("nanosleep");
            break;
        }
        if (!main_dziala) return;
    }
}

void* watek_czasu_meczu(void *arg) {
    (void)arg;

    sleep_cale(stan_hali->czas_do_meczu);
    if (!main_dziala) return NULL;

    struct sembuf lock = {0, -1, 0};
    struct sembuf unlock = {0, 1, 0};
    if (semop(sem_id, &lock, 1) == -1) {
        perror("semop lock faza start");
        return NULL;
    }

    if (stan_hali->faza_meczu == FAZA_PRZED_MECZEM) {
        stan_hali->faza_meczu = FAZA_MECZ;
    }

    if (semop(sem_id, &unlock, 1) == -1) {
        perror("semop unlock faza start");
        return NULL;
    }

    if (semctl(sem_id, SEM_START_MECZU, SETVAL, 0) == -1) {
        perror("semctl start meczu");
    }

    time_t czas_rozpoczecia_meczu = time(NULL);
    printf("\n%s", KOLOR_BOLD);
    printf("+===============================================+\n");
    printf("|          MECZ ROZPOCZETY!                     |\n");
    printf("+===============================================+\n");
    printf("%s\n", KOLOR_RESET);
    rejestr_log("MAIN", "MECZ ROZPOCZETY - kibicow w hali: %d",
               stan_hali->suma_kibicow_w_hali);

    sleep_cale(stan_hali->czas_trwania_meczu);
    if (!main_dziala) return NULL;

    if (semop(sem_id, &lock, 1) == -1) {
        perror("semop lock faza koniec");
        return NULL;
    }

    if (stan_hali->faza_meczu == FAZA_MECZ) {
        stan_hali->faza_meczu = FAZA_PO_MECZU;
    }

    if (semop(sem_id, &unlock, 1) == -1) {
        perror("semop unlock faza koniec");
        return NULL;
    }

    struct sembuf sig_faza = {SEM_FAZA_MECZU, 10000, 0};
    if (semop(sem_id, &sig_faza, 1) == -1) {
        perror("semop SEM_FAZA_MECZU");
    }

    printf("\n%s", KOLOR_BOLD);
    printf("+===============================================+\n");
    printf("|          MECZ ZAKONCZONY!                     |\n");
    printf("+===============================================+\n");
    printf("%s\n", KOLOR_RESET);
    rejestr_log("MAIN", "MECZ ZAKONCZONY po %ld s",
               (long)(time(NULL) - czas_rozpoczecia_meczu));

    return NULL;
}

void* watek_generator_kibicow(void *arg) {
    (void)arg;
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();

    while (main_dziala) {
        if (stan_hali->faza_meczu == FAZA_PO_MECZU) break;

        if (stan_hali->ewakuacja_trwa) {
            struct sembuf wait_ewak = {SEM_EWAKUACJA_KONIEC, -1, 0};
            if (semop(sem_id, &wait_ewak, 1) == -1) {
                if (errno == EINTR) continue;
                perror("semop SEM_EWAKUACJA_KONIEC");
                break;
            }
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execl("./kibic", "kibic", NULL);
            perror("execl kibic");
            _exit(EXIT_FAILURE);
        } else if (pid < 0) {
            perror("fork kibic");
        }

        int opoznienie = (rand_r(&seed) % 100000) + 25000;
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = opoznienie * 1000;
        while (nanosleep(&ts, &ts) == -1) {
            if (errno != EINTR) {
                perror("nanosleep");
                break;
            }
        }
    }

    return NULL;
}

void* watek_serwera_socket(void *arg) {
    (void)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return NULL;
    }

    if (fcntl(server_fd, F_SETFD, FD_CLOEXEC) == -1) {
        perror("fcntl server");
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(SOCKET_MONITOR_PORT);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind");
        if (close(server_fd) == -1) {
            perror("close server");
        }
        return NULL;
    }

    if (listen(server_fd, 5) == -1) {
        perror("listen");
        if (close(server_fd) == -1) {
            perror("close server");
        }
        return NULL;
    }

    printf("MAIN: Serwer socket monitoringu aktywny na porcie %d\n", SOCKET_MONITOR_PORT);
    rejestr_log("MAIN", "Serwer socket uruchomiony na porcie %d", SOCKET_MONITOR_PORT);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        if (fcntl(client_fd, F_SETFD, FD_CLOEXEC) == -1) {
            perror("fcntl client");
        }

        char bufor[512];
        int suma_biletow = 0;
        for (int i = 0; i < LICZBA_WSZYSTKICH_SEKTOROW; i++) {
            suma_biletow += stan_hali->liczniki_sektorow[i];
        }
        int len = snprintf(bufor, sizeof(bufor),
            "HALA|OSOB_W_HALI:%d|SUMA_BILETOW:%d|BILETY:%s|EWAKUACJA:%s|FAZA:%d",
            stan_hali->suma_kibicow_w_hali,
            suma_biletow,
            stan_hali->wszystkie_bilety_sprzedane ? "WYPRZEDANE" : "DOSTEPNE",
            stan_hali->ewakuacja_trwa ? "TAK" : "NIE",
            stan_hali->faza_meczu);

        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            len += snprintf(bufor + len, sizeof(bufor) - len,
                "|S%d:%d/%d(osob:%d)%s",
                i,
                stan_hali->liczniki_sektorow[i],
                stan_hali->pojemnosc_sektora,
                stan_hali->osoby_w_sektorze[i],
                stan_hali->sektor_zablokowany[i] ? "[B]" : "");
        }

        len += snprintf(bufor + len, sizeof(bufor) - len,
            "|VIP:%d/%d(osob:%d)",
            stan_hali->liczniki_sektorow[SEKTOR_VIP],
            stan_hali->pojemnosc_vip,
            stan_hali->osoby_w_sektorze[SEKTOR_VIP]);

        if (send(client_fd, bufor, strlen(bufor), 0) == -1) {
            perror("send");
        }
        if (close(client_fd) == -1) {
            perror("close client");
        }
    }

    if (close(server_fd) == -1) {
        perror("close server");
    }
    return NULL;
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
    setlinebuf(stdout);

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

    if (rejestr_init(NULL, 1) == -1) {
        fprintf(stderr, "Ostrzezenie: Nie udalo sie otworzyc pliku rejestru\n");
    }

    printf("===========================================\n");
    printf("    SYMULATOR HALI WIDOWISKOWO-SPORTOWEJ\n");
    printf("===========================================\n\n");

    rejestr_log("MAIN", "Start symulacji z pojemnoscia K=%d", pojemnosc_K);

    signal(SIGINT, obsluga_sygnalow);
    signal(SIGTERM, obsluga_sygnalow);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    signal(SYGNAL_EWAKUACJA, SIG_IGN);

    srand(time(NULL));
    inicjalizuj_zasoby();

    pthread_t watek_socket;
    if (pthread_create(&watek_socket, NULL, watek_serwera_socket, NULL) != 0) {
        perror("pthread_create socket");
    } else {
        pthread_detach(watek_socket);
    }

    pthread_t watek_czasu;
    if (pthread_create(&watek_czasu, NULL, watek_czasu_meczu, NULL) != 0) {
        perror("pthread_create czas");
    }

    pthread_t watek_generator;
    if (pthread_create(&watek_generator, NULL, watek_generator_kibicow, NULL) != 0) {
        perror("pthread_create generator");
    }

    printf("\nMAIN: Uruchamianie systemu...\n");
    printf("MAIN: Uruchom kierownika w osobnym terminalu: ./kierownik\n");

    uruchom_kasjerow();
    uruchom_pracownikow();

    printf("\nMAIN: System gotowy. Generowanie kibicow...\n");
    printf("(Nacisnij Ctrl+C aby zakonczyc)\n\n");

    rejestr_log("MAIN", "System gotowy - rozpoczynam generowanie kibicow");

    struct sembuf wait_faza = {SEM_FAZA_MECZU, -1, 0};
    while (semop(sem_id, &wait_faza, 1) == -1) {
        if (errno == EINTR) {
            if (shutdown_request) break;
            continue;
        }
        perror("semop wait SEM_FAZA_MECZU");
        break;
    }

    if (shutdown_request) {
        printf("\nMAIN: Otrzymano sygnal zakonczenia. Sprzatanie...\n");
        rejestr_log("MAIN", "Otrzymano sygnal zakonczenia");
        main_dziala = 0;
        pthread_cancel(watek_generator);
        pthread_cancel(watek_czasu);
        pthread_join(watek_generator, NULL);
        pthread_join(watek_czasu, NULL);
        zakoncz_symulacje("Przerwano symulacje");
    }

    main_dziala = 0;

    if (pthread_join(watek_generator, NULL) != 0) {
        perror("pthread_join generator");
    }
    if (pthread_join(watek_czasu, NULL) != 0) {
        perror("pthread_join czas");
    }

    printf("MAIN: Czekam az kibice opuszcza hale...\n");
    rejestr_log("MAIN", "Czekam na wyjscie kibicow");

    while (1) {
        struct sembuf lock = {0, -1, 0};
        struct sembuf unlock = {0, 1, 0};
        if (semop(sem_id, &lock, 1) == -1) {
            if (errno == EINTR) continue;
            perror("semop lock suma");
            break;
        }

        int pozostalo = stan_hali->suma_kibicow_w_hali;

        if (semop(sem_id, &unlock, 1) == -1) {
            perror("semop unlock suma");
            break;
        }

        if (pozostalo <= 0) {
            printf("MAIN: Wszyscy kibice opuscili hale.\n");
            rejestr_log("MAIN", "Wszyscy kibice opuscili hale");
            break;
        }

        struct sembuf wait_wyjscie = {SEM_KIBIC_WYSZEDL, -1, 0};
        if (semop(sem_id, &wait_wyjscie, 1) == -1) {
            if (errno == EINTR) continue;
            perror("semop wait SEM_KIBIC_WYSZEDL");
            break;
        }
    }

    zakoncz_symulacje("Koniec symulacji po zakonczeniu meczu");

    return 0;
}