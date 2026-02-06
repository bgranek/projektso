/*
 * PRACOWNIK.C - Proces pracownika sektora
 *
 * Odpowiedzialnosci:
 * - Kontrola kibicow przy stanowiskach
 * - Reakcja na blokady i ewakuacje
 * - Wysylanie zgloszen do kierownika
 */
#include "common.h"

int id_sektora = -1;     // Numer sektora (0-7)
int shm_id = -1;         // ID pamieci dzielonej
int sem_id = -1;         // ID semaforow
StanHali *stan_hali = NULL; // Wskaznik na stan hali
volatile sig_atomic_t ewakuacja_zgloszono = 0; // Czy sektor zgloszyl ewakuacje
int praca_trwa = 1;      // Flaga pracy watkow stanowisk

pthread_t watki_stanowisk[2]; // Dwa stanowiska na sektor
pthread_t watek_sygnalow_id;  // Watek nasluchu sygnalow
pthread_mutex_t mutex_sektor = PTHREAD_MUTEX_INITIALIZER; // Ochrona flag sektorowych
pthread_cond_t cond_sektor = PTHREAD_COND_INITIALIZER;    // Budzenie stanowisk

typedef struct {
    int id_stanowiska;
} DaneWatku;

/* Sprzatanie procesu pracownika */
void obsluga_wyjscia() {
    // Zatrzymaj watki stanowisk
    pthread_mutex_lock(&mutex_sektor);
    praca_trwa = 0;
    pthread_cond_broadcast(&cond_sektor);
    pthread_mutex_unlock(&mutex_sektor);

    // Sprzatanie prymitywow synchronizacji
    pthread_mutex_destroy(&mutex_sektor);
    pthread_cond_destroy(&cond_sektor);

    if (stan_hali != NULL && id_sektora >= 0) {
        // Wyczysc PID pracownika
        stan_hali->pidy_pracownikow[id_sektora] = 0;
        if (shmdt(stan_hali) == -1) {
            perror("shmdt pracownik");
        }
    }
}

/* Zatrzymanie pracy stanowisk (bezpiecznie z sygnalow) */
static void ustaw_praca_stop() {
    // Zatrzymanie pracy watkow (wybudzanie z cond)
    pthread_mutex_lock(&mutex_sektor);
    praca_trwa = 0;
    pthread_cond_broadcast(&cond_sektor);
    pthread_mutex_unlock(&mutex_sektor);
}

/* Ustawienie blokady sektora i pobudzenie stanowisk */
static void ustaw_blokade(int blokada) {
    if (stan_hali != NULL && id_sektora >= 0) {
        // Aktualizacja flagi blokady sektora
        pthread_mutex_lock(&mutex_sektor);
        stan_hali->sektor_zablokowany[id_sektora] = blokada;
        pthread_cond_broadcast(&cond_sektor);
        pthread_mutex_unlock(&mutex_sektor);
    }
}

/* Podlaczenie do zasobow IPC i rejestru */
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
    
    if (rejestr_init(NULL, 0) == -1) {
        fprintf(stderr, "Pracownik %d: Nie udalo sie otworzyc rejestru\n", id_sektora);
    }
}

/* Watek obslugi sygnalow (blokada/ewakuacja/SIGTERM) */
static void* watek_sygnalow(void *arg) {
    (void)arg;
    // Oczekiwane sygnaly w watku sygnalow
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SYGNAL_BLOKADA_SEKTORA);
    sigaddset(&set, SYGNAL_ODBLOKOWANIE_SEKTORA);
    sigaddset(&set, SYGNAL_EWAKUACJA);
    sigaddset(&set, SIGTERM);

    while (1) {
        int sig = sigwaitinfo(&set, NULL);
        if (sig == -1) {
            if (errno == EINTR) continue;
            perror("sigwaitinfo");
            continue;
        }

        if (sig == SYGNAL_BLOKADA_SEKTORA) {
            // Zablokuj sektor
            ustaw_blokade(1);
            const char *msg = "[BLOKADA] SEKTOR ZABLOKOWANY\n";
            write(STDOUT_FILENO, msg, strlen(msg));
        } else if (sig == SYGNAL_ODBLOKOWANIE_SEKTORA) {
            // Odblokuj sektor i wybudz oczekujacych
            ustaw_blokade(0);
            struct sembuf sig_odblok = {SEM_SEKTOR(id_sektora), 100, 0};
            semop_retry_ctx(sem_id, &sig_odblok, 1, "semop sig odblok");
            const char *msg = "[ODBLOKOWANIE] SEKTOR ODBLOKOWANY\n";
            write(STDOUT_FILENO, msg, strlen(msg));
        } else if (sig == SYGNAL_EWAKUACJA) {
            // Ewakuacja - zakoncz prace stanowisk
            const char *msg = "[EWAKUACJA] Otwieram bramki awaryjne.\n";
            write(STDOUT_FILENO, msg, strlen(msg));
            ustaw_praca_stop();
            struct sembuf wake = {SEM_PRACA(id_sektora), 2, 0};
            semop_retry_ctx(sem_id, &wake, 1, "semop wake praca");
            break;
        } else if (sig == SIGTERM) {
            // SIGTERM - zakoncz prace watkow
            ustaw_praca_stop();
            struct sembuf wake = {SEM_PRACA(id_sektora), 2, 0};
            semop_retry_ctx(sem_id, &wake, 1, "semop wake praca");
            break;
        }
    }
    return NULL;
}

