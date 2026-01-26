CC = gcc
CFLAGS = -Wall -Wextra -D_GNU_SOURCE
LDFLAGS = -lpthread

TARGETS = main kierownik kasjer pracownik kibic monitor
OBJS = main.o kierownik.o kasjer.o pracownik.o kibic.o monitor.o

all: $(TARGETS)

main: main.o
	$(CC) $(CFLAGS) -o $@ main.o $(LDFLAGS)

kierownik: kierownik.o
	$(CC) $(CFLAGS) -o $@ kierownik.o $(LDFLAGS)

kasjer: kasjer.o
	$(CC) $(CFLAGS) -o $@ kasjer.o $(LDFLAGS)

pracownik: pracownik.o
	$(CC) $(CFLAGS) -o $@ pracownik.o $(LDFLAGS)

kibic: kibic.o
	$(CC) $(CFLAGS) -o $@ kibic.o $(LDFLAGS)

monitor: monitor.o
	$(CC) $(CFLAGS) -o $@ monitor.o

monitor.o: monitor.c
	$(CC) $(CFLAGS) -c $<

%.o: %.c common.h rejestr.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(TARGETS) *.o