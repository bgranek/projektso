#include "common.h"

int shm_id = -1;
int sem_id = -1;
int fifo_fd = -1;
StanHali *stan_hali = NULL;

void obsluga_wyjscia() {
    if (fifo_fd != -1) {
        if (close(fifo_fd) == -1) {
            perror("close FIFO kierownik");
        }
    }
    if (stan_hali != NULL) {
        if (shmdt(stan_hali) == -1) {
            perror("shmdt kierownik");
        }
    }
}

void obsluga_sigint(int sig) {
    (void)sig;
    printf("\nKierownik: Zamykanie...\n");
    rejestr_log("KIEROWNIK", "Zamykanie");
    exit(0);
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);
    key_t klucz_sem = ftok(".", KLUCZ_SEM);
    SPRAWDZ(klucz_sem);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

    sem_id = semget(klucz_sem, SEM_TOTAL, 0600);
    SPRAWDZ(sem_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    fifo_fd = open(FIFO_PRACOWNIK_KIEROWNIK, O_RDWR);
    if (fifo_fd == -1) {
        perror("open FIFO (kierownik)");
    } else {
        printf("Kierownik: Otwarto FIFO do odbioru zgloszen.\n");
    }

    rejestr_init(NULL, 0);
}

void* watek_fifo(void *arg) {
    (void)arg;
    if (fifo_fd == -1) return NULL;

    while (1) {
        KomunikatFifo kom;
        size_t offset = 0;
        while (offset < sizeof(KomunikatFifo)) {
            ssize_t r = read(fifo_fd, (char*)&kom + offset, sizeof(KomunikatFifo) - offset);
            if (r == -1) {
                if (errno == EINTR) continue;
                perror("read FIFO");
                return NULL;
            }
            if (r == 0) continue;
            offset += (size_t)r;
        }

        printf("\n%s", KOLOR_CYAN);
        printf("+========================================+\n");
        printf("|       ZGLOSZENIE OD PRACOWNIKA         |\n");
        printf("+========================================+\n");
        printf("| Sektor: %d\n", kom.nr_sektora);
        printf("| PID pracownika: %d\n", kom.pid_pracownika);
        printf("| Typ: %s\n", kom.typ == 1 ? "EWAKUACJA ZAKONCZONA" : "INNE");
        printf("| Wiadomosc: %s\n", kom.wiadomosc);
        printf("+========================================+\n");
        printf("%s\n", KOLOR_RESET);

        rejestr_log("KIEROWNIK", "Zgloszenie od sektora %d: %s", kom.nr_sektora, kom.wiadomosc);

        if (kom.typ == 1) {
            struct sembuf lock = {0, -1, 0};
            struct sembuf unlock = {0, 1, 0};
            if (semop(sem_id, &lock, 1) == -1) {
                if (errno != EINTR) perror("semop lock ewakuacja");
            } else {
                stan_hali->sektor_ewakuowany[kom.nr_sektora] = 1;

                int wszystkie = 1;
                for (int i = 0; i < LICZBA_SEKTOROW; i++) {
                    if (!stan_hali->sektor_ewakuowany[i]) {
                        wszystkie = 0;
                        break;
                    }
                }
                if (wszystkie) {
                    printf("%s[INFO] WSZYSTKIE SEKTORY EWAKUOWANE! Hala pusta.%s\n",
                           KOLOR_ZIELONY, KOLOR_RESET);
                    rejestr_log("KIEROWNIK", "Wszystkie sektory ewakuowane - hala pusta");
                    stan_hali->ewakuacja_trwa = 0;
                    struct sembuf sig_ewak = {SEM_EWAKUACJA_KONIEC, 1, 0};
                    if (semop(sem_id, &sig_ewak, 1) == -1) {
                        perror("semop SEM_EWAKUACJA_KONIEC");
                    }
                }

                if (semop(sem_id, &unlock, 1) == -1) {
                    perror("semop unlock ewakuacja");
                }
            }
        }
    }
    return NULL;
}

