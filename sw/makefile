all: bbcli

CFLAGS=-O2 -Wall

LOBJS=fwComm.o fwUtil.o cmdXfer.o

bbcli: bbcli.o $(LOBJS)
	$(CC) $(CFLAGS) -o $@ $^


%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

bbcli.o: fwComm.h fwUtil.h
fwComm.o: fwComm.h cmdXfer.h
cmdXfer.o: cmdXfer.h

.PHONY: clean

clean:
	$(RM) bbcli.o bbcli $(LOBJS)
