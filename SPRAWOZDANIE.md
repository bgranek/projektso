# Sprawozdanie z projektu: Symulator Hali Widowiskowo-Sportowej

**Temat:** 18 - Hala widowiskowo-sportowa
**Przedmiot:** Systemy Operacyjne
**Język programowania:** C

---

## 1. Cel projektu

Celem projektu jest stworzenie symulatora hali widowiskowo-sportowej, w której odbywa się mecz finałowy siatkówki. Symulacja obejmuje:

- Obsługę kibiców przybywających na halę (kupowanie biletów, kontrola bezpieczeństwa, zajmowanie miejsc)
- Dynamiczne zarządzanie kasami biletowymi (otwieranie/zamykanie w zależności od liczby kibiców w kolejce)
- Kontrolę bezpieczeństwa przy wejściach do sektorów (wykrywanie przedmiotów niebezpiecznych, separacja drużyn)
- Obsługę sygnałów od kierownika (blokada sektora, odblokowanie, ewakuacja)
- Rejestrowanie przebiegu symulacji w pliku logów

Projekt demonstruje praktyczne zastosowanie mechanizmów IPC (Inter-Process Communication) systemu UNIX/Linux: pamięci współdzielonej, semaforów System V, kolejek komunikatów, łącz nazwanych (FIFO) oraz gniazd sieciowych.

---

## 2. Założenia projektowe

### 2.1 Parametry hali

| Parametr | Wartość | Opis |
|----------|---------|------|
| Pojemność K | 80 - 100000 | Całkowita pojemność hali (podzielna przez 8) |
| Sektory zwykłe | 8 | Każdy o pojemności K/8 |
| Sektor VIP | 1 | Pojemność < 0.3% * K |
| Kasy | 10 | Dynamicznie aktywowane (min. 2 zawsze czynne) |
| Stanowiska kontroli | 2 na sektor | Max 3 osoby jednocześnie na stanowisku |

### 2.2 Zasady działania kas

- Zawsze działają minimum 2 stanowiska kasowe
- Na każdych K/10 kibiców w kolejce przypada minimum 1 czynne stanowisko
- Kasa zamykana gdy kolejka < (K/10)*(N-1), gdzie N to liczba czynnych kas
- Jeden kibic może kupić maksymalnie 2 bilety w tym samym sektorze
- Osoby VIP kupują bilet omijając kolejkę

### 2.3 Zasady kontroli bezpieczeństwa

- Do każdego z 8 sektorów osobne wejście z 2 stanowiskami kontroli
- Max 3 osoby na stanowisku kontroli jednocześnie
- Jeśli >1 osoba na stanowisku - muszą być kibicami tej samej drużyny
- Kibic może przepuścić max 5 innych kibiców (potem frustracja)
- VIP wchodzą osobnym wejściem bez kontroli
- Dzieci <15 lat wchodzą pod opieką osoby dorosłej

### 2.4 Sygnały kierownika

| Sygnał | Akcja |
|--------|-------|
| SIGRTMIN+1 | Blokada wejścia do sektora |
| SIGRTMIN+2 | Odblokowanie wejścia do sektora |
| SIGRTMIN+3 | Ewakuacja całej hali |

---

## 3. Uruchomienie programu

### 3.1 Kompilacja

```bash
make clean && make
```

### 3.2 Uruchomienie symulatora

```bash
./main [OPCJE]
```

### 3.3 Opcje uruchomienia

| Opcja | Opis | Domyślnie | Zakres |
|-------|------|-----------|--------|
| `-k <liczba>` | Pojemność hali K | 1600 | 80 - 100000 (podzielne przez 8) |
| `-t <sekundy>` | Czas do rozpoczęcia meczu | 30 | 10 - 3600 |
| `-d <sekundy>` | Czas trwania meczu | 60 | 10 - 3600 |
| `-h` | Wyświetl pomoc | - | - |

### 3.4 Przykłady uruchomienia

```bash
./main                        # Domyślne parametry (K=1600, t=30s, d=60s)
./main -k 800                 # Pojemność 800 kibiców
./main -k 3200 -t 60 -d 120   # K=3200, mecz za 60s, trwa 120s
```

### 3.5 Uruchomienie kierownika (w osobnym terminalu)

```bash
./kierownik
```

### 3.6 Monitor zewnętrzny (opcjonalnie)

```bash
./monitor localhost 9999
```

---

## 4. Opis plików źródłowych

| Plik | Odpowiedzialność |
|------|-----------------|
| `main.c` | Proces główny - inicjalizacja zasobów IPC, uruchamianie procesów potomnych (kasjerzy, pracownicy, kibice), zarządzanie fazami meczu, serwer socket monitoringu |
| `kierownik.c` | Program kierownika hali - interaktywny interfejs do wysyłania sygnałów (blokada/odblokowanie sektorów, ewakuacja), wyświetlanie statusu hali, odbieranie zgłoszeń przez FIFO |
| `kasjer.c` | Proces kasjera - obsługa sprzedaży biletów, komunikacja z kibicami przez kolejki komunikatów, dynamiczna aktywacja/deaktywacja kas |
| `pracownik.c` | Proces pracownika technicznego - kontrola bezpieczeństwa (2 wątki na 2 stanowiska), obsługa sygnałów od kierownika, wysyłanie zgłoszeń przez FIFO |
| `kibic.c` | Proces kibica - kupowanie biletów, przechodzenie kontroli, oglądanie meczu, obsługa ewakuacji, obsługa rodzin (rodzic+dziecko) |
| `monitor.c` | Zewnętrzny monitor - łączy się przez socket i wyświetla status hali |
| `common.h` | Wspólne definicje - stałe, struktury danych, makra semaforów, struktury IPC |
| `rejestr.h` | System logowania - zapis przebiegu symulacji do pliku tekstowego |
| `Makefile` | Skrypt kompilacji |

---

## 5. Opis działania procesów

### 5.1 Proces główny (main)

1. Parsuje argumenty linii poleceń i waliduje parametry
2. Tworzy zasoby IPC (pamięć współdzielona, semafory, kolejka komunikatów, FIFO)
3. Inicjalizuje strukturę `StanHali` w pamięci współdzielonej
4. Uruchamia wątek serwera socket do monitoringu
5. Uruchamia wątek zarządzający fazami meczu (start, trwanie, koniec)
6. Uruchamia procesy kasjerów (10) przez `fork()` + `execl()`
7. Uruchamia procesy pracowników (8) przez `fork()` + `execl()`
8. Generuje procesy kibiców w pętli
9. Po zakończeniu meczu czeka na wyjście wszystkich kibiców
10. Sprząta zasoby IPC i kończy symulację

### 5.2 Kierownik hali

