CC=gcc
CFLAGS=-w -std=c11
PROG=UnixLs
OBJS= main.o

UnixLs: $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm *.o $(PROG)