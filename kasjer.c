/*
 * KASJER.C - Proces kasjera
 *
 * Odpowiedzialnosci:
 * - Sprzedaz biletow przez kolejke komunikatow
 * - Dynamiczne wlaczanie/wylaczanie kas
 * - Zamykanie kas przy wyprzedaniu/ewakuacji
 */
#include "common.h"

volatile sig_atomic_t kasjer_dziala = 1; // Flaga pracy petli glownej

/* Zatrzymanie pracy kasjera (SIGTERM i ewakuacja) */
void handler_term(int sig) {
    (void)sig;
    kasjer_dziala = 0;
}

int id_kasjera = -1;     // Numer kasy (0..LICZBA_KAS-1)
int shm_id = -1;         // ID pamieci dzielonej
int msg_id = -1;         // ID kolejki komunikatow
int sem_id = -1;         // ID semaforow
StanHali *stan_hali = NULL; // Wskaznik na stan hali

/* Sprzatanie przy wyjsciu procesu */
void obsluga_wyjscia() {
    if (stan_hali != NULL && id_kasjera >= 0) {
        stan_hali->pidy_kasjerow[id_kasjera] = 0;
        stan_hali->kasa_aktywna[id_kasjera] = 0;
        if (shmdt(stan_hali) == -1) {
            perror("shmdt kasjer");
        }
    }
}

/* Podlaczenie do zasobow IPC */
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

    sem_id = semget(klucz_sem, SEM_TOTAL, 0600);
    SPRAWDZ(sem_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    rejestr_init(NULL, 0);
}

/* Sprawdzenie, czy wszystkie bilety zostaly sprzedane */
int sprawdz_czy_wyprzedane() {
    int suma_sprzedanych = 0;
    int suma_pojemnosci = 0;

    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        suma_sprzedanych += stan_hali->liczniki_sektorow[i];
        suma_pojemnosci += stan_hali->pojemnosc_sektora;
    }

    suma_sprzedanych += stan_hali->liczniki_sektorow[SEKTOR_VIP];
    suma_pojemnosci += stan_hali->pojemnosc_vip;

    return suma_sprzedanych >= suma_pojemnosci;
}

/* Aktualizacja aktywnych kas na podstawie dlugosci kolejek */
void aktualizuj_status() {
    struct sembuf lock = {0, -1, 0};
    struct sembuf unlock = {0, 1, 0};

    // Sekcja krytyczna: zmiana statusu kas
    if (semop(sem_id, &lock, 1) == -1) {
        if (errno != EINTR) perror("semop lock kasa");
        return;
    }

    if (stan_hali->wszystkie_bilety_sprzedane) {
        // Wyprzedane: kasjer przechodzi w stan nieaktywny
        stan_hali->kasa_aktywna[id_kasjera] = 0;
        stan_hali->kasa_zamykanie[id_kasjera] = 0;
        if (semop(sem_id, &unlock, 1) == -1) {
            perror("semop unlock kasa");
        }
        return;
    }

    if (stan_hali->kasa_zamykanie[id_kasjera] &&
        stan_hali->kolejka_dlugosc[id_kasjera] == 0) {
        // Zamykanie: kolejka pusta, mozna wylaczyc kase
        stan_hali->kasa_aktywna[id_kasjera] = 0;
        stan_hali->kasa_zamykanie[id_kasjera] = 0;
    }

    int poprzedni_status = stan_hali->kasa_aktywna[id_kasjera];
    if (id_kasjera == 0) {
        // Kasa 0 steruje liczba aktywnych kas
        int K = 0;
        for (int i = 0; i < LICZBA_KAS; i++) {
            K += stan_hali->kolejka_dlugosc[i];
        }

        // Limit osob na kase (min 1)
        int limit = stan_hali->pojemnosc_calkowita / 10;
        if (limit <= 0) limit = 1;

        int potrzebne = (K + limit - 1) / limit;
        if (potrzebne < 2) potrzebne = 2;
        if (potrzebne > LICZBA_KAS) potrzebne = LICZBA_KAS;

        int N = stan_hali->aktywne_kasy;
        if (N < 2) N = 2;
        if (N > LICZBA_KAS) N = LICZBA_KAS;

        if (potrzebne > N) {
            // Zwieksz liczbe kas
            int reopened = 0;
            for (int i = 0; i < LICZBA_KAS; i++) {
                if (stan_hali->kasa_zamykanie[i]) {
                    stan_hali->kasa_zamykanie[i] = 0;
                    N++;
                    reopened = 1;
                    break;
                }
            }
            if (!reopened) {
                for (int i = 0; i < LICZBA_KAS; i++) {
                    if (!stan_hali->kasa_aktywna[i]) {
                        stan_hali->kasa_aktywna[i] = 1;
                        N++;
                        struct sembuf wake = {SEM_KASA(i), 1, 0};
                        if (semop(sem_id, &wake, 1) == -1) {
                            perror("semop wake kasa");
                        }
                        break;
                    }
                }
            }
        } else if (potrzebne < N && N > 2) {
            // Zmniejsz liczbe kas (oznacz do zamkniecia)
            int zamknieta = -1;
            for (int i = LICZBA_KAS - 1; i >= 0; i--) {
                if (stan_hali->kasa_aktywna[i] && !stan_hali->kasa_zamykanie[i]) {
                    zamknieta = i;
                    break;
                }
            }
            if (zamknieta >= 0) {
                stan_hali->kasa_zamykanie[zamknieta] = 1;
                N--;
            }
        }

        stan_hali->aktywne_kasy = N;
    }

    int nowy_status = stan_hali->kasa_aktywna[id_kasjera];
    if (semop(sem_id, &unlock, 1) == -1) {
        perror("semop unlock kasa");
    }

    if (poprzedni_status != nowy_status) {
        rejestr_log("KASJER", "Kasa %d: Status zmieniony na %s",
                   id_kasjera,
                   nowy_status ? "AKTYWNA" : "NIEAKTYWNA");
    }
}

