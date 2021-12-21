all: bbcli

CFLAGS=-O2 -Wall -g -fpic

LOBJS+=fwComm.o fwUtil.o cmdXfer.o at25Sup.o dac47cxSup.o
LOBJS+=lmh6882Sup.o

PYINC=/usr/include/python3.8

all: bbcli pyfwcomm.so

bbcli: bbcli.o $(LOBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

pyfwcomm.o: pyfwcomm.c
	$(CC) $(CFLAGS) -c -o $@ $^ -I $(PYINC)

pyfwcomm.c: pyfwcomm.pyx fwComm.h pyfwcomm.pxd
	cython3 $<


%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

bbcli.o: fwComm.h fwUtil.h at25Sup.h lmh6882Sup.h
fwComm.o: fwComm.h cmdXfer.h
cmdXfer.o: cmdXfer.h
at25Sup.o: fwComm.h cmdXfer.h

.PHONY: clean

clean:
	$(RM) bbcli.o bbcli $(LOBJS) pyfwcomm.c

pyfwcomm.so: pyfwcomm.o $(LOBJS)
	$(CC) $^ -shared -o $@
