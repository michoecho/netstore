CFLAGS += -Wall -Wextra

all: netstore-server netstore-client

klient.o serwer.o common.o: common.h

netstore-server: serwer.o common.o
	$(CC) $(CFLAGS) -o $@ $^
netstore-client: klient.o common.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f netstore-client netstore-server serwer.o klient.o common.o

.PHONY: clean all