/* Obsluga pojedynczego zapytania o bilet */
void obsluz_klienta() {
    if (!stan_hali->kasa_aktywna[id_kasjera]) {
        return;
    }

    KomunikatBilet zapytanie;
    long typ = id_kasjera + 1;
    // Odbior zapytania dla tej kasy (mtype = id+1)
    if (msgrcv(msg_id, &zapytanie, sizeof(KomunikatBilet) - sizeof(long),
               typ, 0) == -1) {
        if (errno == EINTR) return;
        if (errno == EIDRM || errno == EINVAL) exit(0);
        perror("msgrcv");
        return;
    }

    if (zapytanie.pid_kibica == 0 && zapytanie.liczba_biletow == 0) {
        // Pusty komunikat - pomijamy
        return;
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    // Sekcja krytyczna: sprzedaz biletow
    if (semop(sem_id, operacje, 1) == -1) {
        if (errno != EINTR) perror("semop lock sprzedaz");
        return;
    }

    int znaleziono_sektor = -1;
    int sprzedane_bilety = 0;
    int zadane_bilety = zapytanie.liczba_biletow;

    if (zadane_bilety < 1) zadane_bilety = 1;
    if (zadane_bilety > MAX_BILETOW_NA_KIBICA) zadane_bilety = MAX_BILETOW_NA_KIBICA;

    if (zapytanie.czy_vip) {
        // VIP ma osobna pule miejsc
        if (stan_hali->liczniki_sektorow[SEKTOR_VIP] + zadane_bilety <= stan_hali->pojemnosc_vip) {
            stan_hali->liczniki_sektorow[SEKTOR_VIP] += zadane_bilety;
            znaleziono_sektor = SEKTOR_VIP;
            sprzedane_bilety = zadane_bilety;
        }
    } else {
        // Zwykly kibic: szukaj sektora z wolnymi miejscami
        int start_sektor = rand() % LICZBA_SEKTOROW;

        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            int idx = (start_sektor + i) % LICZBA_SEKTOROW;
            if (stan_hali->sektor_zablokowany[idx]) continue;
            if (stan_hali->liczniki_sektorow[idx] + zadane_bilety <= stan_hali->pojemnosc_sektora) {
                stan_hali->liczniki_sektorow[idx] += zadane_bilety;
                znaleziono_sektor = idx;
                sprzedane_bilety = zadane_bilety;
                break;
            }
        }

        if (znaleziono_sektor == -1) {
            // Brak miejsca na pelny pakiet - sprzedaj resztki
            for (int i = 0; i < LICZBA_SEKTOROW; i++) {
                int idx = (start_sektor + i) % LICZBA_SEKTOROW;
                if (stan_hali->sektor_zablokowany[idx]) continue;
                int wolne = stan_hali->pojemnosc_sektora - stan_hali->liczniki_sektorow[idx];
                if (wolne > 0) {
                    stan_hali->liczniki_sektorow[idx] += wolne;
                    znaleziono_sektor = idx;
                    sprzedane_bilety = wolne;
                    break;
                }
            }
        }
    }

    if (sprawdz_czy_wyprzedane() && !stan_hali->wszystkie_bilety_sprzedane) {
        // Ustaw globalny stan wyprzedania
        stan_hali->wszystkie_bilety_sprzedane = 1;
        stan_hali->aktywne_kasy = 0;
        for (int i = 0; i < LICZBA_KAS; i++) {
            stan_hali->kasa_aktywna[i] = 0;
        }
        rejestr_log("KASJER", "Wszystkie bilety sprzedane - zamykanie kas");
        printf("%sKasjer %d: WSZYSTKIE BILETY SPRZEDANE - zamykam kasy%s\n",
               KOLOR_ZOLTY, id_kasjera, KOLOR_RESET);
        for (int i = 0; i < LICZBA_KAS; i++) {
            struct sembuf wake = {SEM_KASA(i), 1, 0};
            if (semop(sem_id, &wake, 1) == -1) {
                perror("semop wake kasa wyprzedane");
            }
        }
    }

    // Zwolnij sekcje krytyczna sprzedazy
    operacje[0].sem_op = 1;
    if (semop(sem_id, operacje, 1) == -1) {
        perror("semop unlock sprzedaz");
    }

    // Odpowiedz do kibica (mtype = PID)
    OdpowiedzBilet odpowiedz;
    memset(&odpowiedz, 0, sizeof(odpowiedz));
    odpowiedz.mtype = zapytanie.pid_kibica;
    odpowiedz.przydzielony_sektor = znaleziono_sektor;
    odpowiedz.liczba_sprzedanych = sprzedane_bilety;
    odpowiedz.czy_sukces = (znaleziono_sektor != -1) ? 1 : 0;

    if (msgsnd(msg_id, &odpowiedz, sizeof(OdpowiedzBilet) - sizeof(long), 0) == -1) {
        if (errno == EIDRM || errno == EINVAL) exit(0);
        perror("msgsnd");
    } else {
        if (odpowiedz.czy_sukces) {
            char *typ_sektora = (znaleziono_sektor == SEKTOR_VIP) ? "VIP" : "zwykly";
            printf("%sKasjer %d: Sprzedano %d bilet(y) (%s sektor %d) dla PID %d %s%s\n",
                   KOLOR_ZIELONY,
                   id_kasjera,
                   sprzedane_bilety,
                   typ_sektora,
                   odpowiedz.przydzielony_sektor,
                   zapytanie.pid_kibica,
                   zapytanie.czy_vip ? "[VIP]" : "",
                   KOLOR_RESET);
            rejestr_log("SPRZEDAZ", "Kasa %d: %d bilet(y) sektor %d dla PID %d %s",
                       id_kasjera, sprzedane_bilety, odpowiedz.przydzielony_sektor,
                       zapytanie.pid_kibica, zapytanie.czy_vip ? "VIP" : "");
        } else {
            printf("%sKasjer %d: Brak miejsc dla PID %d%s\n",
                   KOLOR_CZERWONY,
                   id_kasjera,
                   zapytanie.pid_kibica,
                   KOLOR_RESET);
            rejestr_log("SPRZEDAZ", "Kasa %d: Brak miejsc dla PID %d", id_kasjera, zapytanie.pid_kibica);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uzycie: %s <nr_kasjera>\n", argv[0]);
        fprintf(stderr, "  nr_kasjera: 0-%d\n", LICZBA_KAS - 1);
        exit(EXIT_FAILURE);
    }

    id_kasjera = parsuj_int(argv[1], "numer kasjera", 0, LICZBA_KAS - 1);

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL) ^ (getpid() << 16));  // Losowe ziarno
    inicjalizuj();

    // Rejestracja obslugi sygnalow (ten sam handler dla SIGTERM i ewakuacji)
    struct sigaction sa;
    sa.sa_handler = handler_term;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SYGNAL_EWAKUACJA, &sa, NULL);

    stan_hali->pidy_kasjerow[id_kasjera] = getpid(); // Rejestr PID kasjera
    aktualizuj_status();

    printf("Kasjer %d gotowy (PID: %d) %s\n",
           id_kasjera,
           getpid(),
           stan_hali->kasa_aktywna[id_kasjera] ? "[AKTYWNA]" : "[NIEAKTYWNA]");
    rejestr_log("KASJER", "Kasa %d: Start PID %d", id_kasjera, getpid());

    while (kasjer_dziala) {
        // Ewakuacja zamyka kase
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja - zamykam kase.\n", id_kasjera);
            rejestr_log("KASJER", "Kasa %d: Zamknieta - ewakuacja", id_kasjera);
            break;
        }

        if (stan_hali->wszystkie_bilety_sprzedane) {
            // Wyprzedane - opuszczamy petle
            if (stan_hali->kasa_aktywna[id_kasjera]) {
                stan_hali->kasa_aktywna[id_kasjera] = 0;
                printf("Kasjer %d: Wszystkie bilety sprzedane - zamykam kase.\n", id_kasjera);
                rejestr_log("KASJER", "Kasa %d: Zamknieta - wyprzedane", id_kasjera);
            }
            break;
        }

        // Aktualizacja stanu i obsluga klienta
        aktualizuj_status();

        if (!stan_hali->kasa_aktywna[id_kasjera]) {
            struct sembuf wait_kasa = {SEM_KASA(id_kasjera), -1, 0};
            if (semop(sem_id, &wait_kasa, 1) == -1) {
                if (errno == EINTR) continue;
                perror("semop wait kasa");
                break;
            }
            continue;
        }

        obsluz_klienta();
    }

    return 0;
}