1. Łączy się z pamięcią współdzieloną i otwiera FIFO do odbioru zgłoszeń
2. Uruchamia wątek nasłuchujący na FIFO (zgłoszenia o zakończeniu ewakuacji sektorów)
3. Wyświetla interaktywne menu:
   - Pokaż status hali (sektory, kasy, liczba kibiców)
   - Zablokuj sektor (wysyła SIGRTMIN+1 do pracownika)
   - Odblokuj sektor (wysyła SIGRTMIN+2 do pracownika)
   - Zarządź ewakuację (wysyła SIGRTMIN+3 do wszystkich pracowników)

### 5.3 Kasjer

1. Łączy się z zasobami IPC
2. Inicjalnie kasy 0 i 1 są aktywne, pozostałe czekają
3. W pętli głównej:
   - Aktualizuje status kasy (kasa 0 zarządza dynamiką wszystkich kas)
   - Jeśli kasa nieaktywna - czeka na semaforze `SEM_KASA(id)`
   - Odbiera komunikat od kibica z kolejki komunikatów
   - Sprawdza dostępność miejsc w żądanym sektorze
   - Wysyła odpowiedź z przydzielonym sektorem lub odmową
4. Kończy gdy wszystkie bilety sprzedane lub symulacja zakończona

### 5.4 Pracownik techniczny

1. Łączy się z zasobami IPC
2. Rejestruje swój PID w pamięci współdzielonej
3. Uruchamia wątek obsługi sygnałów (sigwait):
   - SIGRTMIN+1: ustawia blokadę sektora
   - SIGRTMIN+2: usuwa blokadę sektora
   - SIGRTMIN+3: inicjuje ewakuację
4. Uruchamia 2 wątki stanowisk kontroli, każdy:
   - Czeka na semaforze `SEM_PRACA(sektor)` na kibica
   - Sprawdza czy kibic ma niebezpieczny przedmiot
   - Sprawdza czy dziecko ma opiekuna
   - Kontroluje zgodność drużyn na stanowisku
   - Sygnalizuje kibicom zakończenie kontroli
5. Po ewakuacji wysyła komunikat przez FIFO do kierownika

### 5.5 Kibic

Kibice dzielą się na kategorie:
- **Zwykły** - kupuje 1-2 bilety, może mieć kolegę
- **VIP** - omija kolejkę do kasy, osobne wejście
- **Rodzic** - kupuje bilet i tworzy proces dziecka
- **Dziecko** - wchodzi razem z rodzicem
- **Kolega** - otrzymuje bilet od innego kibica

Przebieg:
1. Losuje kategorię i drużynę (A lub B)
2. Idzie do kasy:
   - Wybiera kasę z najkrótszą kolejką
   - Wysyła żądanie biletu przez kolejkę komunikatów
   - Odbiera odpowiedź (sektor lub odmowa)
3. Idzie do bramki (kontrola bezpieczeństwa):
   - Zajmuje miejsce na stanowisku
   - Czeka na kontrolę (semafor `SEM_SLOT`)
   - Jeśli inny kibic innej drużyny - przepuszcza go (max 5 razy)
4. Wchodzi na sektor i ogląda mecz (czeka na `SEM_FAZA_MECZU`)
5. Po meczu lub ewakuacji opuszcza halę

---

## 6. Semafory

Projekt wykorzystuje **146 semaforów System V** zorganizowanych w następujące grupy:

### 6.1 Semafor główny (mutex)

| Indeks | Nazwa | Opis |
|--------|-------|------|
| 0 | SEM_MUTEX | Globalny mutex do ochrony sekcji krytycznych przy dostępie do pamięci współdzielonej |

### 6.2 Semafory stanowisk kontroli (SEM_SLOT)

| Indeksy | Wzór | Opis |
|---------|------|------|
| 2-49 | `SEM_SLOT(sektor, stanowisko, miejsce)` | 8 sektorów × 2 stanowiska × 3 miejsca = 48 semaforów. Kibic czeka na swoim semaforze aż pracownik zakończy kontrolę |

### 6.3 Semafory sektorów (SEM_SEKTOR)

| Indeksy | Wzór | Opis |
|---------|------|------|
| 50-57 | `SEM_SEKTOR(s)` | Kontrola dostępu do sektora - kibic czeka jeśli sektor zablokowany |

### 6.4 Semafory rodzin (SEM_RODZINA)

| Indeksy | Wzór | Opis |
|---------|------|------|
| 58-107 | `SEM_RODZINA(id)` | Synchronizacja rodzica i dziecka przy bramce (50 rodzin max) |

### 6.5 Semafor fazy meczu

| Indeks | Nazwa | Opis |
|--------|-------|------|
| 108 | SEM_FAZA_MECZU | Kibice czekający na koniec meczu. Wartość zwiększana masowo po zakończeniu meczu |

### 6.6 Semafory pracy pracowników

| Indeksy | Wzór | Opis |
|---------|------|------|
| 109-116 | `SEM_PRACA(s)` | Pracownik czeka na tym semaforze na kibica do kontroli |

### 6.7 Semafory bramek

| Indeksy | Wzór | Opis |
|---------|------|------|
| 117-132 | `SEM_BRAMKA(sektor, stanowisko)` | Synchronizacja zajmowania miejsc na stanowisku kontroli |

### 6.8 Semafory kas

| Indeksy | Wzór | Opis |
|---------|------|------|
| 133-142 | `SEM_KASA(id)` | Kasjer czeka na tym semaforze gdy kasa jest nieaktywna |

### 6.9 Semafory pomocnicze

| Indeks | Nazwa | Opis |
|--------|-------|------|
| 143 | SEM_KIBIC_WYSZEDL | Sygnalizacja wyjścia kibica z hali |
| 144 | SEM_EWAKUACJA_KONIEC | Sygnalizacja zakończenia ewakuacji |
| 145 | SEM_START_MECZU | Kibice czekający na rozpoczęcie meczu (wartość 0 = mecz trwa) |

---

## 7. Pamięć współdzielona

Projekt wykorzystuje jeden segment pamięci współdzielonej zawierający strukturę `StanHali`:

### 7.1 Parametry hali

| Pole | Typ | Opis |
|------|-----|------|
| `pojemnosc_calkowita` | int | Całkowita pojemność K |
| `pojemnosc_sektora` | int | Pojemność jednego sektora (K/8) |
| `pojemnosc_vip` | int | Pojemność sektora VIP |
| `limit_vip` | int | Maksymalna liczba VIP (<0.3% * K) |

### 7.2 Stan sektorów

| Pole | Typ | Opis |
|------|-----|------|
| `liczniki_sektorow[9]` | int[] | Liczba sprzedanych biletów w każdym sektorze |
| `osoby_w_sektorze[9]` | int[] | Aktualna liczba osób w każdym sektorze |
| `sektor_zablokowany[8]` | int[] | Flagi blokady sektorów |
| `sektor_ewakuowany[8]` | int[] | Flagi ewakuacji sektorów |

### 7.3 Stan kas