void pokaz_status() {
    printf("\n%s", KOLOR_BOLD);
    printf("+=========================================================+\n");
    printf("|                      STATUS HALI                        |\n");
    printf("+=========================================================+\n");
    printf("%s", KOLOR_RESET);
    printf("| Pojemnosc: %d | Kibicow w hali: %d | VIP: %d/%d\n",
           stan_hali->pojemnosc_calkowita,
           stan_hali->suma_kibicow_w_hali,
           stan_hali->liczba_vip,
           stan_hali->limit_vip);
    printf("| Bilety: %s\n",
           stan_hali->wszystkie_bilety_sprzedane ? "WYPRZEDANE" : "DOSTEPNE");
    printf("+---------------------------------------------------------+\n");
    printf("| SEKTORY ZWYKLE (0-7):\n");

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        char status[32] = "";
        if (stan_hali->sektor_zablokowany[i]) strcat(status, "[ZABLOK]");
        if (stan_hali->sektor_ewakuowany[i]) strcat(status, "[EWAK]");
        if (strlen(status) == 0) strcpy(status, "[OK]");

        char *kolor = KOLOR_ZIELONY;
        if (stan_hali->sektor_zablokowany[i]) kolor = KOLOR_CZERWONY;
        else if (stan_hali->sektor_ewakuowany[i]) kolor = KOLOR_ZOLTY;

        printf("|  %s[%d] %3d/%3d %s%s\n",
               kolor,
               i,
               stan_hali->liczniki_sektorow[i],
               stan_hali->pojemnosc_sektora,
               status,
               KOLOR_RESET);

        rejestr_sektor(i, stan_hali->liczniki_sektorow[i],
                      stan_hali->pojemnosc_sektora, stan_hali->sektor_zablokowany[i]);
    }

    printf("+---------------------------------------------------------+\n");
    printf("| %sSEKTOR VIP: %d/%d%s\n",
           KOLOR_MAGENTA,
           stan_hali->liczniki_sektorow[SEKTOR_VIP],
           stan_hali->pojemnosc_vip,
           KOLOR_RESET);
    printf("+---------------------------------------------------------+\n");
    printf("| KASY: ");
    for (int i = 0; i < LICZBA_KAS; i++) {
        if (stan_hali->kasa_aktywna[i]) {
            printf("%s[%d]%s ", KOLOR_ZIELONY, i, KOLOR_RESET);
        } else {
            printf("%s[%d]%s ", KOLOR_CZERWONY, i, KOLOR_RESET);
        }
    }
    printf("\n");

    printf("| Ewakuacja: %s%s%s\n",
           stan_hali->ewakuacja_trwa ? KOLOR_CZERWONY : KOLOR_ZIELONY,
           stan_hali->ewakuacja_trwa ? "TAK" : "NIE",
           KOLOR_RESET);
    printf("+=========================================================+\n");

    rejestr_statystyki(stan_hali->pojemnosc_calkowita, stan_hali->suma_kibicow_w_hali,
                      stan_hali->liczba_vip, stan_hali->limit_vip);
}

void wyslij_blokade() {
    int nr_sektora = bezpieczny_scanf_int(
        "Podaj numer sektora do zablokowania (0-7): ",
        0,
        LICZBA_SEKTOROW - 1
    );

    if (nr_sektora < 0) {
        printf("Anulowano.\n");
        return;
    }

    if (stan_hali->sektor_zablokowany[nr_sektora]) {
        printf("%s[UWAGA] Sektor %d jest juz zablokowany.%s\n",
               KOLOR_ZOLTY, nr_sektora, KOLOR_RESET);
        return;
    }

    pid_t pid = stan_hali->pidy_pracownikow[nr_sektora];
    if (pid > 0) {
        if (kill(pid, SYGNAL_BLOKADA_SEKTORA) == 0) {
            printf("%s[OK] Wyslano sygnal BLOKADA do pracownika sektora %d (PID: %d)%s\n",
                   KOLOR_ZIELONY, nr_sektora, pid, KOLOR_RESET);
            rejestr_log("KIEROWNIK", "Wyslano blokade do sektora %d", nr_sektora);
        } else {
            perror("kill blokada");
        }
    } else {
        printf("%s[BLAD] Brak aktywnego pracownika w sektorze %d.%s\n",
               KOLOR_CZERWONY, nr_sektora, KOLOR_RESET);
    }
}

