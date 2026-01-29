#include "common.h"
#include <time.h>
#include <sys/wait.h>

int shm_id = -1;
int msg_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;

int moja_druzyna = 0;
int jestem_vip = 0;
int ma_bilet = 0;
int numer_sektora = -1;
int mam_noz = 0;
int moj_wiek = 20;
int liczba_biletow = 1;

int jestem_dzieckiem = 0;
int jestem_rodzicem = 0;
int jestem_kolega = 0;
pid_t pid_partnera = 0;
int id_rodziny = -1;

volatile sig_atomic_t ewakuacja_mnie = 0;
int jestem_w_hali = 0;

void handler_ewakuacja(int sig) {
    (void)sig;
    ewakuacja_mnie = 1;
}

void ewakuuj_sie() {
    if (!jestem_w_hali) return;

    printf("%sKibic %d: [EWAKUACJA] Opuszczam hale natychmiast!%s\n",
           KOLOR_CZERWONY, getpid(), KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d ewakuacja z sektora %d", getpid(), numer_sektora);

    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) == -1) return;

    stan_hali->suma_kibicow_w_hali--;
    if (numer_sektora >= 0 && numer_sektora < LICZBA_WSZYSTKICH_SEKTOROW) {
        stan_hali->osoby_w_sektorze[numer_sektora]--;
        if (stan_hali->osoby_w_sektorze[numer_sektora] < 0) {
            stan_hali->osoby_w_sektorze[numer_sektora] = 0;
        }
    }
    if (jestem_vip && numer_sektora == SEKTOR_VIP) {
        stan_hali->liczba_vip--;
    }

    op.sem_op = 1;
    semop(sem_id, &op, 1);

    jestem_w_hali = 0;
    printf("%sKibic %d: Ewakuowalem sie z hali.%s\n",
           KOLOR_ZIELONY, getpid(), KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d ewakuacja zakonczona", getpid());
}

void obsluga_wyjscia() {
    if (stan_hali != NULL) {
        if (jestem_rodzicem && id_rodziny >= 0) {
            stan_hali->rejestr_rodzin.rodziny[id_rodziny].aktywna = 0;
        }
        shmdt(stan_hali);
    }
}

int zarejestruj_rodzine(pid_t pid_rodzica, pid_t pid_dziecka) {
    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) == -1) return -1;

    int idx = -1;
    RejestrRodzin *rej = &stan_hali->rejestr_rodzin;

    for (int i = 0; i < MAX_RODZIN; i++) {
        if (!rej->rodziny[i].aktywna) {
            idx = i;
            break;
        }
    }

    if (idx >= 0) {
        rej->rodziny[idx].pid_rodzica = pid_rodzica;
        rej->rodziny[idx].pid_dziecka = pid_dziecka;
        rej->rodziny[idx].sektor = -1;
        rej->rodziny[idx].rodzic_przy_bramce = 0;
        rej->rodziny[idx].dziecko_przy_bramce = 0;
        rej->rodziny[idx].aktywna = 1;
        if (rej->liczba_rodzin < MAX_RODZIN) rej->liczba_rodzin++;
    }

    op.sem_op = 1;
    semop(sem_id, &op, 1);
    return idx;
}

void ustaw_przy_bramce(int flaga) {
    if (id_rodziny < 0) return;

    struct sembuf op = {0, -1, 0};
    if (semop(sem_id, &op, 1) == -1) return;

    Rodzina *r = &stan_hali->rejestr_rodzin.rodziny[id_rodziny];
    if (jestem_rodzicem) {
        r->rodzic_przy_bramce = flaga;
    } else {
        r->dziecko_przy_bramce = flaga;
    }

    op.sem_op = 1;
    semop(sem_id, &op, 1);
}