| Pole | Typ | Opis |
|------|-----|------|
| `kasa_aktywna[10]` | int[] | Flagi aktywności kas |
| `kasa_zamykanie[10]` | int[] | Flagi kas w trakcie zamykania |
| `kolejka_dlugosc[10]` | int[] | Długość kolejki do każdej kasy |
| `wszystkie_bilety_sprzedane` | int | Flaga wyprzedania biletów |
| `aktywne_kasy` | int | Liczba aktywnych kas |

### 7.4 PID-y procesów

| Pole | Typ | Opis |
|------|-----|------|
| `pidy_kasjerow[10]` | pid_t[] | PID-y procesów kasjerów |
| `pidy_pracownikow[8]` | pid_t[] | PID-y procesów pracowników |
| `pid_kierownika` | pid_t | PID kierownika |
| `pid_main` | pid_t | PID procesu głównego |

### 7.5 Stan symulacji

| Pole | Typ | Opis |
|------|-----|------|
| `suma_kibicow_w_hali` | int | Aktualna liczba kibiców na hali |
| `liczba_vip` | int | Aktualna liczba VIP na hali |
| `ewakuacja_trwa` | int | Flaga trwającej ewakuacji |
| `faza_meczu` | FazaMeczu | PRZED_MECZEM / MECZ / PO_MECZU |
| `czas_startu_symulacji` | time_t | Timestamp rozpoczęcia |
| `czas_do_meczu` | int | Sekundy do rozpoczęcia meczu |
| `czas_trwania_meczu` | int | Czas trwania meczu w sekundach |

### 7.6 Struktury bramek i rodzin

| Pole | Typ | Opis |
|------|-----|------|
| `bramki[8][2]` | Bramka[][] | 8 sektorów × 2 stanowiska - stan kontroli |
| `rejestr_rodzin` | RejestrRodzin | Rejestr par rodzic-dziecko |

---

## 8. Kolejki komunikatów

Projekt wykorzystuje jedną kolejkę komunikatów System V do komunikacji kibic-kasjer:

### 8.1 Komunikat zapytania (kibic → kasjer)

```c
typedef struct {
    long mtype;          // TYP_KOMUNIKATU_ZAPYTANIE (1)
    pid_t pid_kibica;    // PID kibica do odpowiedzi
    int id_druzyny;      // DRUZYNA_A lub DRUZYNA_B
    int czy_vip;         // 1 jeśli VIP
    int liczba_biletow;  // 1 lub 2
    int nr_sektora;      // -1 = dowolny, 0-7 = konkretny
    int nr_kasy;         // Numer wybranej kasy
} KomunikatBilet;
```

### 8.2 Komunikat odpowiedzi (kasjer → kibic)

```c
typedef struct {
    long mtype;              // PID kibica
    int przydzielony_sektor; // Numer przydzielonego sektora
    int liczba_sprzedanych;  // Ile biletów sprzedano
    int czy_sukces;          // 1 = sukces, 0 = brak miejsc
} OdpowiedzBilet;
```

### 8.3 Przepływ komunikacji

1. Kibic wybiera kasę z najkrótszą kolejką
2. Kibic wysyła `KomunikatBilet` z `mtype = TYP_KOMUNIKATU_ZAPYTANIE`
3. Kasjer odbiera komunikat (`msgrcv` z typem 1)
4. Kasjer sprawdza dostępność miejsc i wysyła `OdpowiedzBilet` z `mtype = pid_kibica`
5. Kibic odbiera odpowiedź (`msgrcv` z typem = swój PID)

---

## 9. Łącza nazwane (FIFO)

Projekt wykorzystuje jedno łącze FIFO do komunikacji pracownik → kierownik:

### 9.1 Ścieżka FIFO

```
/tmp/hala_fifo_ewakuacja
```

### 9.2 Struktura komunikatu

```c
typedef struct {
    int typ;                // 1 = ewakuacja zakończona
    int nr_sektora;         // Numer sektora
    pid_t pid_pracownika;   // PID pracownika
    char wiadomosc[128];    // Opis tekstowy
} KomunikatFifo;
```

### 9.3 Zastosowanie

- Pracownik po ewakuacji wszystkich kibiców z sektora wysyła komunikat do kierownika
- Kierownik w osobnym wątku odbiera komunikaty i aktualizuje status ewakuacji
- Po otrzymaniu komunikatów ze wszystkich 8 sektorów kierownik wie, że hala jest pusta

---

## 10. Gniazda sieciowe (sockets)

Projekt zawiera serwer TCP do zewnętrznego monitoringu:

### 10.1 Parametry

| Parametr | Wartość |
|----------|---------|
| Port | 9999 |
| Protokół | TCP (SOCK_STREAM) |
| Adres | INADDR_ANY (wszystkie interfejsy) |

### 10.2 Format odpowiedzi

```
HALA|OSOB_W_HALI:123|SUMA_BILETOW:456|BILETY:DOSTEPNE|EWAKUACJA:NIE|FAZA:1|S0:50/200(osob:48)|...|VIP:3/4(osob:3)
```

### 10.3 Użycie monitora

```bash
./monitor localhost 9999
```

---

## 11. Obsługa sygnałów

### 11.1 Sygnały czasu rzeczywistego

| Sygnał | Nadawca | Odbiorca | Akcja |
|--------|---------|----------|-------|
| SIGRTMIN+1 | Kierownik | Pracownik | Blokada wejścia do sektora |
| SIGRTMIN+2 | Kierownik | Pracownik | Odblokowanie wejścia |
| SIGRTMIN+3 | Kierownik | Pracownik | Rozpoczęcie ewakuacji |

### 11.2 Implementacja w pracowniku

Pracownik używa dedykowanego wątku z `sigwait()`:

1. Blokuje sygnały SIGRTMIN+1/2/3 w masce sygnałów
2. Wątek czeka na `sigwait()` na te sygnały
3. Po otrzymaniu sygnału ustawia odpowiednie flagi w pamięci współdzielonej
4. Budzi wątki stanowisk przez `pthread_cond_broadcast()`

### 11.3 Obsługa SIGTERM/SIGINT

- Proces główny ignoruje SIGTERM podczas zamykania
- Wysyła SIGTERM do grupy procesów `kill(0, SIGTERM)`
- Kierownik obsługuje SIGINT do czystego zamknięcia

---

## 12. Obsługa plików

### 12.1 Plik logów (symulacja.log)

System logowania w `rejestr.h` wykorzystuje:

| Funkcja | Użycie |
|---------|--------|
| `creat()` | Tworzenie/czyszczenie pliku logów |
| `open()` | Otwarcie pliku do dopisywania (O_WRONLY \| O_APPEND) |
| `write()` | Zapis logów |
| `close()` | Zamknięcie pliku |
| `flock()` | Blokada pliku przy współbieżnym zapisie |

### 12.2 Format logów

```
[HH:MM:SS] [KATEGORIA ] [PID:XXXXXX] Treść komunikatu
```

