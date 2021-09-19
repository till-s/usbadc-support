all: bbcli

bbcli: bbcli.o fwComm.o cmdXfer.o
	$(CC) $(CFLAGS) -o $@ $^

bbcli.o: bbcli.c fwComm.h cmdXfer.h
	$(CC) $(CFLAGS) -c -o $@ $<

fwComm.o: fwComm.c fwComm.h cmdXfer.h
	$(CC) $(CFLAGS) -c -o $@ $<

cmdXfer.o: cmdXfer.c cmdXfer.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean

clean:
	$(RM) bbcli.o fwComm.o cmdXfer.o bbcli
