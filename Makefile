CC=gcc
CFLAGS=-c -O3 -march=native -ffast-math -fopenmp -Wall -Wextra
LDFLAGS=-lX11 -fopenmp

i3lock-fancy-rapid: i3lock-fancy-rapid.o
	$(CC) $^ $(LDFLAGS) -o $@

i3lock-fancy-rapid.o: i3lock-fancy-rapid.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f i3lock-fancy-rapid *.o