Kategorie: MAIN, KIEROWNIK, KASJER, PRACOWNIK, KIBIC, KONTROLA, STATYSTYKI, SEKTOR

---

## 13. Wątki (pthreads)

### 13.1 Wątki w procesie głównym

| Wątek | Funkcja | Opis |
|-------|---------|------|
| `watek_czasu_meczu` | Zarządzanie fazami | Czeka na start meczu, zmienia fazę, czeka na koniec |
| `watek_serwera_socket` | Serwer monitoringu | Obsługuje połączenia TCP od monitora |
| `watek_generator_kibicow` | Generator kibiców | Tworzy procesy kibiców przez fork()+execl() |

### 13.2 Wątki w procesie pracownika

| Wątek | Funkcja | Opis |
|-------|---------|------|
| `watek_stanowiska(0)` | Stanowisko kontroli 0 | Kontrola kibiców |
| `watek_stanowiska(1)` | Stanowisko kontroli 1 | Kontrola kibiców |
| `watek_sygnalow` | Obsługa sygnałów | sigwait() na SIGRTMIN+1/2/3 |

### 13.3 Synchronizacja wątków

| Mechanizm | Użycie |
|-----------|--------|
| `pthread_mutex_t` | Ochrona struktur Bramka, RejestrRodzin, mutex_sektor |
| `pthread_cond_t` | Sygnalizacja zmian blokady/ewakuacji (cond_sektor) |
| `pthread_create()` | Tworzenie wątków |
| `pthread_join()` | Oczekiwanie na zakończenie wątków stanowisk |
| `pthread_detach()` | Wątek socket i sygnałów działają niezależnie |

---

## 14. Obsługa błędów

### 14.1 Makro SPRAWDZ

```c
#define SPRAWDZ(x) \
    do { \
        if ((x) == -1) { \
            perror(#x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)
```

Używane przy wszystkich wywołaniach systemowych (shmget, semget, msgget, fork, itp.)

### 14.2 Walidacja danych wejściowych

```c
#define WALIDUJ_ZAKRES(wartosc, min, max, nazwa) \
    do { \
        if ((wartosc) < (min) || (wartosc) > (max)) { \
            fprintf(stderr, "Blad: %s musi byc w zakresie %d-%d\n", ...); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)
```

### 14.3 Bezpieczne parsowanie

Funkcja `parsuj_int()` - walidacja argumentów z linii poleceń
Funkcja `bezpieczny_scanf_int()` - walidacja danych od użytkownika w kierowniku

### 14.4 Obsługa errno

- Wszystkie funkcje systemowe sprawdzają wartość zwracaną
- W przypadku błędu wywoływane `perror()` z opisem kontekstu
- Specjalna obsługa `EINTR` - ponawianie operacji przerwanej sygnałem
- Funkcja pomocnicza `semop_retry_ctx()` automatycznie ponawia semop przy EINTR

---

## 15. Co udało się zrealizować

### 15.1 Wymagania obowiązkowe - ZREALIZOWANE

- [x] 8 sektorów + sektor VIP
- [x] 10 kas z dynamiczną aktywacją (min 2 zawsze czynne)
- [x] 2 stanowiska kontroli na sektor (max 3 osoby)
- [x] Kontrola drużyn na stanowisku (ta sama drużyna)
- [x] Limit przepuszczeń (max 5) z obsługą frustracji
- [x] Obsługa VIP (osobne wejście, omijanie kolejki)
- [x] Obsługa rodzin (dzieci <15 lat z opiekunem)
- [x] Sygnały blokady/odblokowania/ewakuacji
- [x] Komunikacja FIFO (pracownik → kierownik)
- [x] Logowanie do pliku tekstowego

### 15.2 Wymagania techniczne - ZREALIZOWANE

- [x] Język C
- [x] fork() + exec() dla procesów
- [x] Pamięć współdzielona (shmget, shmat, shmdt, shmctl)
- [x] Semafory System V (semget, semctl, semop)
- [x] Kolejki komunikatów (msgget, msgsnd, msgrcv, msgctl)
- [x] Łącza nazwane FIFO (mkfifo)
- [x] Gniazda sieciowe (socket, bind, listen, accept)
- [x] Wątki POSIX (pthread_create, pthread_join, pthread_mutex, pthread_cond)
- [x] Obsługa sygnałów (sigaction, sigwait, kill)
- [x] Obsługa błędów (perror, errno)
- [x] Walidacja danych wejściowych
- [x] Minimalne prawa dostępu IPC (0600)
- [x] Usuwanie zasobów po zakończeniu

---

## 16. Elementy specjalne

### 16.1 Kolorowy interfejs terminala

Symulacja wykorzystuje kody ANSI do kolorowania wyjścia:
- Zielony - sukces, normalne operacje
- Czerwony - błędy, ostrzeżenia, frustracja
- Żółty - oczekiwanie, blokada
- Cyan - informacje VIP, dzieci
- Magenta - sektor VIP
- Bold - nagłówki, ważne komunikaty

### 16.2 Serwer monitoringu TCP

Zewnętrzny program `monitor.c` może połączyć się przez socket i otrzymać aktualny stan hali w formacie tekstowym - przydatne do debugowania i wizualizacji.

### 16.3 System logowania

Kompletny system logowania z:
- Timestampami
- Kategoryzacją komunikatów
- PID-ami procesów
- Blokadą pliku (flock) dla bezpieczeństwa współbieżnego
- Automatycznym nagłówkiem/stopką

### 16.4 Obsługa rodzin

Zaawansowana synchronizacja rodzic-dziecko:
- Rejestr rodzin w pamięci współdzielonej
- Dedykowane semafory dla każdej rodziny
- Rodzic czeka na dziecko przy bramce
- Dziecko nie może wejść bez rodzica

### 16.5 Dynamiczne zarządzanie kasami

Implementacja zgodna z wymaganiami:
- Algorytm aktywacji/deaktywacji kas
- Kasa 0 jako "koordynator" - podejmuje decyzje dla wszystkich
- Płynne zamykanie (czeka na opróżnienie kolejki)

---

## 17. Opisy ważniejszych fragmentów kodu

### 17.1 Inicjalizacja zasobów IPC (main.c)

Proces główny tworzy wszystkie zasoby IPC przed uruchomieniem procesów potomnych:

```c
// Pamięć współdzielona
shm_id = shmget(klucz_shm, sizeof(StanHali), IPC_CREAT | 0600);
stan_hali = (StanHali*)shmat(shm_id, NULL, 0);

// Semafory (146 semaforów)
sem_id = semget(klucz_sem, SEM_TOTAL, IPC_CREAT | 0600);

// Kolejka komunikatów
msg_id = msgget(klucz_msg, IPC_CREAT | 0600);

// FIFO
mkfifo(FIFO_PRACOWNIK_KIEROWNIK, 0600);
```