int czekaj_na_partnera() {
    if (id_rodziny < 0) return 1;

    for (int i = 0; i < 100; i++) {
        Rodzina *r = &stan_hali->rejestr_rodzin.rodziny[id_rodziny];
        if (r->rodzic_przy_bramce && r->dziecko_przy_bramce) {
            return 1;
        }
        if (stan_hali->ewakuacja_trwa) return 0;
        usleep(100000);
    }
    return 0;
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

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler_ewakuacja;
    sigaction(SYGNAL_EWAKUACJA, &sa, NULL);

    rejestr_init(NULL, 0);
}

int sprawdz_vip() {
    if ((rand() % 1000) >= 3) {
        return 0;
    }

    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;

    if (semop(sem_id, operacje, 1) == -1) {
        return 0;
    }

    int moze_byc_vip = 0;
    if (stan_hali->liczba_vip < stan_hali->limit_vip) {
        stan_hali->liczba_vip++;
        moze_byc_vip = 1;
    }

    operacje[0].sem_op = 1;
    semop(sem_id, operacje, 1);

    return moze_byc_vip;
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
    if (stan_hali->wszystkie_bilety_sprzedane) {
        printf("%sKibic %d: Kasy zamkniete - bilety wyprzedane.%s\n",
               KOLOR_CZERWONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d: Kasy zamkniete", getpid());
        return;
    }

    if (!jestem_vip) {
        aktualizuj_kolejke(1);
    } else {
        printf("%sKibic %d (VIP): Omijam kolejke do kasy.%s\n",
               KOLOR_MAGENTA, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d VIP omija kolejke", getpid());
    }

    KomunikatBilet msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = TYP_KOMUNIKATU_ZAPYTANIE;
    msg.pid_kibica = getpid();
    msg.id_druzyny = moja_druzyna;
    msg.czy_vip = jestem_vip;
    msg.liczba_biletow = liczba_biletow;
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
        liczba_biletow = odp.liczba_sprzedanych;

        char *typ_sektora = (numer_sektora == SEKTOR_VIP) ? "VIP" : "zwykly";
        printf("%sKibic %d (%s, Druzyna %c): Kupilem %d bilet(y) do sektora %d (%s).%s\n",
               KOLOR_ZIELONY,
               getpid(),
               jestem_vip ? "VIP" : "zwykly",
               (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
               liczba_biletow,
               numer_sektora,
               typ_sektora,
               KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d kupil %d bilet(y) do sektora %d (%s)",
                   getpid(), liczba_biletow, numer_sektora, typ_sektora);
    } else {
        printf("%sKibic %d: Brak biletow - wyprzedane!%s\n",
               KOLOR_CZERWONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d nie dostal biletu - brak miejsc", getpid());
    }
}

void idz_do_bramki() {
    if (!ma_bilet) return;
    sleep(1);

    if (jestem_dzieckiem || jestem_rodzicem) {
        ustaw_przy_bramce(1);
        printf("Kibic %d (%s): Czekam na %s przy bramce...\n",
               getpid(),
               jestem_rodzicem ? "RODZIC" : "DZIECKO",
               jestem_rodzicem ? "dziecko" : "rodzica");

        if (!czekaj_na_partnera()) {
            printf("%sKibic %d: Partner nie dotarl - rezygnuje.%s\n",
                   KOLOR_CZERWONY, getpid(), KOLOR_RESET);
            return;
        }
        printf("%sKibic %d: Rodzina kompletna - wchodzimy razem!%s\n",
               KOLOR_CYAN, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d rodzina kompletna przy bramce", getpid());
    }

    if (numer_sektora == SEKTOR_VIP) {
        if (stan_hali->ewakuacja_trwa) {
            printf("%sKibic %d (VIP): Ewakuacja trwa - nie wchodze.%s\n",
                   KOLOR_CZERWONY, getpid(), KOLOR_RESET);
            return;
        }

        printf("%sKibic %d (VIP): Wchodze osobnym wejsciem VIP bez kontroli.%s\n",
               KOLOR_MAGENTA, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d VIP wchodzi bez kontroli", getpid());

        struct sembuf op = {0, -1, 0};
        semop(sem_id, &op, 1);
        stan_hali->suma_kibicow_w_hali++;
        stan_hali->osoby_w_sektorze[SEKTOR_VIP]++;
        op.sem_op = 1;
        semop(sem_id, &op, 1);

        jestem_w_hali = 1;
        printf("%sKibic %d: Wszedlem na sektor VIP! Ogladam mecz...%s\n",
               KOLOR_ZIELONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d wszedl na sektor VIP", getpid());

        while (stan_hali->faza_meczu != FAZA_PO_MECZU && !ewakuacja_mnie && !stan_hali->ewakuacja_trwa) {
            sleep(1);
        }

        if (ewakuacja_mnie || stan_hali->ewakuacja_trwa) {
            ewakuuj_sie();
            return;
        }

        op.sem_op = -1;
        semop(sem_id, &op, 1);
        stan_hali->suma_kibicow_w_hali--;
        stan_hali->osoby_w_sektorze[SEKTOR_VIP]--;
        stan_hali->liczba_vip--;
        op.sem_op = 1;
        semop(sem_id, &op, 1);

        jestem_w_hali = 0;
        printf("Kibic %d: Wychodze z hali.\n", getpid());
        rejestr_log("KIBIC", "PID %d wyszedl z hali", getpid());
        return;
    }

    if (stan_hali->sektor_zablokowany[numer_sektora]) {
        printf("%sKibic %d: Sektor %d zablokowany. Czekam...%s\n",
               KOLOR_ZOLTY, getpid(), numer_sektora, KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d czeka - sektor %d zablokowany", getpid(), numer_sektora);
        while (stan_hali->sektor_zablokowany[numer_sektora]) {
            if (stan_hali->ewakuacja_trwa) return;
            sleep(1);
        }
    }

    int proba_vip = 0;
    int przepuszczeni = 0;
    #define MAX_PRZEPUSZCZEN 5

    int wybrane_stanowisko = rand() % 2;
    int moje_miejsce = -1;

    printf("Kibic %d: Ide do stanowiska %d w sektorze %d.\n",
           getpid(), wybrane_stanowisko, numer_sektora);

    while (moje_miejsce == -1) {
        if (stan_hali->ewakuacja_trwa) return;

        if (stan_hali->sektor_zablokowany[numer_sektora]) {
            printf("%sKibic %d: Sektor %d zablokowany podczas czekania. Czekam...%s\n",
                   KOLOR_ZOLTY, getpid(), numer_sektora, KOLOR_RESET);
            while (stan_hali->sektor_zablokowany[numer_sektora]) {
                if (stan_hali->ewakuacja_trwa) return;
                sleep(1);
            }
        }

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
                    b->miejsca[i].druzyna = moja_druzyna;
                    b->miejsca[i].ma_przedmiot = mam_noz;
                    b->miejsca[i].wiek = moj_wiek;
                    b->miejsca[i].zgoda_na_wejscie = 0;
                    b->miejsca[i].pid_kibica = getpid();
                    moje_miejsce = i;
                    przepuszczeni = 0;
                    break;
                }
            }
        } else {
            przepuszczeni++;

            if (przepuszczeni >= MAX_PRZEPUSZCZEN) {
                printf("%sKibic %d: FRUSTRACJA! Przepuscilem juz %d osob! Wpycham sie!%s\n",
                       KOLOR_CZERWONY, getpid(), przepuszczeni, KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d frustracja po %d przepuszczeniach", getpid(), przepuszczeni);
                proba_vip = 1;
                operacje[0].sem_op = 1;
                semop(sem_id, operacje, 1);
                break;
            } else if (przepuszczeni > 0 && przepuszczeni % 2 == 0) {
                printf("Kibic %d: Przepuscilem %d/%d osob...\n",
                       getpid(), przepuszczeni, MAX_PRZEPUSZCZEN);
            }
        }

        operacje[0].sem_op = 1;
        semop(sem_id, operacje, 1);

        if (moje_miejsce == -1 && !proba_vip) {
            usleep(100000);
        }
    }

    if (!proba_vip) {
        printf("Kibic %d: Czekam na kontrole na stanowisku %d...\n",
               getpid(), wybrane_stanowisko);

        while (1) {
            if (stan_hali->ewakuacja_trwa) return;

            if (stan_hali->sektor_zablokowany[numer_sektora]) {
                usleep(100000);
                continue;
            }

            int zgoda = stan_hali->bramki[numer_sektora][wybrane_stanowisko]
                       .miejsca[moje_miejsce].zgoda_na_wejscie;

            if (zgoda == 2) {
                printf("%sKibic %d: Zostalem wyrzucony - znaleziono noz!%s\n",
                       KOLOR_CZERWONY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d wyrzucony - noz", getpid());
                struct sembuf op = {0, -1, 0};
                semop(sem_id, &op, 1);
                memset(&stan_hali->bramki[numer_sektora][wybrane_stanowisko]
                       .miejsca[moje_miejsce], 0, sizeof(MiejscaKolejki));
                op.sem_op = 1;
                semop(sem_id, &op, 1);
                return;
            } else if (zgoda == 3) {
                printf("%sKibic %d: Zawrocony - za mlody bez opiekuna!%s\n",
                       KOLOR_ZOLTY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d zawrocony - wiek", getpid());
                struct sembuf op = {0, -1, 0};
                semop(sem_id, &op, 1);
                memset(&stan_hali->bramki[numer_sektora][wybrane_stanowisko]
                       .miejsca[moje_miejsce], 0, sizeof(MiejscaKolejki));
                op.sem_op = 1;
                semop(sem_id, &op, 1);
                return;
            } else if (zgoda == 1) {
                break;
            }
            usleep(50000);
        }

        struct sembuf op = {0, -1, 0};
        semop(sem_id, &op, 1);
        memset(&stan_hali->bramki[numer_sektora][wybrane_stanowisko]
               .miejsca[moje_miejsce], 0, sizeof(MiejscaKolejki));
        op.sem_op = 1;
        semop(sem_id, &op, 1);
    }

    if (proba_vip) {
        if (mam_noz) {
             printf("%sKibic %d (AGRESOR): Ochrona znalazla noz!%s\n",
                    KOLOR_CZERWONY, getpid(), KOLOR_RESET);
             rejestr_log("KIBIC", "PID %d agresor wyrzucony - noz", getpid());
             return;
        }
        if (moj_wiek < 15) {
             printf("%sKibic %d (AGRESOR): Za mlody bez opiekuna!%s\n",
                    KOLOR_ZOLTY, getpid(), KOLOR_RESET);
             rejestr_log("KIBIC", "PID %d agresor wyrzucony - wiek", getpid());
             return;
        }
    }

    struct sembuf op = {0, -1, 0};
    semop(sem_id, &op, 1);
    stan_hali->suma_kibicow_w_hali++;
    stan_hali->osoby_w_sektorze[numer_sektora]++;
    op.sem_op = 1;
    semop(sem_id, &op, 1);

    jestem_w_hali = 1;
    printf("%sKibic %d: Wszedlem na sektor %d! Ogladam mecz...%s\n",
           KOLOR_ZIELONY, getpid(), numer_sektora, KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d wszedl na sektor %d", getpid(), numer_sektora);

    while (stan_hali->faza_meczu != FAZA_PO_MECZU && !ewakuacja_mnie && !stan_hali->ewakuacja_trwa) {
        sleep(1);
    }

    if (ewakuacja_mnie || stan_hali->ewakuacja_trwa) {
        ewakuuj_sie();
        return;
    }

    op.sem_op = -1;
    semop(sem_id, &op, 1);
    stan_hali->suma_kibicow_w_hali--;
    stan_hali->osoby_w_sektorze[numer_sektora]--;
    if (stan_hali->osoby_w_sektorze[numer_sektora] < 0) {
        stan_hali->osoby_w_sektorze[numer_sektora] = 0;
    }
    op.sem_op = 1;
    semop(sem_id, &op, 1);

    jestem_w_hali = 0;
    printf("Kibic %d: Wychodze z hali.\n", getpid());
    rejestr_log("KIBIC", "PID %d wyszedl z hali", getpid());
}

int main(int argc, char *argv[]) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_nsec ^ getpid()));

    if (atexit(obsluga_wyjscia) != 0) exit(EXIT_FAILURE);
    inicjalizuj();
    if (stan_hali->ewakuacja_trwa) return 0;

    if (argc >= 3 && strcmp(argv[1], "--dziecko") == 0) {
        jestem_dzieckiem = 1;
        pid_partnera = (pid_t)atoi(argv[2]);
        id_rodziny = (argc >= 4) ? atoi(argv[3]) : -1;
        moj_wiek = WIEK_DZIECKA_MIN + (rand() % (WIEK_DZIECKA_MAX - WIEK_DZIECKA_MIN + 1));
        moja_druzyna = (argc >= 5) ? atoi(argv[4]) : DRUZYNA_A;
        numer_sektora = (argc >= 6) ? atoi(argv[5]) : -1;
        ma_bilet = (numer_sektora >= 0) ? 1 : 0;

        printf("%sKibic %d (DZIECKO): Start z rodzicem %d | Wiek: %d | Sektor: %d%s\n",
               KOLOR_CYAN, getpid(), pid_partnera, moj_wiek, numer_sektora, KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d dziecko rodzica %d, wiek %d, sektor %d",
                   getpid(), pid_partnera, moj_wiek, numer_sektora);

        if (ma_bilet) {
            idz_do_bramki();
        }
        return 0;
    }

    if (argc >= 4 && strcmp(argv[1], "--kolega") == 0) {
        jestem_kolega = 1;
        moja_druzyna = atoi(argv[2]);
        numer_sektora = atoi(argv[3]);
        ma_bilet = 1;
        moj_wiek = 18 + (rand() % 42);
        mam_noz = ((rand() % 100) < SZANSA_NA_PRZEDMIOT);

        printf("%sKibic %d (KOLEGA): Start | Druzyna: %c | Sektor: %d | Wiek: %d | Noz: %s%s\n",
               KOLOR_ZIELONY, getpid(),
               (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
               numer_sektora, moj_wiek,
               mam_noz ? "TAK" : "NIE",
               KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d kolega: Druzyna %c, Sektor %d, Wiek %d",
                   getpid(), (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
                   numer_sektora, moj_wiek);

        idz_do_bramki();
        return 0;
    }

    moja_druzyna = (rand() % 2) ? DRUZYNA_A : DRUZYNA_B;
    jestem_vip = sprawdz_vip();
    mam_noz = ((rand() % 100) < SZANSA_NA_PRZEDMIOT);

    int tworze_rodzine = (!jestem_vip && (rand() % 100) < SZANSA_RODZINY);

    if (tworze_rodzine) {
        jestem_rodzicem = 1;
        moj_wiek = WIEK_RODZICA_MIN + (rand() % (WIEK_RODZICA_MAX - WIEK_RODZICA_MIN + 1));
        liczba_biletow = 2;

        printf("%sKibic %d (RODZIC): Jestem z dzieckiem | Druzyna: %c | Wiek: %d%s\n",
               KOLOR_CYAN, getpid(), (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
               moj_wiek, KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d rodzic, druzyna %c, wiek %d",
                   getpid(), (moja_druzyna == DRUZYNA_A) ? 'A' : 'B', moj_wiek);

        sprobuj_kupic_bilet();

        if (ma_bilet) {
            id_rodziny = zarejestruj_rodzine(getpid(), 0);

            pid_t pid_dziecka = fork();
            if (pid_dziecka == 0) {
                char arg_rodzic[16], arg_rodzina[16], arg_druzyna[16], arg_sektor[16];
                snprintf(arg_rodzic, sizeof(arg_rodzic), "%d", getppid());
                snprintf(arg_rodzina, sizeof(arg_rodzina), "%d", id_rodziny);
                snprintf(arg_druzyna, sizeof(arg_druzyna), "%d", moja_druzyna);
                snprintf(arg_sektor, sizeof(arg_sektor), "%d", numer_sektora);
                execl("./kibic", "kibic", "--dziecko", arg_rodzic, arg_rodzina,
                      arg_druzyna, arg_sektor, NULL);
                perror("execl kibic dziecko");
                exit(EXIT_FAILURE);
            } else if (pid_dziecka > 0) {
                pid_partnera = pid_dziecka;

                struct sembuf op = {0, -1, 0};
                semop(sem_id, &op, 1);
                if (id_rodziny >= 0) {
                    stan_hali->rejestr_rodzin.rodziny[id_rodziny].pid_dziecka = pid_dziecka;
                    stan_hali->rejestr_rodzin.rodziny[id_rodziny].sektor = numer_sektora;
                }
                op.sem_op = 1;
                semop(sem_id, &op, 1);

                printf("%sKibic %d (RODZIC): Utworzono dziecko PID %d%s\n",
                       KOLOR_CYAN, getpid(), pid_dziecka, KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d rodzic utworzyl dziecko %d", getpid(), pid_dziecka);

                idz_do_bramki();
                waitpid(pid_dziecka, NULL, 0);
            }
        }
    } else {
        moj_wiek = (rand() % 60) + 5;
        if (jestem_vip) {
            liczba_biletow = 1;
        } else {
            liczba_biletow = (rand() % MAX_BILETOW_NA_KIBICA) + 1;
        }

        printf("Kibic %d: Start | Druzyna: %c | Wiek: %d | VIP: %s | Noz: %s | Bilety: %d\n",
               getpid(),
               (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
               moj_wiek,
               jestem_vip ? "TAK" : "NIE",
               mam_noz ? "TAK" : "NIE",
               liczba_biletow);

        rejestr_log("KIBIC", "PID %d start: Druzyna %c, Wiek %d, VIP %s, Noz %s, Bilety %d",
                   getpid(),
                   (moja_druzyna == DRUZYNA_A) ? 'A' : 'B',
                   moj_wiek,
                   jestem_vip ? "TAK" : "NIE",
                   mam_noz ? "TAK" : "NIE",
                   liczba_biletow);

        sprobuj_kupic_bilet();
        if (ma_bilet) {
            if (!jestem_vip && liczba_biletow == 2) {
                pid_t pid_kolegi = fork();
                if (pid_kolegi == 0) {
                    char arg_druzyna[16], arg_sektor[16];
                    snprintf(arg_druzyna, sizeof(arg_druzyna), "%d", moja_druzyna);
                    snprintf(arg_sektor, sizeof(arg_sektor), "%d", numer_sektora);
                    execl("./kibic", "kibic", "--kolega", arg_druzyna, arg_sektor, NULL);
                    perror("execl kibic kolega");
                    exit(EXIT_FAILURE);
                } else if (pid_kolegi > 0) {
                    printf("%sKibic %d: Przekazuje bilet koledze (PID %d)%s\n",
                           KOLOR_ZIELONY, getpid(), pid_kolegi, KOLOR_RESET);
                    rejestr_log("KIBIC", "PID %d przekazal bilet koledze %d", getpid(), pid_kolegi);
                    idz_do_bramki();
                    waitpid(pid_kolegi, NULL, 0);
                }
            } else {
                idz_do_bramki();
            }
        }
    }

    return 0;
}