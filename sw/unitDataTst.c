#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "unitData.h"

#define NUM_CH 2

int
main(int argc, char **argv)
{
int i;
size_t bufSz;
uint8_t *buf = 0;
const UnitData *udTst = 0;
	UnitData *ud = unitDataCreate( NUM_CH );
	assert( ud != 0 );
	assert( unitDataGetNumChannels( ud ) == NUM_CH );
	for ( i = 0; i < NUM_CH; i++ ) {
		assert( 0 == unitDataSetScaleVolt( ud, i, 10.0*(i+1) ) );
		assert( 0 == unitDataSetOffsetVolt( ud, i,-1.0*(i+1) ) );
	}
	assert( -EINVAL == unitDataSetScaleVolt( ud, NUM_CH, 100.0 ) );
	assert( -EINVAL == unitDataSetOffsetVolt( ud, NUM_CH, 100.0 ) );
	bufSz = unitDataGetSerializedSize( NUM_CH );
	assert( ( buf = calloc( bufSz, 1 ) ) );
	assert( bufSz == unitDataSerialize( ud, buf, bufSz ) );

	assert( 0 == unitDataParse( &udTst, buf, bufSz ) );
	assert( unitDataGetNumChannels( udTst ) == NUM_CH );
	for ( i = 0; i < NUM_CH; i++ ) {
		assert( unitDataGetScaleVolt( ud, i )  == 10.0*(i+1) );
		assert( unitDataGetOffsetVolt( ud, i ) == -1.0*(i+1) );
	}
	
	unitDataFree( ud );
	unitDataFree( udTst );
	free( buf );
	printf("%s Test PASSED\n", argv[0]);
	return 0;
}
