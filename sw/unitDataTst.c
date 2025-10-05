#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "unitData.h"

#define NUM_CH 2

uint8_t            *buf = 0;
size_t              bufSz;

int
main(int argc, char **argv)
{
int                 i;
const UnitData     *udTst = 0;
const ScopeCalData *calDatap;
ScopeCalData        calData;
ScopeCalData        cmp;

	UnitData *ud = unitDataCreate( NUM_CH );
	assert( ud != 0 );
	assert( unitDataGetNumChannels( ud ) == NUM_CH );
	assert( !!(calDatap = unitDataGetCalData( ud, 0 )) );
	assert( isnan( calDatap->fullScaleVolt ) );
	for ( i = 0; i < NUM_CH; i++ ) {
		assert( 0 == unitDataSetFullScaleVolt( ud, i, 10.0*(i+1) ) );
		assert( 0 == unitDataSetOffsetVolt( ud, i,-1.0*(i+1) ) );
	}
	assert( -EINVAL == unitDataSetFullScaleVolt( ud, NUM_CH, 1.0 ) );
	assert( -EINVAL == unitDataSetOffsetVolt( ud, NUM_CH, 100.0 ) );
	bufSz = unitDataGetSerializedSize( NUM_CH );
	assert( ( buf = calloc( bufSz, 1 ) ) );
	assert( bufSz == unitDataSerialize( ud, buf, bufSz ) );


	assert( 0 == unitDataParse( &udTst, buf, bufSz ) );
	assert( unitDataGetNumChannels( udTst ) == NUM_CH );
	for ( i = 0; i < NUM_CH; i++ ) {
		assert( unitDataGetFullScaleVolt( udTst, i )  == 10.0*(i+1) );
		assert( unitDataGetOffsetVolt( udTst, i ) == -1.0*(i+1) );
	}
	
	scope_cal_data_init( &calData );
	calData.fullScaleVolt = 3.14159265;
	calData.offsetVolt    = 0.707;
	assert( 0 == unitDataSetCalData( ud, 1, &calData ) );

	memset( buf, 0, bufSz );
	assert( bufSz == unitDataSerialize( ud, buf, bufSz ) );
	assert( 0 == unitDataParse( &udTst, buf, bufSz ) );

	assert( 0 == unitDataCopyCalData( udTst, 1, &cmp ) );

	assert( 0 == memcmp( &cmp, &calData, sizeof(cmp) ) );

	unitDataFree( ud );
	unitDataFree( udTst );
	free( buf );
	printf("%s Test PASSED\n", argv[0]);
	return 0;
}
