/*
 * KIBIC.C - Proces kibica
 *
 * Odpowiedzialnosci:
 * - Zakup biletu i wybor kasy
 * - Wejscie przez bramke (VIP, rodziny, agresor)
 * - Zachowanie w trakcie meczu i wyjscie/ewakuacja
 */
#include "common.h"
#include <time.h>
#include <sys/wait.h>

int shm_id = -1;
int msg_id = -1;
int sem_id = -1;
StanHali *stan_hali = NULL;

// --- Stan kibica (wspolny dla calego procesu) ---
int moja_druzyna = 0;       // Wylosowana druzyna kibica
int jestem_vip = 0;         // Flaga VIP (osobne wejscie)
int ma_bilet = 0;           // Czy kibic kupil bilet
int numer_sektora = -1;     // Przydzielony sektor
int mam_noz = 0;            // Czy niesie zabroniony przedmiot
int moj_wiek = 20;          // Wiek kibica
int liczba_biletow = 1;     // Zadana liczba biletow

// --- Role specjalne ---
int jestem_dzieckiem = 0;   // Wejscie w trybie dziecka
int jestem_rodzicem = 0;    // Wejscie w trybie rodzica
int jestem_kolega = 0;      // Wejscie z biletu "kolega"
pid_t pid_partnera = 0;     // PID partnera (dziecko/rodzic/kolega)
int id_rodziny = -1;        // Indeks w rejestrze rodzin

// --- Flagi runtime ---
volatile sig_atomic_t ewakuacja_mnie = 0; // Flaga ewakuacji procesu
int jestem_w_hali = 0;                   // Czy kibic jest w hali
int moja_kasa = -1;                      // Numer wybranej kasy
int agresor_aktywny = 0;                 // Czy kibic ma status agresora
int agresor_sektor = -1;                 // Sektor agresora
int agresor_stanowisko = -1;             // Stanowisko agresora

/* Sygnal ewakuacji - ustaw tylko flage */
void handler_ewakuacja(int sig) {
    (void)sig;
    ewakuacja_mnie = 1;
}

/* Szybkie wyjscie przy SIGTERM */
void handler_term(int sig) {
    (void)sig;
    _exit(0);
}