/* Uruchomienie watku sygnalow i maskowanie sygnalow w watkach roboczych */
static void uruchom_watek_sygnalow() {
    // Maskuj sygnaly w watkach roboczych - odbiera tylko watek sygnalow
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SYGNAL_BLOKADA_SEKTORA);
    sigaddset(&set, SYGNAL_ODBLOKOWANIE_SEKTORA);
    sigaddset(&set, SYGNAL_EWAKUACJA);
    sigaddset(&set, SIGTERM);

    int rc = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (rc != 0) {
        errno = rc;
        perror("pthread_sigmask");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&watek_sygnalow_id, NULL, watek_sygnalow, NULL) != 0) {
        perror("pthread_create sygnaly");
        exit(EXIT_FAILURE);
    }
}

/* Zgloszenie do kierownika przez FIFO */
void wyslij_zgloszenie_do_kierownika(int typ, const char *wiadomosc) {
    int fd = open(FIFO_PRACOWNIK_KIEROWNIK, O_WRONLY | O_NONBLOCK);
    if (fd == -1) {
        if (errno != ENXIO) {
            perror("open FIFO (pracownik)");
        }
        return;
    }

    KomunikatFifo kom;
    memset(&kom, 0, sizeof(kom));
    kom.typ = typ;
    kom.nr_sektora = id_sektora;
    kom.pid_pracownika = getpid();
    strncpy(kom.wiadomosc, wiadomosc, sizeof(kom.wiadomosc) - 1);

    ssize_t napisano = write(fd, &kom, sizeof(KomunikatFifo));
    if (napisano == -1) {
        perror("write FIFO");
    } else {
        printf("Pracownik %d: Wyslano zgloszenie do kierownika: %s\n",
               id_sektora, wiadomosc);
        rejestr_log("PRACOWNIK", "Sektor %d: Wyslano zgloszenie - %s", id_sektora, wiadomosc);
    }

    if (close(fd) == -1) {
        perror("close FIFO");
    }
}

/* Monitorowanie ewakuacji sektora i wyslanie zgloszenia */
void obsluguj_ewakuacje() {
    if (!stan_hali->ewakuacja_trwa || ewakuacja_zgloszono) {
        return;
    }

    // Czekaj az sektor bedzie pusty i zglos to kierownikowi
    while (!ewakuacja_zgloszono) {
        struct sembuf operacje[1];
        operacje[0].sem_num = 0;
        operacje[0].sem_op = -1;
        operacje[0].sem_flg = 0;

        if (semop_retry_ctx(sem_id, operacje, 1, "semop lock ewakuacja") == -1) return;

        int liczba_osob = stan_hali->osoby_w_sektorze[id_sektora];

        operacje[0].sem_op = 1;
        if (semop_retry_ctx(sem_id, operacje, 1, "semop unlock ewakuacja") == -1) return;

        if (liczba_osob == 0) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Sektor %d calkowicie ewakuowany (0 osob)",
                     id_sektora);

            printf("%sPracownik %d: [OK] %s%s\n",
                   KOLOR_ZIELONY, id_sektora, msg, KOLOR_RESET);

            rejestr_log("PRACOWNIK", msg);
            wyslij_zgloszenie_do_kierownika(1, msg);

            ewakuacja_zgloszono = 1;
            break;
        }

        struct sembuf wait_wyjscie = {SEM_KIBIC_WYSZEDL, -1, 0};
        if (semop_retry_ctx(sem_id, &wait_wyjscie, 1, "semop wait wyszedl") == -1) return;
    }
}

