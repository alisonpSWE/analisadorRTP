# Makefile - analisadorRTP
# Build no MinGW.org (Windows): use "mingw32-make", nao "make".

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99
LDLIBS  = -lws2_32
ALVO    = analisador.exe

FONTES  = main.c rtp.c stats.c
OBJETOS = $(FONTES:.c=.o)

.PHONY: all clean

all: $(ALVO)

$(ALVO): $(OBJETOS)
	$(CC) $(CFLAGS) -o $(ALVO) $(OBJETOS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-del /Q $(ALVO) $(OBJETOS) 2>NUL
