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
}

void aktualizuj_status() {
    if (id_kasjera < 2) {
        stan_hali->kasa_aktywna[id_kasjera] = 1;
        return;
    }

    int N = 0;
    int K = 0;
    for(int i=0; i<LICZBA_KAS; i++) {
        if(stan_hali->kasa_aktywna[i]) N++;
        K += stan_hali->kolejka_dlugosc[i];
    }

    int limit = stan_hali->pojemnosc_calkowita / 10;
    int potrzebne = (K / limit) + 1;
    
    if (potrzebne < 2) potrzebne = 2;
    if (potrzebne > LICZBA_KAS) potrzebne = LICZBA_KAS;

    if (id_kasjera < potrzebne) {
        stan_hali->kasa_aktywna[id_kasjera] = 1;
    } else {
        stan_hali->kasa_aktywna[id_kasjera] = 0;
    }
}

void obsluz_klienta() {
    if (!stan_hali->kasa_aktywna[id_kasjera]) {
        return;
    }

    KomunikatBilet zapytanie;
    if (msgrcv(msg_id, &zapytanie, sizeof(KomunikatBilet) - sizeof(long), TYP_KOMUNIKATU_ZAPYTANIE, IPC_NOWAIT) == -1) {
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
    int start_sektor = rand() % LICZBA_SEKTOROW;
    
    for (int i = 0; i < LICZBA_SEKTOROW; i++) {
        int idx = (start_sektor + i) % LICZBA_SEKTOROW;
        if (stan_hali->liczniki_sektorow[idx] < stan_hali->pojemnosc_sektora) {
            stan_hali->liczniki_sektorow[idx]++;
            znaleziono_sektor = idx;
            break;
        }
    }

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

    OdpowiedzBilet odpowiedz;
    memset(&odpowiedz, 0, sizeof(odpowiedz));
    odpowiedz.mtype = zapytanie.pid_kibica;
    odpowiedz.przydzielony_sektor = znaleziono_sektor;
    odpowiedz.czy_sukces = (znaleziono_sektor != -1) ? 1 : 0;

    if (msgsnd(msg_id, &odpowiedz, sizeof(OdpowiedzBilet) - sizeof(long), 0) == -1) {
        if (errno == EIDRM || errno == EINVAL) exit(0);
        perror("msgsnd");
    } else {
        if (odpowiedz.czy_sukces) {
            printf("%sKasjer %d: Sprzedano bilet (Sektor %d) dla PID %d.%s\n", 
                   KOLOR_ZIELONY, id_kasjera, odpowiedz.przydzielony_sektor, zapytanie.pid_kibica, KOLOR_RESET);
        } else {
            printf("%sKasjer %d: Brak miejsc dla PID %d.%s\n", 
                   KOLOR_CZERWONY, id_kasjera, zapytanie.pid_kibica, KOLOR_RESET);
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

    while (1) {
        if (stan_hali->ewakuacja_trwa) {
            printf("Kasjer %d: Ewakuacja - zamykam kase.\n", id_kasjera);
            break;
        }
        aktualizuj_status();
        obsluz_klienta();
        usleep(100000); 
    }

    return 0;
}