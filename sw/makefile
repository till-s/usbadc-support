#LB-MIT
#
# MIT License
#
# Copyright (c) 2026 Till Straumann
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
#LE-MIT

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

ifndef HAVE_JANSSON
$(shell pkg-config --exists jansson)
ifeq ($(.SHELLSTATUS),0)
HAVE_JANSSON=YES
JANSSON_LIBS=$(shell pkg-config --libs jansson)
else
HAVE_JANSSON=NO
endif
endif

HCC_NO=$(CC)

H5_DEFINES_YES=CONFIG_WITH_HDF5
JANSSON_DEFINES_YES=CONFIG_WITH_JANSSON

CFLAGS=-O2 -Wall -g -fpic -fno-strict-aliasing -I.
CFLAGS+=$(addprefix -D,$(H5_DEFINES_$(HAVE_H5)))
CFLAGS+=$(addprefix -D,$(JANSSON_DEFINES_$(HAVE_JANSSON)))

OBJS+=fwComm.o fwUtil.o cmdXfer.o at25Sup.o dac47cxSup.o
OBJS+=lmh6882Sup.o max195xxSup.o versaClkSup.o fegRegSup.o ad8370Sup.o
OBJS+=tca6408FECSup.o at24EepromSup.o unitData.o unitDataFlash.o
OBJS+=scopeSup.o jsonSup.o flash.o

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
	$(HCC) $(CFLAGS) -o $@ $< -L. -lfwcomm -lm $(JANSSON_LIBS)

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
flash.o: flash.h

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
	@echo 'cmake_minimum_required(VERSION 3.10)'                 >> $@
	@echo 'include( FindPkgConfig )'                             >> $@
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
	@echo '  add_compile_definitions( CONFIG_WITH_HDF5=1 )'      >> $@
	@echo '  list( APPEND LIBS    $${HDF5_LIBRARIES} )'          >> $@
	@echo 'endif()'                                              >> $@
	@echo ''                                                     >> $@
	@echo 'pkg_check_modules(JANSSON jansson)'                   >> $@
	@echo 'if ( JANSSON_FOUND )'                                 >> $@
	@echo '  include_directories( $${JANSSON_INCLUDE_DIR} )'     >> $@
	@echo '  add_compile_definitions( CONFIG_WITH_JANSSON=1 )'   >> $@
	@echo '  list( APPEND LIBS    $${JANSSON_LIBRARIES} )'       >> $@
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
	@echo ''                                                     >> $@
	@echo 'if ( JANSSON_FOUND )'                                 >> $@
	@echo '  add_executable( jsonTest jsonTest.c )'              >> $@
	@echo '  target_link_libraries( jsonTest PRIVATE $${LIBS} )' >> $@
	@echo 'endif()'                                              >> $@
