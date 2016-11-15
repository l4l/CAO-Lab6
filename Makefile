CC?=clang

.PHONY: virtmem

virtmem: main.o page_table.o disk.o program.o
	$(CC) main.o page_table.o disk.o program.o -o virtmem

main.o: main.c
	$(CC) $(CFLAGS) -Wall -g -c main.c -o main.o

page_table.o: page_table.c
	$(CC) $(CFLAGS) -Wall -g -c page_table.c -o page_table.o

disk.o: disk.c
	$(CC) $(CFLAGS) -Wall -g -c disk.c -o disk.o

program.o: program.c
	$(CC) $(CFLAGS) -Wall -g -c program.c -o program.o


clean:
	rm -f *.o virtmem
