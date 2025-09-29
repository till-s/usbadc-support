# AUTOMATICALLY GENERATED (makefile); do not modify!
cmake_minimum_required(VERSION 3.5)
include(FindPkgConfig)

project( fwcomm LANGUAGES C )

set( SOURCES fwComm.c fwUtil.c cmdXfer.c at25Sup.c dac47cxSup.c lmh6882Sup.c max195xxSup.c versaClkSup.c fegRegSup.c ad8370Sup.c tca6408FECSup.c at24EepromSup.c unitData.c unitDataFlash.c scopeSup.c hdf5Sup.c jsonSup.c )
set( LIBS    fwcomm          )

include_directories( ./ )

find_package(HDF5 COMPONENTS C)
if( HDF5_FOUND )
  include_directories( ${HDF5_INCLUDE_DIR} )
  add_compile_definitions( CONFIG_WITH_HDF5=1 )
  list( APPEND LIBS    ${HDF5_LIBRARIES} )
endif()

pkg_check_modules(JANSSON jansson)

if ( JANSSON_FOUND )
  include_directories( ${JANSSON_INCLUDE_DIR} )
  add_compile_definitions( CONFIG_WITH_JANSSON=1 )
  list( APPEND LIBS    ${JANSSON_LIBRARIES} )
endif()

list( APPEND LIBS m )

add_library( fwcomm ${SOURCES} )

add_executable( bbcli bbcli.c )
target_link_libraries( bbcli PRIVATE ${LIBS} )
add_executable( scopeCal scopeCal.c )
target_link_libraries( scopeCal PRIVATE ${LIBS} )
add_executable( jsonTest jsonTest.c )
target_link_libraries( jsonTest PRIVATE ${LIBS} )
