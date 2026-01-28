# Dokumentacja Projektu - Temat 18: Hala Widowiskowo-Sportowa

## 1. Struktura projektu

### 1.1 Pliki zrodlowe

| Plik | Opis | Linie kodu |
|------|------|------------|
| `main.c` | Glowny program symulacji | ~424 |
| `kierownik.c` | Panel kierownika hali | ~317 |
| `kasjer.c` | Obsluga kas biletowych | ~257 |
| `pracownik.c` | Pracownik techniczny sektora | ~347 |
| `kibic.c` | Proces kibica | ~645 |
| `monitor.c` | Monitor stanu hali (klient socket) | ~125 |
| `common.h` | Wspolne definicje i struktury | ~248 |
| `rejestr.h` | Modul logowania do pliku | ~142 |

### 1.2 Architektura procesow

```
main (glowny proces)
├── kasjer (10 procesow) - fork() + execl()
├── pracownik (8 procesow) - fork() + execl()
│   └── watki stanowisk (2 watki na proces) - pthread_create()
├── kibic (dynamicznie tworzeni) - fork() + execl()
│   └── kibic-dziecko (fork z rodzica)
├── watek_serwera_socket - pthread_create()
└── kierownik (osobny terminal) - uruchamiany recznie
```

---

## 2. Mapowanie wymagan na implementacje

### 2.1 Wymagania z tematu projektu

| Wymaganie | Plik | Funkcja/Linia | Opis implementacji |
|-----------|------|---------------|-------------------|
| Pojemnosc K kibicow, 8 sektorow | `common.h` | L41-43 | `LICZBA_SEKTOROW=8`, `SEKTOR_VIP=8` |
| Pojemnosc sektora K/8 | `main.c` | L97 | `pojemnosc_sektora = pojemnosc_K / LICZBA_SEKTOROW` |
| Sektor VIP | `common.h` | L42 | `SEKTOR_VIP=8` jako 9. sektor |
| 10 kas biletowych | `common.h` | L45 | `LICZBA_KAS=10` |
| Min 2 kasy aktywne | `kasjer.c` | L65-67 | `if (id_kasjera < 2) kasa_aktywna = 1` |
| Dynamiczne otwieranie/zamykanie kas | `kasjer.c` | L59-97 | `aktualizuj_status()` |
| Max 2 bilety na kibica | `common.h` | L47 | `MAX_BILETOW_NA_KIBICA=2` |
| Losowy przydzial sektorow | `kasjer.c` | L133 | `start_sektor = rand() % LICZBA_SEKTOROW` |
| Zamkniecie kas po wyprzedaniu | `kasjer.c` | L159-166 | `sprawdz_czy_wyprzedane()` |
| VIP omija kolejke (<0.3% K) | `main.c` | L98 | `limit_vip = (pojemnosc_K * 3) / 1000` |
| VIP omija kolejke | `kibic.c` | L232-238 | `if (!jestem_vip) aktualizuj_kolejke(1)` |
| Kibice przychodza losowo (nawet po starcie meczu) | `main.c` | L412-420 | Petla generowania kibicow bez blokady fazy |
| Osobne wejscie do kazdego sektora | `pracownik.c` | L181-243 | Kazdy pracownik obsluguje jeden sektor |
| 2 stanowiska kontroli na sektor | `pracownik.c` | L293-308 | `uruchom_watki_stanowisk()` - 2 watki |
| Max 3 osoby na stanowisku | `common.h` | L105 | `MiejscaKolejki miejsca[3]` |
| Ta sama druzyna na stanowisku | `pracownik.c` | L221 | `obecna_druzyna == miejsca[i].druzyna` |
| Max 5 przepuszczen (frustracja) | `kibic.c` | L365, L412 | `MAX_PRZEPUSZCZEN=5`, `przepuszczeni++` |
| VIP osobne wejscie bez kontroli | `kibic.c` | L307-351 | Blok `if (numer_sektora == SEKTOR_VIP)` |
| Dzieci <15 z opiekunem | `pracownik.c` | L208-218 | `sprawdz_rodzine_dziecka()` |
| Sygnal1: blokada sektora | `kierownik.c` | L152-183 | `wyslij_blokade()` -> `SYGNAL_BLOKADA_SEKTORA` |
| Sygnal2: odblokowanie sektora | `kierownik.c` | L185-216 | `wyslij_odblokowanie()` -> `SYGNAL_ODBLOKOWANIE_SEKTORA` |
| Sygnal3: ewakuacja | `kierownik.c` | L218-243 | `zarzadzaj_ewakuacja()` -> `SYGNAL_EWAKUACJA` |
| Pracownik informuje kierownika o ewakuacji | `pracownik.c` | L101-127 | `wyslij_zgloszenie_do_kierownika()` przez FIFO |
| Raport w pliku tekstowym | `rejestr.h` | caly plik | `symulacja.log` |

