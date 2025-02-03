HCC_YES=h5cc

# If user does not set HAVE_H5 we use a crude test to
# check if h5cc is in our PATH:
ifndef HAVE_H5
$(shell $(HCC_YES) -showconfig > /dev/null)
ifeq ($(.SHELLSTATUS),0)
# h5cc -showconfig successfully executed (note -help always has exit status 1)
HAVE_H5=YES
else
HAVE_H5=NO
endif
endif

HCC_NO=$(CC)

H5_DEFINES_YES=CONFIG_WITH_HDF5

CFLAGS=-O2 -Wall -g -fpic -fno-strict-aliasing $(addprefix -D,$(H5_DEFINES_$(HAVE_H5))) -I.

OBJS+=fwComm.o fwUtil.o cmdXfer.o at25Sup.o dac47cxSup.o
OBJS+=lmh6882Sup.o max195xxSup.o versaClkSup.o fegRegSup.o ad8370Sup.o
OBJS+=tca6408FECSup.o at24EepromSup.o unitData.o unitDataFlash.o
OBJS+=scopeSup.o

LOBJS=$(OBJS) $(H5_OBJS)

H5_OBJS=hdf5Sup.o

HCC=$(HCC_$(HAVE_H5))

PROGS=bbcli scopeCal

PYINC=$(lastword $(sort $(wildcard /usr/include/python3.*)))

PYFWCOMM_C=pyfwcomm.c

all: $(PROGS) pyfwcomm.so

libfwcomm.a: $(LOBJS)
	$(AR) r $@ $^

bbcli scopeCal unitDataTst:%:%.o libfwcomm.a
	$(HCC) $(CFLAGS) -o $@ $< -L. -lfwcomm -lm

pyfwcomm.o: $(PYFWCOMM_C)
	$(CC) $(CFLAGS) -c -o $@ $^ -I $(PYINC)

$(H5_OBJS): %.o: %.c %.h
	$(HCC) $(CFLAGS) -c -o $@ $<

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
	$(RM) $(LOBJS) $(PYFWCOMM_C) pyfwcomm.so pyfwcomm.o libfwcomm.a
	$(RM) $(PROGS) $(PROGS:%=%.o)
	$(RM) -rf __pycache__
	$(RM) unitDataTst

pyfwcomm.so: pyfwcomm.o libfwcomm.a
	$(CC) $< -shared -o $@ -L. -lfwcomm

CMakeLists.txt: makefile
	@$(RM) $@
	@echo '# AUTOMATICALLY GENERATED (makefile); do not modify!' >> $@
	@echo 'cmake_minimum_required(VERSION 3.5)'                  >> $@
	@echo ''                                                     >> $@
	@echo 'project( fwcomm LANGUAGES C )'                        >> $@
	@echo ''                                                     >> $@
	@echo 'set( SOURCES $(LOBJS:%.o=%.c) )'                      >> $@
	@echo 'set( LIBS    fwcomm          )'                       >> $@
	@echo ''                                                     >> $@
	@echo 'include_directories( ./ )'                            >> $@
	@echo ''                                                     >> $@
	@echo 'find_package(HDF5 COMPONENTS C)'                      >> $@
	@echo 'if( HDF5_FOUND )'                                     >> $@
	@echo '  include_directories( $${HDF5_INCLUDE_DIR} )'        >> $@
	@echo '  add_compile_definitions( CONFIG_WITH_HDF5 )'        >> $@
	@echo '  list( APPEND LIBS    $${HDF5_LIBRARIES} )'          >> $@
	@echo 'endif()'                                              >> $@
	@echo ''                                                     >> $@
	@echo 'list( APPEND LIBS m )'                                >> $@
	@echo ''                                                     >> $@
	@echo 'add_library( fwcomm $${SOURCES} )'                    >> $@
	@echo ''                                                     >> $@
	@for p in $(PROGS) ; do \
		echo "add_executable( $$p $$p.c )"                       >> $@ ;\
		echo "target_link_libraries( $$p PRIVATE \$${LIBS} )"    >> $@ ;\
	done