/* Natychmiastowe opuszczenie hali podczas ewakuacji */
void ewakuuj_sie() {
    if (!jestem_w_hali) return;

    // Komunikat i log ewakuacji
    printf("%sKibic %d: [EWAKUACJA] Opuszczam hale natychmiast!%s\n",
           KOLOR_CZERWONY, getpid(), KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d ewakuacja z sektora %d", getpid(), numer_sektora);

    // Aktualizacja licznikow w hali
    struct sembuf op = {0, -1, 0};
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock ewakuacja") == -1) return;

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
    if (semop_retry_ctx(sem_id, &op, 1, "semop unlock ewakuacja") == -1) return;

    // Sygnal do oczekujacych na wyjscie
    struct sembuf sig_wyszedl = {SEM_KIBIC_WYSZEDL, 1, 0};
    semop_retry_ctx(sem_id, &sig_wyszedl, 1, "semop sig wyszedl");

    jestem_w_hali = 0;
    printf("%sKibic %d: Ewakuowalem sie z hali.%s\n",
           KOLOR_ZIELONY, getpid(), KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d ewakuacja zakonczona", getpid());
}

/* Sprzatanie zasobow procesu kibica */
void obsluga_wyjscia() {
    if (stan_hali != NULL) {
        // Zwolnij agresora jesli byl ustawiony
        if (agresor_aktywny && agresor_sektor >= 0 && agresor_stanowisko >= 0) {
            struct sembuf op = {0, -1, 0};
            if (semop_retry_ctx(sem_id, &op, 1, "semop lock agresor") == 0) {
                Bramka *b = &stan_hali->bramki[agresor_sektor][agresor_stanowisko];
                if (b->pid_agresora == getpid()) {
                    b->pid_agresora = 0;
                }
                op.sem_op = 1;
                semop_retry_ctx(sem_id, &op, 1, "semop unlock agresor");
            }
        }
        // Dezaktywuj rodzine po wyjsciu rodzica
        if (jestem_rodzicem && id_rodziny >= 0) {
            stan_hali->rejestr_rodzin.rodziny[id_rodziny].aktywna = 0;
        }
        // Odlacz pamiec dzielona
        if (shmdt(stan_hali) == -1) {
            perror("shmdt kibic");
        }
    }
}

/* Rejestracja nowej rodziny (rodzic+dziecko) w pamieci dzielonej */
int zarejestruj_rodzine(pid_t pid_rodzica, pid_t pid_dziecka) {
    // Sekcja krytyczna: rejestr rodzin
    struct sembuf op = {0, -1, 0};
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock rodzina") == -1) return -1;

    int idx = -1;
    RejestrRodzin *rej = &stan_hali->rejestr_rodzin;

    for (int i = 0; i < MAX_RODZIN; i++) {
        // Szukaj wolnego slotu
        if (!rej->rodziny[i].aktywna) {
            idx = i;
            break;
        }
    }

    if (idx >= 0) {
        // Wpisz dane rodziny
        rej->rodziny[idx].pid_rodzica = pid_rodzica;
        rej->rodziny[idx].pid_dziecka = pid_dziecka;
        rej->rodziny[idx].sektor = -1;
        rej->rodziny[idx].rodzic_przy_bramce = 0;
        rej->rodziny[idx].dziecko_przy_bramce = 0;
        rej->rodziny[idx].aktywna = 1;
        if (rej->liczba_rodzin < MAX_RODZIN) rej->liczba_rodzin++;
    }

    op.sem_op = 1;
    if (semop_retry_ctx(sem_id, &op, 1, "semop unlock rodzina") == -1) return -1;
    return idx;
}

/* Oznaczenie obecnosci rodzica/dziecka przy bramce */
void ustaw_przy_bramce(int flaga) {
    if (id_rodziny < 0) return;

    // Zaktualizuj obecnosci w rodzinie
    struct sembuf op = {0, -1, 0};
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock bramka") == -1) return;

    Rodzina *r = &stan_hali->rejestr_rodzin.rodziny[id_rodziny];
    if (jestem_rodzicem) {
        r->rodzic_przy_bramce = flaga;
    } else {
        r->dziecko_przy_bramce = flaga;
    }

    int both_present = (r->rodzic_przy_bramce && r->dziecko_przy_bramce);

    op.sem_op = 1;
    if (semop_retry_ctx(sem_id, &op, 1, "semop unlock bramka") == -1) return;

    if (both_present && flaga) {
        // Sygnal, ze oboje sa przy bramce
        struct sembuf sig_rodz = {SEM_RODZINA(id_rodziny), 2, 0};
        semop_retry_ctx(sem_id, &sig_rodz, 1, "semop sig rodzina");
    }
}

/* Czekanie na partnera przy bramce (rodzic/dziecko) */
int czekaj_na_partnera() {
    if (id_rodziny < 0) return 1;

    // Jesli partner juz obecny, nie czekaj
    Rodzina *r = &stan_hali->rejestr_rodzin.rodziny[id_rodziny];
    if (r->rodzic_przy_bramce && r->dziecko_przy_bramce) {
        return 1;
    }

    // Czekaj z ograniczeniem liczby prob
    for (int i = 0; i < 100; i++) {
        if (stan_hali->ewakuacja_trwa) return 0;
        struct sembuf wait_rodz = {SEM_RODZINA(id_rodziny), -1, 0};
        if (semop_retry_ctx(sem_id, &wait_rodz, 1, "semop wait rodzina") == -1) {
            return 0;
        }
        if (r->rodzic_przy_bramce && r->dziecko_przy_bramce) {
            return 1;
        }
    }
    return 0;
}

/* Podlaczenie do zasobow IPC i konfiguracja sygnalow */
void inicjalizuj() {
    // Klucze IPC
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

    sem_id = semget(klucz_sem, SEM_TOTAL, 0600);
    if (sem_id == -1) exit(0);

    // Mapowanie pamieci dzielonej
    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat kibic");
        exit(EXIT_FAILURE);
    }

    // Sygnal ewakuacji
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = handler_ewakuacja;
    sigaction(SYGNAL_EWAKUACJA, &sa, NULL);

    // Sygnal zakonczenia
    struct sigaction sa_term;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    sa_term.sa_handler = handler_term;
    sigaction(SIGTERM, &sa_term, NULL);

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

    if (semop_retry_ctx(sem_id, operacje, 1, "semop lock vip") == -1) {
        return 0;
    }

    int moze_byc_vip = 0;
    if (stan_hali->liczba_vip < stan_hali->limit_vip) {
        moze_byc_vip = 1;
    }

    operacje[0].sem_op = 1;
    if (semop_retry_ctx(sem_id, operacje, 1, "semop unlock vip") == -1) {
        return 0;
    }

    return moze_byc_vip;
}

int wybierz_kase_aktywna() {
    int wybrana_kasa = -1;
    int min_dlugosc = 1000000;

    for (int i = 0; i < LICZBA_KAS; i++) {
        if (stan_hali->kasa_aktywna[i] && !stan_hali->kasa_zamykanie[i]) {
            if (stan_hali->kolejka_dlugosc[i] < min_dlugosc) {
                min_dlugosc = stan_hali->kolejka_dlugosc[i];
                wybrana_kasa = i;
            }
        }
    }
    return wybrana_kasa;
}

void aktualizuj_kolejke(int zmiana) {
    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;

    if (semop_retry_ctx(sem_id, operacje, 1, "semop lock kolejka") == -1) return;

    if (zmiana > 0) {
        int wybrana_kasa = wybierz_kase_aktywna();
        if (wybrana_kasa != -1) {
            stan_hali->kolejka_dlugosc[wybrana_kasa]++;
            moja_kasa = wybrana_kasa;
        } else {
            moja_kasa = -1;
        }
    } else {
        if (moja_kasa >= 0 && moja_kasa < LICZBA_KAS &&
            stan_hali->kolejka_dlugosc[moja_kasa] > 0) {
            stan_hali->kolejka_dlugosc[moja_kasa]--;
        } else {
            for (int i = 0; i < LICZBA_KAS; i++) {
                 if (stan_hali->kolejka_dlugosc[i] > 0) {
                     stan_hali->kolejka_dlugosc[i]--;
                     break;
                 }
            }
        }
        moja_kasa = -1;
    }

    operacje[0].sem_op = 1;
    semop_retry_ctx(sem_id, operacje, 1, "semop unlock kolejka");
}

int wybierz_kase_bez_kolejki() {
    struct sembuf operacje[1];
    operacje[0].sem_num = 0;
    operacje[0].sem_op = -1;
    operacje[0].sem_flg = 0;

    if (semop_retry_ctx(sem_id, operacje, 1, "semop lock kasa") == -1) return -1;

    int wybrana = wybierz_kase_aktywna();

    operacje[0].sem_op = 1;
    semop_retry_ctx(sem_id, operacje, 1, "semop unlock kasa");

    return wybrana;
}

/* Czyszczenie slota po przejsciu kibica */
void wyczysc_slot(int sektor, int stanowisko, int slot) {
    // Czyszczenie miejsca w bramce po odrzuceniu/wyjsciu
    struct sembuf op = {0, -1, 0};
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock slot") == -1) return;

    Bramka *b = &stan_hali->bramki[sektor][stanowisko];
    memset(&b->miejsca[slot], 0, sizeof(MiejscaKolejki));

    int zajete = 0; // Czy pozostaly inne osoby w stanowisku
    for (int i = 0; i < 3; i++) {
        if (b->miejsca[i].pid_kibica != 0) {
            zajete = 1;
            break;
        }
    }
    if (!zajete) {
        // Brak kibicow - resetuj druzyna stanowiska
        b->obecna_druzyna = 0;
    }
    if (b->pid_agresora != 0) {
        // Jesli agresor nie istnieje w slotach, usun go
        int agresor_w_slotach = 0;
        for (int i = 0; i < 3; i++) {
            if (b->miejsca[i].pid_kibica == b->pid_agresora) {
                agresor_w_slotach = 1;
                break;
            }
        }
        if (!agresor_w_slotach) {
            b->pid_agresora = 0;
        }
    }

    op.sem_op = 1;
    semop_retry_ctx(sem_id, &op, 1, "semop unlock slot");
}

/* Proba zakupu biletu przez kolejke komunikatow */
void sprobuj_kupic_bilet() {
    // Jesli wyprzedane, nie wchodz do kolejki
    if (stan_hali->wszystkie_bilety_sprzedane) {
        printf("%sKibic %d: Kasy zamkniete - bilety wyprzedane.%s\n",
               KOLOR_CZERWONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d: Kasy zamkniete", getpid());
        return;
    }

    // VIP omija kolejke; pozostali zwiekszaja licznik kolejki
    if (!jestem_vip) {
        aktualizuj_kolejke(1);
    } else {
        printf("%sKibic %d (VIP): Omijam kolejke do kasy.%s\n",
               KOLOR_MAGENTA, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d VIP omija kolejke", getpid());
    }

    if (jestem_vip) {
        // VIP wybiera kase bez kolejki
        moja_kasa = wybierz_kase_bez_kolejki();
    }

    if (moja_kasa < 0 || moja_kasa >= LICZBA_KAS) {
        printf("%sKibic %d: Brak aktywnych kas.%s\n",
               KOLOR_CZERWONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d brak aktywnych kas", getpid());
        return;
    }

    // Zbuduj zapytanie do kasjera
    KomunikatBilet msg;
    memset(&msg, 0, sizeof(msg));
    msg.mtype = moja_kasa + 1;
    msg.pid_kibica = getpid();
    msg.id_druzyny = moja_druzyna;
    msg.czy_vip = jestem_vip;
    msg.liczba_biletow = liczba_biletow;
    msg.nr_sektora = -1;
    msg.nr_kasy = moja_kasa;

    // Wyslij zapytanie o bilet
    if (msgsnd(msg_id, &msg, sizeof(KomunikatBilet) - sizeof(long), 0) == -1) {
        perror("msgsnd");
        if (!jestem_vip && moja_kasa >= 0) aktualizuj_kolejke(-1);
        return;
    }

    // Oczekuj na odpowiedz kasjera (mtype = PID)
    OdpowiedzBilet odp;
    if (msgrcv(msg_id, &odp, sizeof(OdpowiedzBilet) - sizeof(long), getpid(), 0) == -1) {
        perror("msgrcv");
        if (!jestem_vip && moja_kasa >= 0) aktualizuj_kolejke(-1);
        return;
    }

    if (!jestem_vip && moja_kasa >= 0) aktualizuj_kolejke(-1);

    if (odp.czy_sukces) {
        // Sukces - zapisz dane biletu
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

/* Wejscie na hale przez bramke kontroli */
void idz_do_bramki() {
    if (!ma_bilet) return;

    if (jestem_dzieckiem || jestem_rodzicem) {
        // Rodzina: poczekaj na partnera przy bramce
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
        // VIP wchodzi osobnym wejsciem bez kontroli bramkowej
        if (stan_hali->ewakuacja_trwa) {
            printf("%sKibic %d (VIP): Ewakuacja trwa - nie wchodze.%s\n",
                   KOLOR_CZERWONY, getpid(), KOLOR_RESET);
            return;
        }

        printf("%sKibic %d (VIP): Wchodze osobnym wejsciem VIP bez kontroli.%s\n",
               KOLOR_MAGENTA, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d VIP wchodzi bez kontroli", getpid());

        // Sekcja krytyczna: limit VIP
        struct sembuf op = {0, -1, 0};
        if (semop_retry_ctx(sem_id, &op, 1, "semop lock vip wejscie") == -1) return;
        if (stan_hali->liczba_vip >= stan_hali->limit_vip) {
            op.sem_op = 1;
            semop_retry_ctx(sem_id, &op, 1, "semop unlock vip brak");
            printf("%sKibic %d (VIP): Brak miejsc VIP - za pozno.%s\n",
                   KOLOR_CZERWONY, getpid(), KOLOR_RESET);
            return;
        }
        stan_hali->liczba_vip++;
        stan_hali->suma_kibicow_w_hali++;
        stan_hali->osoby_w_sektorze[SEKTOR_VIP]++;
        op.sem_op = 1;
        if (semop_retry_ctx(sem_id, &op, 1, "semop unlock vip wejscie") == -1) return;

        jestem_w_hali = 1;
        printf("%sKibic %d: Wszedlem na sektor VIP.%s\n",
               KOLOR_ZIELONY, getpid(), KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d wszedl na sektor VIP", getpid());

        if (stan_hali->faza_meczu == FAZA_PRZED_MECZEM) {
            // Czekaj na start meczu
            printf("Kibic %d: Czekam na start meczu.\n", getpid());
            rejestr_log("KIBIC", "PID %d czeka na start meczu", getpid());
            struct sembuf wait_start = {SEM_START_MECZU, 0, 0};
            if (semop_retry_ctx(sem_id, &wait_start, 1, "semop wait start") == -1) return;
        }

        printf("Kibic %d: Ogladam mecz...\n", getpid());
        rejestr_log("KIBIC", "PID %d oglada mecz", getpid());

        while (stan_hali->faza_meczu != FAZA_PO_MECZU && !ewakuacja_mnie && !stan_hali->ewakuacja_trwa) {
            // Czekaj na koniec meczu lub ewakuacje
            struct sembuf wait_faza = {SEM_FAZA_MECZU, -1, 0};
            if (semop_retry_ctx(sem_id, &wait_faza, 1, "semop wait faza") == -1) return;
        }

        if (ewakuacja_mnie || stan_hali->ewakuacja_trwa) {
            ewakuuj_sie();
            return;
        }

        op.sem_op = -1;
        if (semop_retry_ctx(sem_id, &op, 1, "semop lock vip wyjscie") == -1) return;
        stan_hali->suma_kibicow_w_hali--;
        stan_hali->osoby_w_sektorze[SEKTOR_VIP]--;
        stan_hali->liczba_vip--;
        op.sem_op = 1;
        if (semop_retry_ctx(sem_id, &op, 1, "semop unlock vip wyjscie") == -1) return;

        struct sembuf sig_wyszedl = {SEM_KIBIC_WYSZEDL, 1, 0};
        semop_retry_ctx(sem_id, &sig_wyszedl, 1, "semop sig wyszedl");

        jestem_w_hali = 0;
        printf("Kibic %d: Wychodze z hali.\n", getpid());
        rejestr_log("KIBIC", "PID %d wyszedl z hali", getpid());
        return;
    }

    if (stan_hali->sektor_zablokowany[numer_sektora]) {
        // Wejscie do sektora zablokowane przez kierownika
        printf("%sKibic %d: Sektor %d zablokowany. Czekam...%s\n",
               KOLOR_ZOLTY, getpid(), numer_sektora, KOLOR_RESET);
        rejestr_log("KIBIC", "PID %d czeka - sektor %d zablokowany", getpid(), numer_sektora);
        while (stan_hali->sektor_zablokowany[numer_sektora]) {
            if (stan_hali->ewakuacja_trwa) return;
            struct sembuf wait_sek = {SEM_SEKTOR(numer_sektora), -1, 0};
            if (semop_retry_ctx(sem_id, &wait_sek, 1, "semop wait sektor") == -1) return;
        }
    }

    int przepuszczeni = 0;
    int wpychanie = 0;
    #define MAX_PRZEPUSZCZEN 5
    // Po tylu przepuszczeniach kibic moze zaczac wpychanie

    int wybrane_stanowisko = rand() % 2; // Losowy wybor stanowiska
    int moje_miejsce = -1;
    int retry_bramka = 1;

    printf("Kibic %d: Ide do stanowiska %d w sektorze %d.\n",
           getpid(), wybrane_stanowisko, numer_sektora);

    while (retry_bramka) {
    retry_bramka = 0;
    moje_miejsce = -1;

    while (moje_miejsce == -1) {
        if (stan_hali->ewakuacja_trwa) return;

        if (stan_hali->sektor_zablokowany[numer_sektora]) {
            // Dynamiczna blokada w trakcie oczekiwania
            printf("%sKibic %d: Sektor %d zablokowany podczas czekania. Czekam...%s\n",
                   KOLOR_ZOLTY, getpid(), numer_sektora, KOLOR_RESET);
            while (stan_hali->sektor_zablokowany[numer_sektora]) {
                if (stan_hali->ewakuacja_trwa) return;
                struct sembuf wait_sek = {SEM_SEKTOR(numer_sektora), -1, 0};
                if (semop_retry_ctx(sem_id, &wait_sek, 1, "semop wait sektor") == -1) return;
            }
        }

        struct sembuf operacje[1];
        operacje[0].sem_num = 0;
        operacje[0].sem_op = -1;
        operacje[0].sem_flg = 0;
        if (semop_retry_ctx(sem_id, operacje, 1, "semop lock bramka miejsc") == -1) return;

        Bramka *b = &stan_hali->bramki[numer_sektora][wybrane_stanowisko];

        if (wpychanie && b->pid_agresora == 0) {
            // Ustaw agresora dla bramki
            b->pid_agresora = getpid();
            agresor_aktywny = 1;
            agresor_sektor = numer_sektora;
            agresor_stanowisko = wybrane_stanowisko;
        }
        if (b->pid_agresora != 0 && b->pid_agresora != getpid()) {
            // Sprawdz, czy agresor nadal zyje
            if (kill(b->pid_agresora, 0) == -1 && errno == ESRCH) {
                b->pid_agresora = 0;
            }
        }
        if (b->pid_agresora != 0 && b->pid_agresora != getpid()) {
            // Czekaj na zwolnienie bramki przez agresora
            operacje[0].sem_op = 1;
            semop_retry_ctx(sem_id, operacje, 1, "semop unlock bramka miejsc");
            struct sembuf wait_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), -1, 0};
            if (semop_retry_ctx(sem_id, &wait_bramka, 1, "semop wait bramka") == -1) return;
            continue;
        }

        int wolne = 0; // Liczba wolnych miejsc w stanowisku
        for(int i=0; i<3; i++) if(b->miejsca[i].pid_kibica == 0) wolne++;

        if (wolne > 0 && (b->obecna_druzyna == 0 || b->obecna_druzyna == moja_druzyna)) {
            // Jest wolny slot i zgodnosc druzyny
            if (b->obecna_druzyna == 0) {
                b->obecna_druzyna = moja_druzyna;
            }
            for (int i = 0; i < 3; i++) {
                if (b->miejsca[i].pid_kibica == 0) {
                    // Wypelnij dane kibica w slocie
                    b->miejsca[i].druzyna = moja_druzyna;
                    b->miejsca[i].ma_przedmiot = mam_noz;
                    b->miejsca[i].wiek = moj_wiek;
                    b->miejsca[i].zgoda_na_wejscie = 0;
                    b->miejsca[i].pid_kibica = getpid();
                    moje_miejsce = i;
                    przepuszczeni = 0;
                    // Zasygnalizuj pracownikowi, ze jest kto obslugiwac
                    struct sembuf sig_praca = {SEM_PRACA(numer_sektora), 1, 0};
                    semop_retry_ctx(sem_id, &sig_praca, 1, "semop sig praca");
                    if (b->pid_agresora == getpid()) {
                        // Agresor zostal obsluzony - zwolnij
                        b->pid_agresora = 0;
                        agresor_aktywny = 0;
                    }
                    break;
                }
            }
        } else {
            if (!wpychanie) {
                // Licznik przepuszczen - decyzja o wpychaniu
                przepuszczeni++;
                if (przepuszczeni >= MAX_PRZEPUSZCZEN) {
                    printf("%sKibic %d: FRUSTRACJA! Przepuscilem %d osob! WPYCHAM SIE!%s\n",
                           KOLOR_CZERWONY, getpid(), przepuszczeni, KOLOR_RESET);
                    rejestr_log("KIBIC", "PID %d frustracja po %d przepuszczeniach", getpid(), przepuszczeni);
                    wpychanie = 1;
                    if (b->pid_agresora == 0) {
                        b->pid_agresora = getpid();
                        agresor_aktywny = 1;
                        agresor_sektor = numer_sektora;
                        agresor_stanowisko = wybrane_stanowisko;
                    }
                } else if (przepuszczeni > 0 && przepuszczeni % 2 == 0) {
                    printf("Kibic %d: Przepuscilem %d/%d osob...\n",
                           getpid(), przepuszczeni, MAX_PRZEPUSZCZEN);
                }
            }
        }

        operacje[0].sem_op = 1;
        if (semop_retry_ctx(sem_id, operacje, 1, "semop unlock bramka miejsc") == -1) return;

        if (moje_miejsce == -1) {
            // Brak miejsca - poczekaj na sygnal bramki
            struct sembuf wait_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), -1, 0};
            if (semop_retry_ctx(sem_id, &wait_bramka, 1, "semop wait bramka") == -1) return;
        }
    }

    printf("Kibic %d: Czekam na kontrole na stanowisku %d...\n",
           getpid(), wybrane_stanowisko);

        while (1) {
            if (stan_hali->ewakuacja_trwa) return;

            if (stan_hali->sektor_zablokowany[numer_sektora]) {
                // Blokada w trakcie kontroli
                struct sembuf wait_sek = {SEM_SEKTOR(numer_sektora), -1, 0};
                if (semop_retry_ctx(sem_id, &wait_sek, 1, "semop wait sektor") == -1) return;
                continue;
            }

            // Oczekiwanie na wynik kontroli stanowiska
            struct sembuf wait_slot = {SEM_SLOT(numer_sektora, wybrane_stanowisko, moje_miejsce), -1, 0};
            if (semop_retry_ctx(sem_id, &wait_slot, 1, "semop wait slot") == -1) return;

            int zgoda = stan_hali->bramki[numer_sektora][wybrane_stanowisko]
                       .miejsca[moje_miejsce].zgoda_na_wejscie;

            if (zgoda == 2) {
                // Odrzucenie: noz
                printf("%sKibic %d: Zostalem wyrzucony - znaleziono noz!%s\n",
                       KOLOR_CZERWONY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d wyrzucony - noz", getpid());
                wyczysc_slot(numer_sektora, wybrane_stanowisko, moje_miejsce);
                struct sembuf sig_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), 1, 0};
                semop_retry_ctx(sem_id, &sig_bramka, 1, "semop sig bramka");
                return;
            } else if (zgoda == 3) {
                // Odrzucenie: wiek bez opiekuna
                printf("%sKibic %d: Zawrocony - za mlody bez opiekuna!%s\n",
                       KOLOR_ZOLTY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d zawrocony - wiek", getpid());
                wyczysc_slot(numer_sektora, wybrane_stanowisko, moje_miejsce);
                struct sembuf sig_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), 1, 0};
                semop_retry_ctx(sem_id, &sig_bramka, 1, "semop sig bramka");
                return;
            } else if (zgoda == 4) {
                // Odrzucenie: zla druzyna (powrot do kolejki)
                printf("%sKibic %d: Przepuszczony - zla druzyna, wracam do kolejki%s\n",
                       KOLOR_ZOLTY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d przepuszczony - zla druzyna", getpid());
                wyczysc_slot(numer_sektora, wybrane_stanowisko, moje_miejsce);
                struct sembuf sig_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), 1, 0};
                semop_retry_ctx(sem_id, &sig_bramka, 1, "semop sig bramka");
                if (!wpychanie) {
                    przepuszczeni++;
                    if (przepuszczeni >= MAX_PRZEPUSZCZEN) {
                        printf("%sKibic %d: FRUSTRACJA po przepuszczeniu! WPYCHAM SIE!%s\n",
                               KOLOR_CZERWONY, getpid(), KOLOR_RESET);
                        rejestr_log("KIBIC", "PID %d frustracja po przepuszczeniu", getpid());
                        wpychanie = 1;
                        if (agresor_aktywny == 0) {
                            struct sembuf op = {0, -1, 0};
                            if (semop_retry_ctx(sem_id, &op, 1, "semop lock agresor set") == -1) return;
                            Bramka *b = &stan_hali->bramki[numer_sektora][wybrane_stanowisko];
                            if (b->pid_agresora == 0) {
                                b->pid_agresora = getpid();
                                agresor_aktywny = 1;
                                agresor_sektor = numer_sektora;
                                agresor_stanowisko = wybrane_stanowisko;
                            }
                            op.sem_op = 1;
                            semop_retry_ctx(sem_id, &op, 1, "semop unlock agresor set");
                        }
                    }
                }
                retry_bramka = 1;
                break;
            } else if (zgoda == 5) {
                printf("%sKibic %d: Mecz zakonczony - nie moge wejsc.%s\n",
                       KOLOR_ZOLTY, getpid(), KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d odrzucony - mecz zakonczony", getpid());
                wyczysc_slot(numer_sektora, wybrane_stanowisko, moje_miejsce);
                struct sembuf sig_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), 1, 0};
                semop_retry_ctx(sem_id, &sig_bramka, 1, "semop sig bramka");
                return;
            } else if (zgoda == 1) {
                break;
            }
        }

    if (!retry_bramka) {
        wyczysc_slot(numer_sektora, wybrane_stanowisko, moje_miejsce);
        struct sembuf sig_bramka = {SEM_BRAMKA(numer_sektora, wybrane_stanowisko), 1, 0};
        semop_retry_ctx(sem_id, &sig_bramka, 1, "semop sig bramka");
    }
    }

    struct sembuf op = {0, -1, 0};
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock wejscie") == -1) return;
    stan_hali->suma_kibicow_w_hali++;
    stan_hali->osoby_w_sektorze[numer_sektora]++;
    op.sem_op = 1;
    if (semop_retry_ctx(sem_id, &op, 1, "semop unlock wejscie") == -1) return;

    jestem_w_hali = 1;
    printf("%sKibic %d: Wszedlem na sektor %d.%s\n",
           KOLOR_ZIELONY, getpid(), numer_sektora, KOLOR_RESET);
    rejestr_log("KIBIC", "PID %d wszedl na sektor %d", getpid(), numer_sektora);

    if (stan_hali->faza_meczu == FAZA_PRZED_MECZEM) {
        printf("Kibic %d: Czekam na start meczu.\n", getpid());
        rejestr_log("KIBIC", "PID %d czeka na start meczu", getpid());
        struct sembuf wait_start = {SEM_START_MECZU, 0, 0};
        if (semop_retry_ctx(sem_id, &wait_start, 1, "semop wait start") == -1) return;
    }

    printf("Kibic %d: Ogladam mecz...\n", getpid());
    rejestr_log("KIBIC", "PID %d oglada mecz", getpid());

    while (stan_hali->faza_meczu != FAZA_PO_MECZU && !ewakuacja_mnie && !stan_hali->ewakuacja_trwa) {
        struct sembuf wait_faza = {SEM_FAZA_MECZU, -1, 0};
        if (semop_retry_ctx(sem_id, &wait_faza, 1, "semop wait faza") == -1) return;
    }

    if (ewakuacja_mnie || stan_hali->ewakuacja_trwa) {
        ewakuuj_sie();
        return;
    }

    op.sem_op = -1;
    if (semop_retry_ctx(sem_id, &op, 1, "semop lock wyjscie") == -1) return;
    stan_hali->suma_kibicow_w_hali--;
    stan_hali->osoby_w_sektorze[numer_sektora]--;
    if (stan_hali->osoby_w_sektorze[numer_sektora] < 0) {
        stan_hali->osoby_w_sektorze[numer_sektora] = 0;
    }
    op.sem_op = 1;
    if (semop_retry_ctx(sem_id, &op, 1, "semop unlock wyjscie") == -1) return;

    struct sembuf sig_wyszedl = {SEM_KIBIC_WYSZEDL, 1, 0};
    semop_retry_ctx(sem_id, &sig_wyszedl, 1, "semop sig wyszedl");

    jestem_w_hali = 0;
    printf("Kibic %d: Wychodze z hali.\n", getpid());
    rejestr_log("KIBIC", "PID %d wyszedl z hali", getpid());
}

