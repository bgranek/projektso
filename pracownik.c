#include "common.h"

int id_sektora = -1;
int shm_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;
volatile sig_atomic_t ewakuacja_zgloszono = 0;
volatile sig_atomic_t praca_trwa = 1;

pthread_t watki_stanowisk[2];
pthread_mutex_t mutex_sektor = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cond_sektor = PTHREAD_COND_INITIALIZER;

typedef struct {
    int id_stanowiska;
    int id_sektora;
} DaneWatku;

void obsluga_wyjscia() {
    praca_trwa = 0;
    
    pthread_cond_broadcast(&cond_sektor);
    
    for (int i = 0; i < 2; i++) {
        pthread_join(watki_stanowisk[i], NULL);
    }
    
    pthread_mutex_destroy(&mutex_sektor);
    pthread_cond_destroy(&cond_sektor);
    
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->pidy_pracownikow[id_sektora] = 0;
        shmdt(stan_hali);
    }
}

void handler_blokada(int sig) {
    (void)sig;
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->sektor_zablokowany[id_sektora] = 1;
        const char *msg = "[BLOKADA] SEKTOR ZABLOKOWANY\n";
        write(STDOUT_FILENO, msg, strlen(msg));
    }
}

void handler_odblokowanie(int sig) {
    (void)sig;
    if (stan_hali != NULL && id_sektora >= 0) {
        stan_hali->sektor_zablokowany[id_sektora] = 0;
        pthread_cond_broadcast(&cond_sektor);
        const char *msg = "[ODBLOKOWANIE] SEKTOR ODBLOKOWANY\n";
        write(STDOUT_FILENO, msg, strlen(msg));
    }
}

void handler_ewakuacja(int sig) {
    (void)sig;
    const char *msg = "[EWAKUACJA] Otwieram bramki awaryjne.\n";
    write(STDOUT_FILENO, msg, strlen(msg));
    pthread_cond_broadcast(&cond_sektor);
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);
    key_t klucz_sem = ftok(".", KLUCZ_SEM);
    SPRAWDZ(klucz_sem);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

    sem_id = semget(klucz_sem, 2, 0600);
    SPRAWDZ(sem_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
    
    if (rejestr_init(NULL) == -1) {
        fprintf(stderr, "Pracownik %d: Nie udalo sie otworzyc rejestru\n", id_sektora);
    }
}

void rejestruj_sygnaly() {
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = handler_blokada;
    sigaction(SYGNAL_BLOKADA_SEKTORA, &sa, NULL);

    sa.sa_handler = handler_odblokowanie;
    sigaction(SYGNAL_ODBLOKOWANIE_SEKTORA, &sa, NULL);

    sa.sa_handler = handler_ewakuacja;
    sigaction(SYGNAL_EWAKUACJA, &sa, NULL);
}

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

    close(fd);
}

void obsluguj_ewakuacje() {
    if (!stan_hali->ewakuacja_trwa || ewakuacja_zgloszono) {
        return;
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;

    if (semop(sem_id, operacje, 1) == -1) return;

    int liczba_osob = stan_hali->liczniki_sektorow[id_sektora];

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

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
    } else {
        printf("Pracownik %d: Ewakuacja w toku, pozostalo %d osob w sektorze\n",
               id_sektora, liczba_osob);
    }
}

