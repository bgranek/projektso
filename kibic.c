#include "common.h"
#include <time.h>

int shm_id = -1;
int msg_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;

int moja_druzyna = 0;
int jestem_vip = 0;

void obsluga_wyjscia() {
    if (stan_hali != NULL) {
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
    if (shm_id == -1) exit(0);

    msg_id = msgget(klucz_msg, 0600);
    if (msg_id == -1) exit(0);

    sem_id = semget(klucz_sem, 2, 0600);
    if (sem_id == -1) exit(0);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat kibic");
        exit(EXIT_FAILURE);
    }
}

void aktualizuj_kolejke(int zmiana) {
    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    
    if (semop(sem_id, operacje, 1) == -1) return;

    if (zmiana > 0) {
        int wybrana_kasa = -1;
        int min_dlugosc = 100000;

        for (int i = 0; i < LICZBA_KAS; i++) {
            if (stan_hali->kasa_aktywna[i]) {
                if (stan_hali->kolejka_dlugosc[i] < min_dlugosc) {
                    min_dlugosc = stan_hali->kolejka_dlugosc[i];
                    wybrana_kasa = i;
                }
            }
        }

        if (wybrana_kasa != -1) {
            stan_hali->kolejka_dlugosc[wybrana_kasa]++;
        }
    } else {
        for (int i = 0; i < LICZBA_KAS; i++) {
             if (stan_hali->kolejka_dlugosc[i] > 0) {
                 stan_hali->kolejka_dlugosc[i]--;
                 break;
             }
        }
    }

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    srand(time(NULL) ^ (getpid() << 16));

    if (atexit(obsluga_wyjscia) != 0) {
        perror("atexit");
        exit(EXIT_FAILURE);
    }

    inicjalizuj();

    if (stan_hali->ewakuacja_trwa) {
        return 0;
    }

    moja_druzyna = (rand() % 2) ? DRUZYNA_A : DRUZYNA_B;
    jestem_vip = ((rand() % 100) < 1);

    printf("Kibic %d: Start. Druzyna: %c, VIP: %s\n", 
           getpid(), 
           (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
           jestem_vip ? "TAK" : "NIE");

    return 0;
}