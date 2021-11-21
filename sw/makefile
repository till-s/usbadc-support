all: bbcli

CFLAGS=-O2 -Wall -g

LOBJS+=fwComm.o fwUtil.o cmdXfer.o at25Sup.o dac47cxSup.o
LOBJS+=lmh6882Sup.o

bbcli: bbcli.o $(LOBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm


%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

bbcli.o: fwComm.h fwUtil.h at25Sup.h lmh6882Sup.h
fwComm.o: fwComm.h cmdXfer.h
cmdXfer.o: cmdXfer.h
at25Sup.o: fwComm.h cmdXfer.h

.PHONY: clean

clean:
	$(RM) bbcli.o bbcli $(LOBJS)
