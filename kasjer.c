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

    rejestr_init(NULL);
}

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

void aktualizuj_status() {
    if (stan_hali->wszystkie_bilety_sprzedane) {
        stan_hali->kasa_aktywna[id_kasjera] = 0;
        return;
    }

    if (id_kasjera < 2) {
        stan_hali->kasa_aktywna[id_kasjera] = 1;
        return;
    }

    int N = 0;
    int K = 0;

    for(int i = 0; i < LICZBA_KAS; i++) {
        if(stan_hali->kasa_aktywna[i]) N++;
        K += stan_hali->kolejka_dlugosc[i];
    }

    int limit = stan_hali->pojemnosc_calkowita / 10;
    int potrzebne = (K / limit) + 1;

    if (potrzebne < 2) potrzebne = 2;
    if (potrzebne > LICZBA_KAS) potrzebne = LICZBA_KAS;

    int poprzedni_status = stan_hali->kasa_aktywna[id_kasjera];

    if (K < limit * (N - 1) && id_kasjera >= potrzebne) {
        stan_hali->kasa_aktywna[id_kasjera] = 0;
    } else if (id_kasjera < potrzebne) {
        stan_hali->kasa_aktywna[id_kasjera] = 1;
    }

    if (poprzedni_status != stan_hali->kasa_aktywna[id_kasjera]) {
        rejestr_log("KASJER", "Kasa %d: Status zmieniony na %s",
                   id_kasjera,
                   stan_hali->kasa_aktywna[id_kasjera] ? "AKTYWNA" : "NIEAKTYWNA");
    }
}

void obsluz_klienta() {
    if (!stan_hali->kasa_aktywna[id_kasjera]) {
        return;
    }

    KomunikatBilet zapytanie;
    if (msgrcv(msg_id, &zapytanie, sizeof(KomunikatBilet) - sizeof(long),
               TYP_KOMUNIKATU_ZAPYTANIE, IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) return;
        if (errno == EIDRM || errno == EINVAL) exit(0);
        perror("msgrcv");
        return;
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    semop(sem_id, operacje, 1);

    int znaleziono_sektor = -1;
    int sprzedane_bilety = 0;
    int zadane_bilety = zapytanie.liczba_biletow;

    if (zadane_bilety < 1) zadane_bilety = 1;
    if (zadane_bilety > MAX_BILETOW_NA_KIBICA) zadane_bilety = MAX_BILETOW_NA_KIBICA;

    if (zapytanie.czy_vip) {
        if (stan_hali->liczniki_sektorow[SEKTOR_VIP] + zadane_bilety <= stan_hali->pojemnosc_vip) {
            stan_hali->liczniki_sektorow[SEKTOR_VIP] += zadane_bilety;
            znaleziono_sektor = SEKTOR_VIP;
            sprzedane_bilety = zadane_bilety;
        }
    } else {
        int start_sektor = rand() % LICZBA_SEKTOROW;

        for (int i = 0; i < LICZBA_SEKTOROW; i++) {
            int idx = (start_sektor + i) % LICZBA_SEKTOROW;
            if (stan_hali->liczniki_sektorow[idx] + zadane_bilety <= stan_hali->pojemnosc_sektora) {
                stan_hali->liczniki_sektorow[idx] += zadane_bilety;
                znaleziono_sektor = idx;
                sprzedane_bilety = zadane_bilety;
                break;
            }
        }

        if (znaleziono_sektor == -1) {
            for (int i = 0; i < LICZBA_SEKTOROW; i++) {
                int idx = (start_sektor + i) % LICZBA_SEKTOROW;
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

    if (sprawdz_czy_wyprzedane()) {
        stan_hali->wszystkie_bilety_sprzedane = 1;
        rejestr_log("KASJER", "Wszystkie bilety sprzedane - zamykanie kas");
        printf("%sKasjer %d: WSZYSTKIE BILETY SPRZEDANE - zamykam kasy%s\n",
               KOLOR_ZOLTY, id_kasjera, KOLOR_RESET);
    }

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

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

    srand(time(NULL) ^ (getpid() << 16));
    inicjalizuj();

    stan_hali->pidy_kasjerow[id_kasjera] = getpid();
    aktualizuj_status();

    printf("Kasjer %d gotowy (PID: %d) %s\n",
           id_kasjera,
           getpid(),
           stan_hali->kasa_aktywna[id_kasjera] ? "[AKTYWNA]" : "[NIEAKTYWNA]");
    rejestr_log("KASJER", "Kasa %d: Start PID %d", id_kasjera, getpid());

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja - zamykam kase.\n", id_kasjera);
            rejestr_log("KASJER", "Kasa %d: Zamknieta - ewakuacja", id_kasjera);
            break;
        }

        if (stan_hali->wszystkie_bilety_sprzedane) {
            if (stan_hali->kasa_aktywna[id_kasjera]) {
                stan_hali->kasa_aktywna[id_kasjera] = 0;
                printf("Kasjer %d: Wszystkie bilety sprzedane - zamykam kase.\n", id_kasjera);
                rejestr_log("KASJER", "Kasa %d: Zamknieta - wyprzedane", id_kasjera);
            }
            sleep(1);
            continue;
        }

        aktualizuj_status();
        obsluz_klienta();
        usleep(100000);
    }

    return 0;
}