all: bbcli

CFLAGS=-O2 -Wall -g -fpic

LOBJS+=fwComm.o fwUtil.o cmdXfer.o at25Sup.o dac47cxSup.o
LOBJS+=lmh6882Sup.o max195xxSup.o versaClkSup.o fegRegSup.o ad8370Sup.o

PYINC=$(lastword $(sort $(wildcard /usr/include/python3.*)))

PYFWCOMM_C=pyfwcomm.c

all: bbcli pyfwcomm.so

bbcli: bbcli.o $(LOBJS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

pyfwcomm.o: $(PYFWCOMM_C)
	$(CC) $(CFLAGS) -c -o $@ $^ -I $(PYINC)

$(PYFWCOMM_C): pyfwcomm.pyx fwComm.h pyfwcomm.pxd 
	cython3  $<


%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

bbcli.o $(PYFWCOMM_C): fwComm.h fwUtil.h at25Sup.h lmh6882Sup.h dac47cxSup.h max195xxSup.h versaClkSup.h fegRegSup.h ad8370Sup.h
fwComm.o: fwComm.h cmdXfer.h
cmdXfer.o: cmdXfer.h
at25Sup.o: fwComm.h cmdXfer.h
max195xxSup.o: fwComm.h max195xxSup.h
versaClkSup.o: fwComm.h versaClkSup.h

.PHONY: clean

clean:
	$(RM) bbcli.o bbcli $(LOBJS) $(PYFWCOMM_C) pyfwcomm.so pyfwcomm.o
	$(RM) -rf __pycache__

pyfwcomm.so: pyfwcomm.o $(LOBJS)
	$(CC) $^ -shared -o $@