int main(int argc, char *argv[]) {
    setlinebuf(stdout);

    // Losowe ziarno dla zachowan kibica
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    srand((unsigned int)(ts.tv_nsec ^ getpid()));

    if (atexit(obsluga_wyjscia) != 0) exit(EXIT_FAILURE);
    inicjalizuj();
    if (stan_hali->ewakuacja_trwa) return 0;

    // Tryb dziecka: uruchamiany przez rodzica
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

    // Tryb kolegi: drugi bilet z tego samego zakupu
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

    // Domyslna sciezka: losowanie cech kibica
    moja_druzyna = (rand() % 2) ? DRUZYNA_A : DRUZYNA_B;
    jestem_vip = sprawdz_vip();
    mam_noz = ((rand() % 100) < SZANSA_NA_PRZEDMIOT);

    int tworze_rodzine = (!jestem_vip && (rand() % 100) < SZANSA_RODZINY);

    // Rodzina: rodzic kupuje 2 bilety i tworzy dziecko
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
            // Rejestruj rodzine i uruchom dziecko jako osobny proces
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
                // Rodzic kontynuuje wejsciem, dziecko idzie swoja sciezka
                pid_partnera = pid_dziecka;

                struct sembuf op = {0, -1, 0};
                if (semop_retry_ctx(sem_id, &op, 1, "semop lock rodzina upd") == -1) {
                    return 0;
                }
                if (id_rodziny >= 0) {
                    // Ustal dane dziecka i sektor rodziny
                    stan_hali->rejestr_rodzin.rodziny[id_rodziny].pid_dziecka = pid_dziecka;
                    stan_hali->rejestr_rodzin.rodziny[id_rodziny].sektor = numer_sektora;
                }
                op.sem_op = 1;
                semop_retry_ctx(sem_id, &op, 1, "semop unlock rodzina upd");

                printf("%sKibic %d (RODZIC): Utworzono dziecko PID %d%s\n",
                       KOLOR_CYAN, getpid(), pid_dziecka, KOLOR_RESET);
                rejestr_log("KIBIC", "PID %d rodzic utworzyl dziecko %d", getpid(), pid_dziecka);

                idz_do_bramki(); // Rodzic wchodzi do bramki
                if (waitpid(pid_dziecka, NULL, 0) == -1) {
                    perror("waitpid dziecko");
                }
            } else {
                perror("fork dziecko");
            }
        }
    } else {
        // Standardowy kibic (ew. VIP) z 1-2 biletami
        moj_wiek = (rand() % 47) + 18;
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

        sprobuj_kupic_bilet(); // Najpierw zakup biletu
        if (ma_bilet) {
            if (!jestem_vip && liczba_biletow == 2) {
                // Drugi bilet = "kolega" jako osobny proces
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
                    if (waitpid(pid_kolegi, NULL, 0) == -1) {
                        perror("waitpid kolega");
                    }
                } else {
                    perror("fork kolega");
                }
            } else {
                idz_do_bramki();
            }
        }
    }

    return 0;
}