int sprawdz_rodzine_dziecka(pid_t pid_dziecka) {
    RejestrRodzin *rej = &stan_hali->rejestr_rodzin;

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

void obsluz_stanowisko(int nr_stanowiska) {
    Bramka *b = &stan_hali->bramki[id_sektora][nr_stanowiska];

    int liczba_zajetych = 0;
    for (int i = 0; i < 3; i++) {
        if (b->miejsca[i].pid_kibica != 0) {
            liczba_zajetych++;
        }
    }

    if (liczba_zajetych == 0) {
        b->obecna_druzyna = 0;
    }

    for (int i = 0; i < 3; i++) {
        if (b->miejsca[i].pid_kibica != 0 && b->miejsca[i].zgoda_na_wejscie == 0) {
            if (b->miejsca[i].ma_przedmiot) {
                printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Posiada noz!%s\n",
                       KOLOR_CZERWONY,
                       id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                       KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: Zatrzymano PID %d - noz",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica);
                b->miejsca[i].zgoda_na_wejscie = 2;
                continue;
            }

            if (b->miejsca[i].wiek < 15) {
                if (!sprawdz_rodzine_dziecka(b->miejsca[i].pid_kibica)) {
                    printf("%sPracownik %d (Stanowisko %d): ZATRZYMANO PID %d - Wiek %d < 15 bez opiekuna%s\n",
                           KOLOR_ZOLTY,
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, b->miejsca[i].wiek,
                           KOLOR_RESET);
                    rejestr_log("KONTROLA", "Sektor %d Stan %d: Zatrzymano PID %d - wiek %d bez opiekuna",
                               id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, b->miejsca[i].wiek);
                    b->miejsca[i].zgoda_na_wejscie = 3;
                    continue;
                }
                printf("%sPracownik %d (Stanowisko %d): Dziecko PID %d ma opiekuna - OK%s\n",
                       KOLOR_CYAN, id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica, KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: Dziecko PID %d z opiekunem",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica);
            }

            if (b->obecna_druzyna == 0 || b->obecna_druzyna == b->miejsca[i].druzyna) {
                b->obecna_druzyna = b->miejsca[i].druzyna;
                b->miejsca[i].zgoda_na_wejscie = 1;

                printf("%sPracownik %d (Stanowisko %d): Wpuszczam PID %d (Druzyna %c, Wiek %d)%s\n",
                       KOLOR_ZIELONY,
                       id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                       (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B', b->miejsca[i].wiek,
                       KOLOR_RESET);
                rejestr_log("KONTROLA", "Sektor %d Stan %d: Wpuszczono PID %d druzyna %c",
                           id_sektora, nr_stanowiska, b->miejsca[i].pid_kibica,
                           (b->obecna_druzyna == DRUZYNA_A) ? 'A' : 'B');
            }
        }
    }
}

void* watek_stanowiska(void *arg) {
    DaneWatku *dane = (DaneWatku*)arg;
    int nr_stanowiska = dane->id_stanowiska;
    
    printf("Pracownik %d: Watek stanowiska %d uruchomiony\n", id_sektora, nr_stanowiska);
    rejestr_log("PRACOWNIK", "Sektor %d: Watek stanowiska %d uruchomiony", id_sektora, nr_stanowiska);
    
    while (praca_trwa) {
        pthread_mutex_lock(&mutex_sektor);
        
        while (stan_hali->sektor_zablokowany[id_sektora] && praca_trwa && !stan_hali->ewakuacja_trwa) {
            pthread_cond_wait(&cond_sektor, &mutex_sektor);
        }
        
        pthread_mutex_unlock(&mutex_sektor);
        
        if (!praca_trwa) break;
        
        if (stan_hali->ewakuacja_trwa) {
            usleep(500000);
            continue;
        }
        
        struct sembuf operacje[1];
        operacje[0].sem_num = 0;
        operacje[0].sem_op = -1;
        operacje[0].sem_flg = 0;
        
        if (semop(sem_id, operacje, 1) == -1) {
            if (errno == EINTR) continue;
            break;
        }
        
        obsluz_stanowisko(nr_stanowiska);
        
        operacje[0].sem_op = 1;
        semop(sem_id, operacje, 1);
        
        usleep(200000);
    }
    
    printf("Pracownik %d: Watek stanowiska %d zakonczony\n", id_sektora, nr_stanowiska);
    rejestr_log("PRACOWNIK", "Sektor %d: Watek stanowiska %d zakonczony", id_sektora, nr_stanowiska);
    
    free(dane);
    return NULL;
}

void uruchom_watki_stanowisk() {
    for (int i = 0; i < 2; i++) {
        DaneWatku *dane = malloc(sizeof(DaneWatku));
        if (dane == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }
        dane->id_stanowiska = i;
        dane->id_sektora = id_sektora;
        
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

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_nsec ^ getpid()));

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();
    rejestruj_sygnaly();

    stan_hali->pidy_pracownikow[id_sektora] = getpid();

    printf("Pracownik sektora %d gotowy (PID: %d). Uruchamiam 2 watki stanowisk.\n",
           id_sektora, getpid());
    rejestr_log("PRACOWNIK", "Sektor %d: Start PID %d", id_sektora, getpid());

    uruchom_watki_stanowisk();

    while (praca_trwa) {
        if (stan_hali->ewakuacja_trwa) {
            obsluguj_ewakuacje();
        }
        sleep(1);
    }

    return 0;
}