---

## 3. Funkcje systemowe - lokalizacja w kodzie

### 3.1 Tworzenie i obsluga plikow

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `creat()` | `rejestr.h` | L26 | Tworzenie pliku logu |
| `open()` | `rejestr.h` | L32 | Otwieranie pliku logu (O_WRONLY\|O_APPEND\|O_CREAT) |
| `open()` | `pracownik.c` | L102 | Otwieranie FIFO do zapisu |
| `open()` | `kierownik.c` | L36 | Otwieranie FIFO do odczytu |
| `close()` | `rejestr.h` | L28, L70 | Zamykanie pliku logu |
| `close()` | `pracownik.c` | L126 | Zamykanie FIFO |
| `close()` | `kierownik.c` | L9 | Zamykanie FIFO |
| `read()` | `kierownik.c` | L52 | Odczyt z FIFO |
| `write()` | `rejestr.h` | L50, L69, L98, L117, L137 | Zapis do logu |
| `write()` | `pracownik.c` | L42, L52, L59, L117 | Zapis do FIFO i stdout |
| `unlink()` | `main.c` | L15, L65 | Usuwanie FIFO |

### 3.2 Tworzenie procesow

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `fork()` | `main.c` | L228, L245, L412 | Tworzenie kasjerow, pracownikow, kibicow |
| `fork()` | `kibic.c` | L587 | Rodzic tworzy proces dziecka |
| `execl()` | `main.c` | L232, L249, L414 | Uruchamianie kasjer, pracownik, kibic |
| `execl()` | `kibic.c` | L594 | Uruchamianie procesu dziecka |
| `exit()` | wszedzie | - | Zakonczenie procesu |
| `wait()` | `main.c` | L58, L393 | Czekanie na procesy potomne |
| `waitpid()` | `kibic.c` | L615 | Rodzic czeka na dziecko |

### 3.3 Tworzenie i obsluga watkow

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `pthread_create()` | `main.c` | L321 | Watek serwera socket |
| `pthread_create()` | `pracownik.c` | L303 | Watki stanowisk kontroli |
| `pthread_join()` | `pracownik.c` | L25 | Czekanie na watki przy wyjsciu |
| `pthread_detach()` | `main.c` | L324 | Odlaczenie watku socket |
| `pthread_mutex_lock()` | `pracownik.c` | L253 | Blokada mutexu sektora |
| `pthread_mutex_unlock()` | `pracownik.c` | L259 | Odblokowanie mutexu |
| `pthread_mutex_lock()` | `rejestr.h` | L78, L106, L125 | Synchronizacja zapisu do logu |
| `pthread_mutex_unlock()` | `rejestr.h` | L100, L119, L139 | Odblokowanie mutexu logu |
| `pthread_cond_wait()` | `pracownik.c` | L256 | Czekanie na odblokowanie sektora |
| `pthread_cond_broadcast()` | `pracownik.c` | L22, L50, L60 | Budzenie watkow |
| `pthread_mutex_destroy()` | `pracownik.c` | L28 | Niszczenie mutexu |
| `pthread_cond_destroy()` | `pracownik.c` | L29 | Niszczenie zmiennej warunkowej |

### 3.4 Obsluga sygnalow

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `signal()` | `main.c` | L56, L312-315, L391 | SIGINT, SIGTERM, SIGCHLD, SIGPIPE |
| `signal()` | `kierownik.c` | L257 | Ignorowanie SYGNAL_EWAKUACJA |
| `sigaction()` | `pracownik.c` | L92, L95, L98 | Handlery blokady, odblokowania, ewakuacji |
| `sigaction()` | `kibic.c` | L157 | Handler ewakuacji |
| `sigaction()` | `kierownik.c` | L255 | Handler SIGINT |
| `kill()` | `main.c` | L57, L392 | Wysylanie SIGTERM do grupy |
| `kill()` | `kierownik.c` | L172, L205, L238 | Wysylanie sygnalow do pracownikow |

**Sygnaly uzywane:**
- `SIGRTMIN+1` - blokada sektora (`common.h:62`)
- `SIGRTMIN+2` - odblokowanie sektora (`common.h:63`)
- `SIGRTMIN+3` - ewakuacja (`common.h:64`)
- `SIGINT` - przerwanie programu
- `SIGTERM` - zakonczenie procesow