### 17.2 Dynamika kas (kasjer.c)

Kasa 0 zarządza wszystkimi kasami na podstawie długości kolejek:

```c
// Oblicz potrzebną liczbę kas
int K = suma_kolejek;  // suma kibiców we wszystkich kolejkach
int limit = pojemnosc_calkowita / 10;
int potrzebne = (K + limit - 1) / limit;  // ceiling division
if (potrzebne < 2) potrzebne = 2;  // zawsze min 2

// Aktywuj kasę jeśli za mało
if (potrzebne > N) {
    // znajdź nieaktywną kasę i aktywuj
    stan_hali->kasa_aktywna[i] = 1;
    semop(sem_id, &wake, 1);  // obudź kasjera
}
// Deaktywuj kasę jeśli za dużo
else if (potrzebne < N && N > 2) {
    stan_hali->kasa_zamykanie[i] = 1;  // oznacz do zamknięcia
}
```

### 17.3 Kontrola drużyn na stanowisku (pracownik.c)

Pracownik sprawdza czy kibice na stanowisku są tej samej drużyny:

```c
// Jeśli stanowisko zajęte przez inną drużynę
if (b->obecna_druzyna != 0 && b->obecna_druzyna != druzyna_kibica) {
    // Kibic musi poczekać lub przepuścić
    przepuszczeni++;
    if (przepuszczeni >= MAX_PRZEPUSZCZEN) {
        // Frustracja - kibic wpycha się siłą
    }
}
```

### 17.4 Synchronizacja rodziny (kibic.c)

Rodzic i dziecko synchronizują się przy bramce:

```c
// Rodzic rejestruje się przy bramce
rejestr->rodziny[id].rodzic_przy_bramce = 1;

// Dziecko czeka na rodzica
while (!rejestr->rodziny[id].rodzic_przy_bramce) {
    semop(sem_id, &wait_rodzina, 1);
}

// Rodzic czeka na dziecko
while (!rejestr->rodziny[id].dziecko_przy_bramce) {
    semop(sem_id, &wait_rodzina, 1);
}
```

### 17.5 Obsługa ewakuacji (pracownik.c)

Wątek sygnałów obsługuje żądanie ewakuacji:

```c
void* watek_sygnalow(void *arg) {
    sigset_t set;
    sigaddset(&set, SYGNAL_EWAKUACJA);

    while (1) {
        int sig;
        sigwait(&set, &sig);

        if (sig == SYGNAL_EWAKUACJA) {
            stan_hali->ewakuacja_trwa = 1;
            pthread_cond_broadcast(&cond_sektor);

            // Czekaj aż wszyscy wyjdą
            while (stan_hali->osoby_w_sektorze[id] > 0) {
                // ...
            }

            // Wyślij potwierdzenie przez FIFO
            write(fifo_fd, &komunikat, sizeof(komunikat));
        }
    }
}
```

### 17.6 Komunikacja kibic-kasjer (kibic.c, kasjer.c)

Kibic wysyła żądanie biletu:

```c
KomunikatBilet kom;
kom.mtype = TYP_KOMUNIKATU_ZAPYTANIE;
kom.pid_kibica = getpid();
kom.liczba_biletow = ile_biletow;
msgsnd(msg_id, &kom, sizeof(kom) - sizeof(long), 0);

// Czekaj na odpowiedź
OdpowiedzBilet odp;
msgrcv(msg_id, &odp, sizeof(odp) - sizeof(long), getpid(), 0);
```

---

## 18. Znane ograniczenia

1. Maksymalna liczba rodzin: 50 (ograniczenie tablicy)
2. Maksymalna pojemność: 100000 (ograniczenie walidacji)
3. Monitor zewnętrzny wymaga osobnego uruchomienia
4. Test ewakuacji wymaga ręcznego uruchomienia kierownika

---

## 19. Testy

Poniżej cztery testy weryfikujące kluczowe funkcjonalności systemu. Każdy test znajduje się w osobnym folderze (`test1/`, `test2/`, `test3/`, `test4/`) z zmodyfikowaną wersją kodu źródłowego.

### Test 1 - Weryfikacja pojemności hali (K=1600)

- **Cel:** Sprawdzenie czy hala prawidłowo ogranicza liczbę kibiców do pojemności K=1600.
- **Założenia:**
  - Generujemy 10 000 kibiców (bez opóźnienia, z barierą)
  - Nikt nie ma noża (`SZANSA_NA_PRZEDMIOT = 0`)
  - Nikt nie jest VIP (wyłączony w kodzie)
  - Nikt nie ma dziecka (`SZANSA_RODZINY = 0`)
- **Oczekiwany rezultat:** Dokładnie **1600 kibiców** wchodzi do hali (pojemność K), każdy sektor ma 200 osób.
- **Wynik:** **OK** - monitor potwierdza 1600 osób w hali, wszystkie sektory pełne.

Dowód z monitora (`test1/dowod_monitor.txt`):
```
Status: ONLINE

Osob w hali: 1600
Biletow sprzedanych: 1600
Bilety: DOSTEPNE
Ewakuacja: NIE
Faza: MECZ TRWA

Sektory (bilety/pojemnosc | osoby w srodku):
  [0] bilety: 200/200 | osoby: 200
  [1] bilety: 200/200 | osoby: 200
  [2] bilety: 200/200 | osoby: 200
  [3] bilety: 200/200 | osoby: 200
  [4] bilety: 200/200 | osoby: 200
  [5] bilety: 200/200 | osoby: 200
  [6] bilety: 200/200 | osoby: 200
  [7] bilety: 200/200 | osoby: 200
VIP: 0/4(osob:0)
```

Weryfikacja komendą (z `test1/symulacja.log`):
```bash
$ grep -c "Sprzedano bilet" test1/symulacja.log
1600

$ grep -c "Wpuszczono PID" test1/symulacja.log
1600
```

Statystyki z logu:
- Sprzedanych biletów: **1600** (pojemność K)
- Wpuszczonych kibiców: **1600** (pojemność K)
- Wykrytych noży: **0**

---

### Test 2 - Weryfikacja kontroli bezpieczeństwa (wszyscy mają noże)

- **Cel:** Sprawdzenie czy kontrola bezpieczeństwa prawidłowo odrzuca kibiców z niebezpiecznymi przedmiotami.
- **Założenia:**
  - Generujemy 10 000 kibiców (bez opóźnienia, z barierą)
  - **Wszyscy mają nóż** (`SZANSA_NA_PRZEDMIOT = 100`)
  - Nikt nie jest VIP (wyłączony - VIP omija kontrolę)
  - Nikt nie ma dziecka (`SZANSA_RODZINY = 0`)
- **Oczekiwany rezultat:** **0 kibiców** wchodzi do hali - wszyscy zostają odrzuceni na kontroli bezpieczeństwa.
- **Wynik:** **OK** - monitor potwierdza 0 osób w hali mimo 1600 sprzedanych biletów.