/* Sprawdzenie, czy dziecko ma opiekuna w rejestrze rodzin */
int sprawdz_rodzine_dziecka(pid_t pid_dziecka) {
    RejestrRodzin *rej = &stan_hali->rejestr_rodzin;

    // Sprawdz czy dziecko ma opiekuna w tym sektorze
    for (int i = 0; i < MAX_RODZIN; i++) {
        if (rej->rodziny[i].aktywna &&
            rej->rodziny[i].pid_dziecka == pid_dziecka &&
            rej->rodziny[i].sektor == id_sektora) {

            if (rej->rodziny[i].rodzic_przy_bramce && rej->rodziny[i].dziecko_przy_bramce) {
                return 1;
            }
        }
    }
    return 0;
}

/* Kontrola kibicow na konkretnym stanowisku */
void obsluz_stanowisko(int nr_stanowiska) {
    Bramka *b = &stan_hali->bramki[id_sektora][nr_stanowiska];

    // Liczba zajetych miejsc w stanowisku
    int liczba_zajetych = 0;
    for (int i = 0; i < 3; i++) {
        if (b->miejsca[i].pid_kibica != 0) {
            liczba_zajetych++;
        }
    }

    if (liczba_zajetych == 0) {
        // Gdy pusto - resetuj druzyna
        b->obecna_druzyna = 0;
    }

    for (int i = 0; i < 3; i++) {
        if (b->miejsca[i].pid_kibica != 0 && b->miejsca[i].zgoda_na_wejscie == 0) {
            if (stan_hali->faza_meczu == FAZA_PO_MECZU) {
                // Po meczu - nie wpuszczamy, zwracamy do kolejki
                b->miejsca[i].zgoda_na_wejscie = 5;
                struct sembuf sig = {SEM_SLOT(id_sektora, nr_stanowiska, i), 1, 0};
                semop_retry_ctx(sem_id, &sig, 1, "semop sig slot");
                continue;
            }

            if (b->miejsca[i].ma_przedmiot) {
                // Odrzucenie: noz
                printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Posiada noz!%s\n",
                       KOLOR_CZERWONY,
                       id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                       KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: Zatrzymano PID %d - noz",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica);
                b->miejsca[i].zgoda_na_wejscie = 2;
                struct sembuf sig_noz = {SEM_SLOT(id_sektora, nr_stanowiska, i), 1, 0};
                semop_retry_ctx(sem_id, &sig_noz, 1, "semop sig noz");
                continue;
            }

            if (b->miejsca[i].wiek < 15) {
                // Dziecko bez opiekuna
                if (!sprawdz_rodzine_dziecka(b->miejsca[i].pid_kibica)) {
                    printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Wiek %d < 15 bez opiekuna%s\n",
                           KOLOR_ZOLTY,
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, b->miejsca[i].wiek,
                           KOLOR_RESET);
                    rejestr_log("KONTROLA", "Sektor %d Stan %d: Zatrzymano PID %d - wiek %d bez opiekuna",
                               id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, b->miejsca[i].wiek);
                    b->miejsca[i].zgoda_na_wejscie = 3;
                    struct sembuf sig_wiek = {SEM_SLOT(id_sektora, nr_stanowiska, i), 1, 0};
                    semop_retry_ctx(sem_id, &sig_wiek, 1, "semop sig wiek");
                    continue;
                }
            }

            if (b->obecna_druzyna == 0 || b->obecna_druzyna == b->miejsca[i].druzyna) {
                // Druzyna zgodna - wpusc
                b->obecna_druzyna = b->miejsca[i].druzyna;
                b->miejsca[i].zgoda_na_wejscie = 1;

                if (b->miejsca[i].wiek < 15) {
                    printf("%sPracownik %d (Stanowisko %d): Dziecko PID %d ma opiekuna - OK%s\n",
                           KOLOR_CYAN, id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, KOLOR_RESET);
                    rejestr_log("KONTROLA", "Sektor %d Stan %d: Dziecko PID %d z opiekunem",
                               id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica);
                }

                printf("%sPracownik %d (Stanowisko %d): Wpuszczam PID %d (Druzyna %c, Wiek %d)%s\n",
                       KOLOR_ZIELONY,
                       id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                       (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B', b->miejsca[i].wiek,
                       KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: Wpuszczono PID %d druzyna %c",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                           (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B');
                struct sembuf sig_ok = {SEM_SLOT(id_sektora, nr_stanowiska, i), 1, 0};
                semop_retry_ctx(sem_id, &sig_ok, 1, "semop sig ok");
            } else {
                // Zla druzyna - przepusc z powrotem
                printf("%sPracownik %d (Stanowisko %d): PID %d ma zla druzyne (%c zamiast %c) - wracaj do kolejki!%s\n",
                       KOLOR_ZOLTY,
                       id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                       (b->miejsca[i].druzyna == DRUZYNA_A) ? 'A' : 'B',
                       (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B',
                       KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: PID %d zla druzyna - przepuszczony",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica);
                b->miejsca[i].zgoda_na_wejscie = 4;
                struct sembuf sig_zla = {SEM_SLOT(id_sektora, nr_stanowiska, i), 1, 0};
                semop_retry_ctx(sem_id, &sig_zla, 1, "semop sig zla");
            }
        }
    }
}

/* Watek stanowiska kontroli w sektorze */
void* watek_stanowiska(void *arg) {
    DaneWatku *dane = (DaneWatku*)arg;
    int nr_stanowiska = dane->id_stanowiska;
    
    printf("Pracownik %d: Watek stanowiska %d uruchomiony\n", id_sektora, nr_stanowiska);
    rejestr_log("PRACOWNIK", "Sektor %d: Watek stanowiska %d uruchomiony", id_sektora, nr_stanowiska);
    
    while (1) {
        pthread_mutex_lock(&mutex_sektor);
        
        // Czekaj, gdy sektor zablokowany
        while (stan_hali->sektor_zablokowany[id_sektora] && praca_trwa && !stan_hali->ewakuacja_trwa) {
            pthread_cond_wait(&cond_sektor, &mutex_sektor);
        }
        
        int lokalna_praca = praca_trwa;
        int lokalna_ewak = stan_hali->ewakuacja_trwa;
        pthread_mutex_unlock(&mutex_sektor);
        
        if (!lokalna_praca || lokalna_ewak) break;

        struct sembuf wait_praca = {SEM_PRACA(id_sektora), -1, 0}; // Czekanie na kibica
        if (semop_retry_ctx(sem_id, &wait_praca, 1, "semop wait praca") == -1) break;

        pthread_mutex_lock(&mutex_sektor);
        lokalna_praca = praca_trwa;
        lokalna_ewak = stan_hali->ewakuacja_trwa;
        pthread_mutex_unlock(&mutex_sektor);
        if (!lokalna_praca || lokalna_ewak) break;

        struct sembuf operacje[1];
        operacje[0].sem_num = 0;
        operacje[0].sem_op = -1;
        operacje[0].sem_flg = 0;

        // Sekcja krytyczna dla obslugi stanowiska
        if (semop_retry_ctx(sem_id, operacje, 1, "semop lock obsluga") == -1) break;

        // Obslugujemy OBA stanowiska, bo SEM_PRACA jest wspolny dla sektora
        // i moze obudzic dowolny watek
        obsluz_stanowisko(0);
        obsluz_stanowisko(1);
        
        operacje[0].sem_op = 1;
        semop_retry_ctx(sem_id, operacje, 1, "semop unlock obsluga");
    }
    
    printf("Pracownik %d: Watek stanowiska %d zakonczony\n", id_sektora, nr_stanowiska);
    rejestr_log("PRACOWNIK", "Sektor %d: Watek stanowiska %d zakonczony", id_sektora, nr_stanowiska);
    
    free(dane);
    return NULL;
}

/* Uruchomienie dwoch watkow stanowisk */
void uruchom_watki_stanowisk() {
    for (int i = 0; i < 2; i++) {
        DaneWatku *dane = malloc(sizeof(DaneWatku));
        if (dane == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        dane->id_stanowiska = i;
        
        if (pthread_create(&watki_stanowisk[i], NULL, watek_stanowiska, dane) != 0) {
            perror("pthread_create");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_sektora>\n", argv[0]);
        fprintf(stderr, "  nr_sektora: 0-%d\n", LICZBA_SEKTOROW - 1);
        exit(EXIT_FAILURE);
    }

    id_sektora = parsuj_int(argv[1], "numer sektora", 0, LICZBA_SEKTOROW - 1);

    setlinebuf(stdout); // Ustawienie buforowania liniowego

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_nsec ^ getpid()));

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj(); // Podlaczenie do IPC
    uruchom_watek_sygnalow();

    stan_hali->pidy_pracownikow[id_sektora] = getpid(); // Rejestr PID pracownika

    printf("Pracownik sektora %d gotowy (PID: %d). Uruchamiam 2 watki stanowisk.\n",
           id_sektora, getpid());
    rejestr_log("PRACOWNIK", "Sektor %d: Start PID %d", id_sektora, getpid());

    uruchom_watki_stanowisk(); // Start dwoch stanowisk kontroli

    for (int i = 0; i < 2; i++) {
        pthread_join(watki_stanowisk[i], NULL);
    }

    pthread_join(watek_sygnalow_id, NULL);

    if (stan_hali->ewakuacja_trwa) {
        obsluguj_ewakuacje();
    }

    return 0;
}