### 3.5 Synchronizacja procesow (semafory)

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `ftok()` | `main.c` | L78-83 | Generowanie kluczy IPC |
| `ftok()` | `kasjer.c` | L19-24 | Generowanie kluczy IPC |
| `ftok()` | `pracownik.c` | L64-67 | Generowanie kluczy IPC |
| `ftok()` | `kibic.c` | L131-136 | Generowanie kluczy IPC |
| `semget()` | `main.c` | L129 | Tworzenie semaforow |
| `semget()` | `kasjer.c` | L32 | Dolaczanie do semaforow |
| `semget()` | `pracownik.c` | L72 | Dolaczanie do semaforow |
| `semget()` | `kibic.c` | L144 | Dolaczanie do semaforow |
| `semctl()` | `main.c` | L132, L136 | Inicjalizacja wartosci semaforow |
| `semctl()` | `main.c` | L36 | Usuwanie semaforow |
| `semop()` | `kasjer.c` | L117, L169 | Operacje P/V |
| `semop()` | `pracownik.c` | L139, L144, L273, L281 | Operacje P/V |
| `semop()` | `kibic.c` | wielokrotnie | Operacje P/V |

### 3.6 Lacza nazwane (FIFO)

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `mkfifo()` | `main.c` | L67 | Tworzenie FIFO dla komunikacji pracownik->kierownik |
| `open()` | `pracownik.c` | L102 | Otwieranie FIFO do zapisu (O_WRONLY\|O_NONBLOCK) |
| `open()` | `kierownik.c` | L36 | Otwieranie FIFO do odczytu (O_RDONLY\|O_NONBLOCK) |
| `write()` | `pracownik.c` | L117 | Wysylanie komunikatu przez FIFO |
| `read()` | `kierownik.c` | L52 | Odbieranie komunikatu z FIFO |

**Sciezka FIFO:** `/tmp/hala_fifo_ewakuacja` (`common.h:49`)

### 3.7 Segmenty pamieci dzielonej

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `shmget()` | `main.c` | L85 | Tworzenie segmentu (IPC_CREAT\|0600) |
| `shmget()` | `kasjer.c` | L26 | Dolaczanie do segmentu |
| `shmget()` | `pracownik.c` | L69 | Dolaczanie do segmentu |
| `shmget()` | `kibic.c` | L138 | Dolaczanie do segmentu |
| `shmget()` | `kierownik.c` | L27 | Dolaczanie do segmentu |
| `shmat()` | `main.c` | L88 | Mapowanie segmentu |
| `shmat()` | wszystkie | - | Mapowanie segmentu |
| `shmdt()` | `main.c` | L28 | Odmapowanie segmentu |
| `shmdt()` | wszystkie | - | Odmapowanie przy wyjsciu |
| `shmctl()` | `main.c` | L31 | Usuwanie segmentu (IPC_RMID) |

**Struktura pamieci dzielonej:** `StanHali` (`common.h:128-159`)

### 3.8 Kolejki komunikatow

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `msgget()` | `main.c` | L141 | Tworzenie kolejki (IPC_CREAT\|0600) |
| `msgget()` | `kasjer.c` | L29 | Dolaczanie do kolejki |
| `msgget()` | `kibic.c` | L141 | Dolaczanie do kolejki |
| `msgsnd()` | `kibic.c` | L249 | Wysylanie zapytania o bilet |
| `msgsnd()` | `kasjer.c` | L178 | Wysylanie odpowiedzi |
| `msgrcv()` | `kasjer.c` | L105 | Odbieranie zapytania (IPC_NOWAIT) |
| `msgrcv()` | `kibic.c` | L255 | Odbieranie odpowiedzi |
| `msgctl()` | `main.c` | L41 | Usuwanie kolejki (IPC_RMID) |

**Struktury komunikatow:** `KomunikatBilet`, `OdpowiedzBilet` (`common.h:161-175`)

### 3.9 Gniazda (socket)

| Funkcja | Plik | Linia | Kontekst |
|---------|------|-------|----------|
| `socket()` | `main.c` | L152 | Tworzenie gniazda serwera |
| `socket()` | `monitor.c` | L34 | Tworzenie gniazda klienta |
| `bind()` | `main.c` | L167 | Bindowanie do portu |
| `listen()` | `main.c` | L173 | Nasluchiwanie polaczen |
| `accept()` | `main.c` | L183 | Akceptowanie polaczen |
| `connect()` | `monitor.c` | L52 | Laczenie z serwerem |
| `send()` | `main.c` | L218 | Wysylanie danych |
| `recv()` | `monitor.c` | L62 | Odbieranie danych |
| `setsockopt()` | `main.c` | L159 | Ustawienia gniazda |
| `inet_pton()` | `monitor.c` | L46 | Konwersja adresu IP |