Dowód z monitora (`test2/dowod_monitor.txt`):
```
Status: ONLINE

Osob w hali: 0
Biletow sprzedanych: 1600
Bilety: DOSTEPNE
Ewakuacja: NIE
Faza: MECZ TRWA

Sektory (bilety/pojemnosc | osoby w srodku):
  [0] bilety: 200/200 | osoby: 0
  [1] bilety: 200/200 | osoby: 0
  [2] bilety: 200/200 | osoby: 0
  [3] bilety: 200/200 | osoby: 0
  [4] bilety: 200/200 | osoby: 0
  [5] bilety: 200/200 | osoby: 0
  [6] bilety: 200/200 | osoby: 0
  [7] bilety: 200/200 | osoby: 0
VIP: 0/4(osob:0)
```

Weryfikacja komendą (z `test2/symulacja.log`):
```bash
$ grep -c "Sprzedano bilet" test2/symulacja.log
1600

$ grep -c "Wykryto noz" test2/symulacja.log
1600

$ grep -c "Wpuszczono PID" test2/symulacja.log
0
```

Statystyki z logu:
- Sprzedanych biletów: **1600**
- Zatrzymanych za nóż: **1600** (wszyscy którzy doszli do kontroli)
- Wpuszczonych kibiców: **0**

---

### Test 3 - Weryfikacja braku głodzenia drużyny mniejszościowej

- **Cel:** Sprawdzenie czy kibic drużyny mniejszościowej (B) nie jest głodzony przez kibiców drużyny większościowej (A).
- **Założenia:**
  - Generujemy 3001 kibiców (bez opóźnienia, z barierą):
    - 3000 kibiców drużyny A
    - 1 kibic drużyny B (generowany jako 1500. w kolejności - w środku)
  - Przypisanie drużyny przez zmienną środowiskową `TEST3_DRUZYNA`
  - Pojemność K=1600 (domyślna)
  - Nikt nie ma noża, nikt nie jest VIP
- **Oczekiwany rezultat:** Kibic drużyny B **wchodzi do hali** - nie jest głodzony mimo przewagi drużyny A.
- **Wynik:** **OK** - log potwierdza wpuszczenie kibica drużyny B.

Dowód w logu (`test3/symulacja.log`):
```
[14:40:48] [KONTROLA  ] [PID:70802 ] Sektor 2 Stan 1: Wpuszczono PID 72312 druzyna B
```

Weryfikacja komendą:
```bash
$ grep -c "Wpuszczono PID" test3/symulacja.log
3001

# Pierwszy wpuszczony kibic (linia 1824):
$ grep -n "Wpuszczono PID" test3/symulacja.log | head -1
1824:[14:40:45] [KONTROLA  ] [PID:70836 ] Sektor 7 Stan 1: Wpuszczono PID 71162 druzyna A

# Kibic druzyny B (linia 13767):
$ grep -n "druzyna B" test3/symulacja.log
13767:[14:40:48] [KONTROLA  ] [PID:70802 ] Sektor 2 Stan 1: Wpuszczono PID 72312 druzyna B

# Ostatni wpuszczony kibic (linia 18055):
$ grep -n "Wpuszczono PID" test3/symulacja.log | tail -1
18055:[14:40:49] [KONTROLA  ] [PID:70801 ] Sektor 1 Stan 0: Wpuszczono PID 72392 druzyna A
```

Statystyki z logu:
- Wpuszczonych kibiców: **3001** (w tym 1 drużyna B)
- Wpuszczanie trwało od linii **1824** do **18055** (zakres 16231 linii)
- Kibic drużyny B wszedł w linii **13767** (~74% procesu wpuszczania)
- **Kibic B nie był głodzony** - wszedł w trakcie trwania wpuszczania, nie na końcu

---

### Test 4 - Weryfikacja kibiców czekających przy bramkach

- **Cel:** Sprawdzenie czy kibice prawidłowo gromadzą się przy bramkach gdy pracownicy ich nie obsługują.
- **Założenia:**
  - Generujemy 5000 kibiców (bez opóźnienia, z barierą)
  - Pracownicy mają `sleep(100)` **przed** semop - nie obsługują bramek przez 100 sekund (kibice po zakupie biletu gromadzą się przy kontroli, lecz przez nią nie przechodzą)
  - Pojemność K=6000
  - Nikt nie ma noża, nikt nie jest VIP
- **Oczekiwany rezultat:** Po sprzedaży biletów kibice gromadzą się przy bramkach jako aktywne procesy.
- **Wynik:** **OK** - 5000 procesów kibica czeka przy bramkach.

Procedura testu:
1. Uruchomiono `./main -k 6000 -t 90 -d 90`
2. Po sprzedaży 5000 biletów wykonano `Ctrl+Z` (wstrzymanie)
3. Sprawdzono stan procesów i zasobów IPC

Dowód z konsoli (`test4/konsola_dowod.txt`):
```
^Z
[1]+  Stopped                 ./main -k 6000 -t 90 -d 90
vscode ➜ /workspace/test4 (main) $ ipcs

------ Message Queues --------
key        msqid      owner      perms      used-bytes   messages
0x662b0302 27         vscode     600        0            0

------ Shared Memory Segments --------
key        shmid      owner      perms      bytes      nattch     status
0x642b0302 27         vscode     600        3576       5019

------ Semaphore Arrays --------
key        semid      owner      perms      nsems
0x652b0302 27         vscode     600        146

vscode ➜ /workspace/test4 (main) $ pgrep -c kibic
5000
vscode ➜ /workspace/test4 (main) $ pgrep -c kasjer
10
vscode ➜ /workspace/test4 (main) $ pgrep -c pracownik
8
vscode ➜ /workspace/test4 (main) $ pgrep -c main
1
```

Analiza wyników:
- **5000 procesów kibica** aktywnych (czekających przy bramkach)
- **10 procesów kasjer** (wszystkie kasy)
- **8 procesów pracownik** (jeden na sektor)
- **1 proces main** (główny)
- Pamięć współdzielona ma **5019 podłączeń** (nattch) - potwierdzenie aktywnych procesów
- Kolejka komunikatów pusta (0 messages) - wszyscy kibice już kupili bilety

---

## 20. Linki do GitHub

**Repozytorium:** https://github.com/bgranek/projektso

