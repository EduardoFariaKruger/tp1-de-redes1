CC = gcc
CFLAGS = -Wall -Wextra -g

# Objetos comuns
OBJS_COMMON = operations.o

# Alvos
all: client server

client: client.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o client client.o $(OBJS_COMMON)

server: server.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) -o server server.o $(OBJS_COMMON)

# Compilação dos arquivos fonte em .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o client server
