#include "common.h"
#include <time.h>

int shm_id = -1;
int msg_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;

int moja_druzyna = 0;
int jestem_vip = 0;
int ma_bilet = 0;
int numer_sektora = -1;
int mam_noz = 0;

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

void sprobuj_kupic_bilet() {
    if (!jestem_vip) aktualizuj_kolejke(1);

    KomunikatBilet msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = TYP_KOMUNIKATU_ZAPYTANIE;
    msg.pid_kibica = getpid();
    msg.id_druzyny = moja_druzyna;
    msg.czy_vip = jestem_vip;
    msg.nr_sektora = -1; 

    if (msgsnd(msg_id, &msg, sizeof(KomunikatBilet) - sizeof(long), 0) == -1) {
        if (!jestem_vip) aktualizuj_kolejke(-1);
        return;
    }

    OdpowiedzBilet odp;
    if (msgrcv(msg_id, &odp, sizeof(OdpowiedzBilet) - sizeof(long), getpid(), 0) == -1) {
        if (!jestem_vip) aktualizuj_kolejke(-1);
        return;
    }

    if (!jestem_vip) aktualizuj_kolejke(-1);

    if (odp.czy_sukces) {
        ma_bilet = 1;
        numer_sektora = odp.przydzielony_sektor;
        printf("Kibic %d (Druzyna %c): Kupilem bilet do sektora %d.\n", 
               getpid(), (moja_druzyna == DRUZYNA_A) ? 'A' : 'B', numer_sektora);
    } else {
        printf("Kibic %d: Brak biletow.\n", getpid());
    }
}

void idz_do_bramki() {
    if (!ma_bilet) return;
    sleep(1);
    
    if (stan_hali->sektor_zablokowany[numer_sektora]) {
        printf("Kibic %d: Sektor %d zablokowany. Czekam...\n", getpid(), numer_sektora);
        while (stan_hali->sektor_zablokowany[numer_sektora]) {
            if (stan_hali->ewakuacja_trwa) return;
            sleep(1);
        }
    }

    int proba_vip = jestem_vip;
    int licznik_frustracji = 0;

    if (proba_vip) {
        printf("Kibic %d (VIP): Wchodze wejsciem dla VIP-ow.\n", getpid());
    } else {
        int wybrane_stanowisko = rand() % 2;
        int moje_miejsce = -1;

        printf("Kibic %d: Ide do stanowiska %d w sektorze %d.\n", getpid(), wybrane_stanowisko, numer_sektora);

        while (moje_miejsce == -1) {
            if (stan_hali->ewakuacja_trwa) return;
            
            struct sembuf operacje[1];
            operacje[0].sem_num = 0;
            operacje[0].sem_op = -1; 
            operacje[0].sem_flg = 0;
            if (semop(sem_id, operacje, 1) == -1) return;

            Bramka *b = &stan_hali->bramki[numer_sektora][wybrane_stanowisko];
            
            int wolne = 0;
            for(int i=0; i<3; i++) if(b->miejsca[i].pid_kibica == 0) wolne++;

            if (wolne > 0 && (b->obecna_druzyna == 0 || b->obecna_druzyna == moja_druzyna)) {
                for (int i = 0; i < 3; i++) {
                    if (b->miejsca[i].pid_kibica == 0) {
                        b->miejsca[i].pid_kibica = getpid();
                        b->miejsca[i].druzyna = moja_druzyna;
                        b->miejsca[i].ma_przedmiot = mam_noz;
                        b->miejsca[i].zgoda_na_wejscie = 0;
                        moje_miejsce = i;
                        break;
                    }
                }
            } else {
                licznik_frustracji++;
                if (licznik_frustracji > 10) {
                    printf("Kibic %d: FRUSTRACJA! Wpycham sie jako VIP (AGRESJA)!\n", getpid());
                    proba_vip = 1;
                    operacje[0].sem_op = 1;
                    semop(sem_id, operacje, 1);
                    break;
                }
            }
            
            operacje[0].sem_op = 1;
            semop(sem_id, operacje, 1);

            if (moje_miejsce == -1) {
                usleep(100000); 
            }
        }

        if (!proba_vip) {
            printf("Kibic %d: Czekam na kontrole na stanowisku %d...\n", getpid(), wybrane_stanowisko);
            while (1) {
                if (stan_hali->ewakuacja_trwa) return;
                
                if (stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].pid_kibica == 0) {
                     if (stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].zgoda_na_wejscie == 2) {
                        printf("Kibic %d: Zostalem wyrzucony przez ochrone!\n", getpid());
                        struct sembuf operacje[1];
                        operacje[0].sem_num = 0; operacje[0].sem_op = -1; operacje[0].sem_flg = 0;
                        semop(sem_id, operacje, 1);
                        
                        stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].pid_kibica = 0;
                        stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].zgoda_na_wejscie = 0;
                        stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].druzyna = 0;
                        
                        operacje[0].sem_op = 1;
                        semop(sem_id, operacje, 1);
                        return;
                     }
                }

                if (stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].zgoda_na_wejscie == 1) {
                    break;
                }
                usleep(50000);
            }
            
            struct sembuf operacje[1];
            operacje[0].sem_num = 0;
            operacje[0].sem_op = -1;
            operacje[0].sem_flg = 0;
            semop(sem_id, operacje, 1);
            
            stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].pid_kibica = 0;
            stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].zgoda_na_wejscie = 0;
            stan_hali->bramki[numer_sektora][wybrane_stanowisko].miejsca[moje_miejsce].druzyna = 0;
            
            operacje[0].sem_op = 1;
            semop(sem_id, operacje, 1);
        }
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;
    semop(sem_id, operacje, 1);
    
    stan_hali->suma_kibicow_w_hali++;
    
    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

    printf("Kibic %d: Wszedlem na sektor %d. Ogladam mecz...\n", getpid(), numer_sektora);
    sleep(5); 

    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    semop(sem_id, operacje, 1);
    
    stan_hali->suma_kibicow_w_hali--;
    
    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

    printf("Kibic %d: Wychodze.\n", getpid());
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    srand(time(NULL) ^ (getpid() << 16));
    if (atexit(obsluga_wyjscia) != 0) exit(EXIT_FAILURE);
    inicjalizuj();
    if (stan_hali->ewakuacja_trwa) return 0;
    moja_druzyna = (rand() % 2) ? DRUZYNA_A : DRUZYNA_B;
    jestem_vip = ((rand() % 100) < 1);
    mam_noz = ((rand() % 100) < SZANSA_NA_PRZEDMIOT);
    printf("Kibic %d: Start. Druzyna: %c, VIP: %s, Noz: %s\n", 
           getpid(), (moja_druzyna == DRUZYNA_A) ? 'A' : 'B', jestem_vip ? "TAK" : "NIE", mam_noz ? "TAK" : "NIE");
    sprobuj_kupic_bilet();
    if (ma_bilet) idz_do_bramki();
    return 0;
}