### 20.1 Tworzenie i obsługa plików (creat, open, close, read, write, unlink)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `creat()` | rejestr.h | 34 | [rejestr.h#L34](https://github.com/bgranek/projektso/blob/main/rejestr.h#L34) |
| `open()` | rejestr.h | 44 | [rejestr.h#L44](https://github.com/bgranek/projektso/blob/main/rejestr.h#L44) |
| `open()` | pracownik.c | 167 | [pracownik.c#L167](https://github.com/bgranek/projektso/blob/main/pracownik.c#L167) |
| `open()` | kierownik.c | 57 | [kierownik.c#L57](https://github.com/bgranek/projektso/blob/main/kierownik.c#L57) |
| `write()` | rejestr.h | 66 | [rejestr.h#L66](https://github.com/bgranek/projektso/blob/main/rejestr.h#L66) |
| `write()` | pracownik.c | 182 | [pracownik.c#L182](https://github.com/bgranek/projektso/blob/main/pracownik.c#L182) |
| `read()` | kierownik.c | 77 | [kierownik.c#L77](https://github.com/bgranek/projektso/blob/main/kierownik.c#L77) |
| `close()` | rejestr.h | 101 | [rejestr.h#L101](https://github.com/bgranek/projektso/blob/main/rejestr.h#L101) |
| `close()` | pracownik.c | 191 | [pracownik.c#L191](https://github.com/bgranek/projektso/blob/main/pracownik.c#L191) |
| `unlink()` | main.c | 30 | [main.c#L30](https://github.com/bgranek/projektso/blob/main/main.c#L30) |

### 20.2 Tworzenie procesów (fork, exec, exit, wait)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `fork()` + `execl()` kasjer | main.c | 561-566 | [main.c#L561-L566](https://github.com/bgranek/projektso/blob/main/main.c#L561-L566) |
| `fork()` + `execl()` pracownik | main.c | 580-585 | [main.c#L580-L585](https://github.com/bgranek/projektso/blob/main/main.c#L580-L585) |
| `fork()` + `execl()` kibic | main.c | 419-421 | [main.c#L419-L421](https://github.com/bgranek/projektso/blob/main/main.c#L419-L421) |
| `exit()` | pracownik.c | 84 | [pracownik.c#L84](https://github.com/bgranek/projektso/blob/main/pracownik.c#L84) |
| `wait()` | main.c | 279 | [main.c#L279](https://github.com/bgranek/projektso/blob/main/main.c#L279) |

### 20.3 Tworzenie i obsługa wątków (pthread_*)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `pthread_create()` socket | main.c | 663 | [main.c#L663](https://github.com/bgranek/projektso/blob/main/main.c#L663) |
| `pthread_create()` czas | main.c | 671 | [main.c#L671](https://github.com/bgranek/projektso/blob/main/main.c#L671) |
| `pthread_create()` generator | main.c | 677 | [main.c#L677](https://github.com/bgranek/projektso/blob/main/main.c#L677) |
| `pthread_create()` stanowiska | pracownik.c | 417 | [pracownik.c#L417](https://github.com/bgranek/projektso/blob/main/pracownik.c#L417) |
| `pthread_create()` sygnaly | pracownik.c | 159 | [pracownik.c#L159](https://github.com/bgranek/projektso/blob/main/pracownik.c#L159) |
| `pthread_join()` | main.c | 717-722 | [main.c#L717-L722](https://github.com/bgranek/projektso/blob/main/main.c#L717-L722) |
| `pthread_detach()` socket | main.c | 666 | [main.c#L666](https://github.com/bgranek/projektso/blob/main/main.c#L666) |
| `pthread_detach()` fifo | kierownik.c | 323 | [kierownik.c#L323](https://github.com/bgranek/projektso/blob/main/kierownik.c#L323) |
| `pthread_mutex_lock()` | pracownik.c | 30 | [pracownik.c#L30](https://github.com/bgranek/projektso/blob/main/pracownik.c#L30) |
| `pthread_mutex_unlock()` | pracownik.c | 33 | [pracownik.c#L33](https://github.com/bgranek/projektso/blob/main/pracownik.c#L33) |
| `pthread_cond_broadcast()` | pracownik.c | 32 | [pracownik.c#L32](https://github.com/bgranek/projektso/blob/main/pracownik.c#L32) |
| `pthread_cond_wait()` | pracownik.c | 365 | [pracownik.c#L365](https://github.com/bgranek/projektso/blob/main/pracownik.c#L365) |

### 20.4 Obsługa sygnałów (kill, sigaction, sigwait)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `sigaction()` SIGTERM | kasjer.c | 337-341 | [kasjer.c#L337-L341](https://github.com/bgranek/projektso/blob/main/kasjer.c#L337-L341) |
| `sigaction()` EWAKUACJA | kibic.c | 226-230 | [kibic.c#L226-L230](https://github.com/bgranek/projektso/blob/main/kibic.c#L226-L230) |
| `sigaction()` SIGINT | kierownik.c | 309-313 | [kierownik.c#L309-L313](https://github.com/bgranek/projektso/blob/main/kierownik.c#L309-L313) |
| `sigwaitinfo()` | pracownik.c | 104 | [pracownik.c#L104](https://github.com/bgranek/projektso/blob/main/pracownik.c#L104) |
| `kill()` blokada | kierownik.c | 223 | [kierownik.c#L223](https://github.com/bgranek/projektso/blob/main/kierownik.c#L223) |
| `kill()` odblokowanie | kierownik.c | 258 | [kierownik.c#L258](https://github.com/bgranek/projektso/blob/main/kierownik.c#L258) |
| `kill()` ewakuacja | kierownik.c | 296 | [kierownik.c#L296](https://github.com/bgranek/projektso/blob/main/kierownik.c#L296) |
| `kill()` SIGTERM | main.c | 251 | [main.c#L251](https://github.com/bgranek/projektso/blob/main/main.c#L251) |

### 20.5 Synchronizacja - semafory (ftok, semget, semctl, semop)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `ftok()` | main.c | 116-121 | [main.c#L116-L121](https://github.com/bgranek/projektso/blob/main/main.c#L116-L121) |
| `semget()` tworzenie | main.c | 184 | [main.c#L184](https://github.com/bgranek/projektso/blob/main/main.c#L184) |
| `semget()` dostęp | kasjer.c | 51 | [kasjer.c#L51](https://github.com/bgranek/projektso/blob/main/kasjer.c#L51) |
| `semctl()` inicjalizacja | main.c | 200-219 | [main.c#L200-L219](https://github.com/bgranek/projektso/blob/main/main.c#L200-L219) |
| `semctl()` usunięcie | main.c | 66 | [main.c#L66](https://github.com/bgranek/projektso/blob/main/main.c#L66) |
| `semop()` lock | kasjer.c | 85 | [kasjer.c#L85](https://github.com/bgranek/projektso/blob/main/kasjer.c#L85) |
| `semop()` unlock | kasjer.c | 94 | [kasjer.c#L94](https://github.com/bgranek/projektso/blob/main/kasjer.c#L94) |
| `semop_retry_ctx()` helper | common.h | 356-363 | [common.h#L356-L363](https://github.com/bgranek/projektso/blob/main/common.h#L356-L363) |

### 20.6 Łącza nazwane FIFO (mkfifo)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `mkfifo()` | main.c | 98 | [main.c#L98](https://github.com/bgranek/projektso/blob/main/main.c#L98) |
| FIFO write (pracownik→kierownik) | pracownik.c | 167-194 | [pracownik.c#L167-L194](https://github.com/bgranek/projektso/blob/main/pracownik.c#L167-L194) |
| FIFO read (kierownik) | kierownik.c | 77-134 | [kierownik.c#L77-L134](https://github.com/bgranek/projektso/blob/main/kierownik.c#L77-L134) |

### 20.7 Segmenty pamięci dzielonej (shmget, shmat, shmdt, shmctl)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `shmget()` tworzenie | main.c | 124-132 | [main.c#L124-L132](https://github.com/bgranek/projektso/blob/main/main.c#L124-L132) |
| `shmat()` | main.c | 137 | [main.c#L137](https://github.com/bgranek/projektso/blob/main/main.c#L137) |
| `shmget()` dostęp | kasjer.c | 45 | [kasjer.c#L45](https://github.com/bgranek/projektso/blob/main/kasjer.c#L45) |
| `shmat()` kasjer | kasjer.c | 54 | [kasjer.c#L54](https://github.com/bgranek/projektso/blob/main/kasjer.c#L54) |
| `shmdt()` | kasjer.c | 30 | [kasjer.c#L30](https://github.com/bgranek/projektso/blob/main/kasjer.c#L30) |
| `shmctl()` usunięcie | main.c | 58 | [main.c#L58](https://github.com/bgranek/projektso/blob/main/main.c#L58) |
| Struktura StanHali | common.h | 209-251 | [common.h#L209-L251](https://github.com/bgranek/projektso/blob/main/common.h#L209-L251) |

### 20.8 Kolejki komunikatów (msgget, msgsnd, msgrcv, msgctl)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `msgget()` tworzenie | main.c | 222-230 | [main.c#L222-L230](https://github.com/bgranek/projektso/blob/main/main.c#L222-L230) |
| `msgget()` dostęp | kasjer.c | 48 | [kasjer.c#L48](https://github.com/bgranek/projektso/blob/main/kasjer.c#L48) |
| `msgsnd()` kibic | kibic.c | 404 | [kibic.c#L404](https://github.com/bgranek/projektso/blob/main/kibic.c#L404) |
| `msgrcv()` kibic | kibic.c | 412 | [kibic.c#L412](https://github.com/bgranek/projektso/blob/main/kibic.c#L412) |
| `msgrcv()` kasjer | kasjer.c | 190 | [kasjer.c#L190](https://github.com/bgranek/projektso/blob/main/kasjer.c#L190) |
| `msgsnd()` kasjer | kasjer.c | 290 | [kasjer.c#L290](https://github.com/bgranek/projektso/blob/main/kasjer.c#L290) |
| `msgctl()` usunięcie | main.c | 74 | [main.c#L74](https://github.com/bgranek/projektso/blob/main/main.c#L74) |
| Struktura KomunikatBilet | common.h | 257-265 | [common.h#L257-L265](https://github.com/bgranek/projektso/blob/main/common.h#L257-L265) |
| Struktura OdpowiedzBilet | common.h | 271-276 | [common.h#L271-L276](https://github.com/bgranek/projektso/blob/main/common.h#L271-L276) |

### 20.9 Gniazda sieciowe (socket, bind, listen, accept, connect)

| Funkcja | Plik | Linia | Link |
|---------|------|-------|------|
| `socket()` serwer | main.c | 457 | [main.c#L457](https://github.com/bgranek/projektso/blob/main/main.c#L457) |
| `bind()` | main.c | 481 | [main.c#L481](https://github.com/bgranek/projektso/blob/main/main.c#L481) |
| `listen()` | main.c | 490 | [main.c#L490](https://github.com/bgranek/projektso/blob/main/main.c#L490) |
| `accept()` | main.c | 503 | [main.c#L503](https://github.com/bgranek/projektso/blob/main/main.c#L503) |
| `socket()` klient | monitor.c | 45 | [monitor.c#L45](https://github.com/bgranek/projektso/blob/main/monitor.c#L45) |
| `connect()` | monitor.c | 66 | [monitor.c#L66](https://github.com/bgranek/projektso/blob/main/monitor.c#L66) |
| Cały serwer socket | main.c | 453-556 | [main.c#L453-L556](https://github.com/bgranek/projektso/blob/main/main.c#L453-L556) |

### 20.10 Obsługa błędów (perror, errno, walidacja)

| Element | Plik | Linia | Link |
|---------|------|-------|------|
| Makro SPRAWDZ | common.h | 141-147 | [common.h#L141-L147](https://github.com/bgranek/projektso/blob/main/common.h#L141-L147) |
| Makro WALIDUJ_ZAKRES | common.h | 150-157 | [common.h#L150-L157](https://github.com/bgranek/projektso/blob/main/common.h#L150-L157) |
| parsuj_int() | common.h | 290-312 | [common.h#L290-L312](https://github.com/bgranek/projektso/blob/main/common.h#L290-L312) |
| bezpieczny_scanf_int() | common.h | 315-353 | [common.h#L315-L353](https://github.com/bgranek/projektso/blob/main/common.h#L315-L353) |
| semop_retry_ctx() (EINTR) | common.h | 356-363 | [common.h#L356-L363](https://github.com/bgranek/projektso/blob/main/common.h#L356-L363) |

### 20.11 Własne moduły

| Moduł | Opis | Link |
|-------|------|------|
| main.c | Proces główny | [main.c](https://github.com/bgranek/projektso/blob/main/main.c) |
| kierownik.c | Program kierownika | [kierownik.c](https://github.com/bgranek/projektso/blob/main/kierownik.c) |
| kasjer.c | Proces kasjera | [kasjer.c](https://github.com/bgranek/projektso/blob/main/kasjer.c) |
| pracownik.c | Proces pracownika | [pracownik.c](https://github.com/bgranek/projektso/blob/main/pracownik.c) |
| kibic.c | Proces kibica | [kibic.c](https://github.com/bgranek/projektso/blob/main/kibic.c) |
| monitor.c | Monitor zewnętrzny | [monitor.c](https://github.com/bgranek/projektso/blob/main/monitor.c) |
| common.h | Wspólne definicje | [common.h](https://github.com/bgranek/projektso/blob/main/common.h) |
| rejestr.h | System logowania | [rejestr.h](https://github.com/bgranek/projektso/blob/main/rejestr.h) |

---

## 21. Podsumowanie

Projekt w pełni realizuje założenia symulacji hali widowiskowo-sportowej z wykorzystaniem wszystkich wymaganych mechanizmów IPC systemu UNIX/Linux. Implementacja obejmuje komunikację międzyprocesową przez pamięć współdzieloną, semafory, kolejki komunikatów, łącza FIFO oraz gniazda sieciowe. Program poprawnie obsługuje scenariusze wieloprocesowe i wielowątkowe, zapewniając synchronizację i unikanie zakleszczeń.