void wyslij_odblokowanie() {
    int nr_sektora = bezpieczny_scanf_int(
        "Podaj numer sektora do odblokowania (0-7): ",
        0,
        LICZBA_SEKTOROW - 1
    );

    if (nr_sektora < 0) {
        printf("Anulowano.\n");
        return;
    }

    if (!stan_hali->sektor_zablokowany[nr_sektora]) {
        printf("%s[UWAGA] Sektor %d nie jest zablokowany.%s\n",
               KOLOR_ZOLTY, nr_sektora, KOLOR_RESET);
        return;
    }

    pid_t pid = stan_hali->pidy_pracownikow[nr_sektora];
    if (pid > 0) {
        if (kill(pid, SYGNAL_ODBLOKOWANIE_SEKTORA) == 0) {
            printf("%s[OK] Wyslano sygnal ODBLOKOWANIE do pracownika sektora %d (PID: %d)%s\n",
                   KOLOR_ZIELONY, nr_sektora, pid, KOLOR_RESET);
            rejestr_log("KIEROWNIK", "Wyslano odblokowanie do sektora %d", nr_sektora);
        } else {
            perror("kill odblokowanie");
        }
    } else {
        printf("%s[BLAD] Brak aktywnego pracownika w sektorze %d.%s\n",
               KOLOR_CZERWONY, nr_sektora, KOLOR_RESET);
    }
}

void zarzadzaj_ewakuacja() {
    if (stan_hali->ewakuacja_trwa) {
        printf("%s[UWAGA] Ewakuacja juz trwa!%s\n", KOLOR_ZOLTY, KOLOR_RESET);
        return;
    }

    printf("\n%s", KOLOR_CZERWONY);
    printf("+=======================================+\n");
    printf("|   UWAGA: OGLASZAM EWAKUACJE HALI!     |\n");
    printf("+=======================================+\n");
    printf("%s\n", KOLOR_RESET);

    rejestr_log("KIEROWNIK", "EWAKUACJA HALI OGLOSZONA");

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        stan_hali->sektor_ewakuowany[i] = 0;
    }

    stan_hali->ewakuacja_trwa = 1;

    if (kill(0, SYGNAL_EWAKUACJA) == -1) {
        perror("kill ewakuacja");
    }

    printf("Oczekiwanie na zgloszenia od pracownikow...\n");
}

int main() {
    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = obsluga_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    signal(SYGNAL_EWAKUACJA, SIG_IGN);

    inicjalizuj();

    pthread_t watek_fifo_id;
    if (pthread_create(&watek_fifo_id, NULL, watek_fifo, NULL) != 0) {
        perror("pthread_create fifo");
    } else {
        pthread_detach(watek_fifo_id);
    }

    printf("\n%s", KOLOR_BOLD);
    printf("+=======================================+\n");
    printf("|        PANEL KIEROWNIKA HALI          |\n");
    printf("+=======================================+\n");
    printf("|  PID: %-10d                      |\n", getpid());
    printf("+=======================================+\n");
    printf("%s", KOLOR_RESET);

    stan_hali->pid_kierownika = getpid();
    rejestr_log("KIEROWNIK", "Start PID %d", getpid());

    while (1) {
        printf("\n+-------------------------------+\n");
        printf("|        MENU KIEROWNIKA        |\n");
        printf("+-------------------------------+\n");
        printf("|  1. Zablokuj sektor           |\n");
        printf("|  2. Odblokuj sektor           |\n");
        printf("|  3. Oglos ewakuacje           |\n");
        printf("|  4. Pokaz status              |\n");
        printf("|  5. Wyjscie                   |\n");
        printf("+-------------------------------+\n");

        int opcja = bezpieczny_scanf_int("Wybierz opcje (1-5): ", 1, 5);

        if (opcja < 0) {
            printf("\nKierownik: Koniec wejscia. Zamykanie...\n");
            break;
        }

        switch (opcja) {
            case 1:
                wyslij_blokade();
                break;
            case 2:
                wyslij_odblokowanie();
                break;
            case 3:
                zarzadzaj_ewakuacja();
                break;
            case 4:
                pokaz_status();
                break;
            case 5:
                printf("Kierownik: Do widzenia!\n");
                rejestr_log("KIEROWNIK", "Zakonczenie pracy");
                exit(0);
            default:
                printf("Nieznana opcja.\n");
        }

    }

    return 0;
}