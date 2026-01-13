#include "common.h"

int shm_id = -1;
StanHali *stan_hali = NULL;

void obsluga_wyjscia() {
    if (stan_hali != NULL) {
        shmdt(stan_hali);
    }
}

void obsluga_sigint(int sig) {
    (void)sig;
    exit(0);
}

void inicjalizuj() {
    key_t klucz_shm = ftok(".", KLUCZ_SHM);
    SPRAWDZ(klucz_shm);

    shm_id = shmget(klucz_shm, sizeof(StanHali), 0600);
    SPRAWDZ(shm_id);

    stan_hali = (StanHali*)shmat(shm_id, NULL, 0);
    if (stan_hali == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }
}

void wyslij_blokade() {
    int nr_sektora;
    printf("Podaj numer sektora do zablokowania (0-%d): ", LICZBA_SEKTOROW - 1);
    if (scanf("%d", &nr_sektora) != 1) return;

    if (nr_sektora < 0 || nr_sektora >= LICZBA_SEKTOROW) {
        printf("Bledny numer sektora.\n");
        return;
    }

    pid_t pid = stan_hali->pidy_pracownikow[nr_sektora];
    if (pid > 0) {
        if (kill(pid, SYGNAL_BLOKADA_SEKTORA) == 0) {
            printf("Wyslano sygnal BLOKADA do pracownika sektora %d (PID: %d)\n", nr_sektora, pid);
        } else {
            perror("kill");
        }
    } else {
        printf("Brak aktywnego pracownika w sektorze %d.\n", nr_sektora);
    }
}

void wyslij_odblokowanie() {
    int nr_sektora;
    printf("Podaj numer sektora do odblokowania (0-%d): ", LICZBA_SEKTOROW - 1);
    if (scanf("%d", &nr_sektora) != 1) return;

    if (nr_sektora < 0 || nr_sektora >= LICZBA_SEKTOROW) {
        printf("Bledny numer sektora.\n");
        return;
    }

    pid_t pid = stan_hali->pidy_pracownikow[nr_sektora];
    if (pid > 0) {
        if (kill(pid, SYGNAL_ODBLOKOWANIE_SEKTORA) == 0) {
            printf("Wyslano sygnal ODBLOKOWANIE do pracownika sektora %d (PID: %d)\n", nr_sektora, pid);
        } else {
            perror("kill");
        }
    } else {
        printf("Brak aktywnego pracownika w sektorze %d.\n", nr_sektora);
    }
}

void zarzadzaj_ewakuacja() {
    printf("UWAGA: OGLASZAM EWAKUACJE HALI!\n");
    stan_hali->ewakuacja_trwa = 1;
    
    if (kill(0, SYGNAL_EWAKUACJA) == -1) {
        perror("kill ewakuacja");
    }
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

    inicjalizuj();

    printf("Kierownik uruchomiony. PID: %d\n", getpid());
    stan_hali->pid_kierownika = getpid();

    while (1) {
        printf("\n--- PANEL KIEROWNIKA ---\n");
        printf("1. Zablokuj sektor\n");
        printf("2. Odblokuj sektor\n");
        printf("3. Oglos ewakuacje\n");
        printf("4. Wyjscie\n");
        printf("Wybierz opcje: ");

        int opcja;
        if (scanf("%d", &opcja) != 1) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            continue;
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
                exit(0);
            default:
                printf("Nieznana opcja.\n");
        }
        sleep(1);
    }

    return 0;
}