CC=gcc
SCALE?=1
CFLAGS=-c -O3 -march=native -ffast-math -fopenmp -Wall -Wextra -DSCALE=$(SCALE)
LDFLAGS=-lX11 -lXext -fopenmp

i3lock-fancy-rapid: i3lock-fancy-rapid.o
	$(CC) $^ $(LDFLAGS) -o $@

clean:
	rm -f i3lock-fancy-rapid *.o