**Port:** 9999 (`common.h:51`)

---

## 4. Obsluga bledow i walidacja

### 4.1 Makro SPRAWDZ

```c
// common.h:79-85
#define SPRAWDZ(x) \
    do { \
        if ((x) == -1) { \
            perror(#x); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)
```

**Uzycie:** `main.c`, `kasjer.c`, `pracownik.c`, `kibic.c`, `kierownik.c`

### 4.2 Walidacja danych wejsciowych

| Funkcja | Plik | Linia | Opis |
|---------|------|-------|------|
| `parsuj_int()` | `common.h` | L184-206 | Walidacja argumentow z linii polecen |
| `bezpieczny_scanf_int()` | `common.h` | L208-246 | Bezpieczne wczytywanie int z konsoli |
| Walidacja K | `main.c` | L282-285 | Sprawdzenie podzielnosci przez 8 |

### 4.3 Uzycie perror()

- `main.c`: L69, L90, L154, L169, L176, L233, L236, L250, L253, L322, L415, L418
- `kasjer.c`: L37, L109, L180
- `pracownik.c`: L77, L105, L119, L298, L304
- `kibic.c`: L149, L596
- `kierownik.c`: L32, L38, L177, L210, L239, L248
- `rejestr.h`: L34

---

## 5. Wyrozniajace elementy (10%)

### 5.1 Kolorowe wyjscie terminala
- Definicje kolorow: `common.h:23-30`
- Uzycie w printf we wszystkich plikach

### 5.2 Monitor w czasie rzeczywistym
- Serwer socket w `main.c` (watek)
- Klient w `monitor.c`
- Wyswietlanie stanu hali z odswiezaniem

### 5.3 System logowania
- Modul `rejestr.h` z synchronizacja mutex
- Zapis do pliku `symulacja.log`
- Znaczniki czasowe i kategorie

### 5.4 Interfejs kierownika
- Menu tekstowe w `kierownik.c`
- Interaktywne zarzadzanie sektorami

---

## 6. Podzial na moduly (5%)

| Modul | Pliki | Opis |
|-------|-------|------|
| Glowny | `main.c` | Inicjalizacja, zarzadzanie procesami |
| Wspolny | `common.h` | Struktury, stale, funkcje pomocnicze |
| Logowanie | `rejestr.h` | System zapisu do pliku |
| Kasjer | `kasjer.c` | Obsluga sprzedazy biletow |
| Pracownik | `pracownik.c` | Kontrola przy wejsciu |
| Kibic | `kibic.c` | Logika zachowania kibica |
| Kierownik | `kierownik.c` | Panel zarzadzania |
| Monitor | `monitor.c` | Podglad stanu hali |

---

## 7. Parametry uruchomienia

### 7.1 main
```
./main [-k pojemnosc] [-t czas_do_meczu] [-d czas_meczu] [-h]
  -k  Pojemnosc hali K (80-100000, podzielne przez 8)
  -t  Czas do rozpoczecia meczu w sekundach
  -d  Czas trwania meczu w sekundach
  -h  Wyswietl pomoc
```

### 7.2 monitor
```
./monitor [-i interwal] [-s serwer] [-h]
  -i  Interwal odswiezania w sekundach
  -s  Adres serwera
  -h  Wyswietl pomoc
```

### 7.3 kierownik
```
./kierownik
(uruchamiany recznie w osobnym terminalu)
```

---

## 8. Prawa dostepu IPC

| Zasob | Prawa | Plik | Linia |
|-------|-------|------|-------|
| Pamiec dzielona | 0600 | `main.c` | L85 |
| Semafory | 0600 | `main.c` | L129 |
| Kolejka komunikatow | 0600 | `main.c` | L141 |
| FIFO | 0666 | `main.c` | L67 |
| Plik logu | 0644 | `rejestr.h` | L26, L32 |

---

## 9. Sprzatanie zasobow

Funkcja `sprzataj_zasoby()` w `main.c:18-50`:
- `shmdt()` - odmapowanie pamieci
- `shmctl(IPC_RMID)` - usuniecie pamieci dzielonej
- `semctl(IPC_RMID)` - usuniecie semaforow
- `msgctl(IPC_RMID)` - usuniecie kolejki komunikatow
- `unlink()` - usuniecie FIFO
- `rejestr_zamknij()` - zamkniecie pliku logu
