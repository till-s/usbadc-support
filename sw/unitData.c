#include "unitData.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>

/* Serialized format; all numbers are stored as little-endian
 *
 *   <version>, <check>, <total size>, {<item>}, <term_tag>
 *
 *   item: <item_size>, <item_tag>, <item_data>
 *
 *   term_tag: 0xff
 *
 *   total size: 2 bytes (LE)
 *   item tag  : 1 byte
 *   item size : 1 byte (data size only)
 *   item data : depends on item
 *
 *   version byte: upper nibble: # channels
 *                 lower nibble: layout version
 *   check byte: complement of version byte
 */
#define O_VERSION 0
#define O_CHECK   1
#define O_TOTSIZE 2
#define O_PAYLOAD 4

#define LAYOUT_VERSION_1 0x1
#define NUM_CHANNELS( vers )   ( ((vers) >> 4 ) & 0xf )
#define LAYOUT_VERSION( vers ) ( ((vers) >> 0 ) & 0xf )
#define MAKE_VERSION( vers, numChannels ) ((uint8_t)((((numChannels)&0xf)<<4) | ((vers) & 0xf)))
#define MAKE_CHECK( version ) ( (uint8_t) ~ (version) )

#define IO_TAG 0
#define IO_SIZ 1
#define IO_DAT 2

#define TAG_SCALE_VOLTS 0x01
#define TAG_OFFSET_VOLTS 0x02
#define TAG_SCALE_RELAT 0x03
#define TAG_TERM 0xff

struct UnitData {
	unsigned version;
	unsigned numChannels;
	float   *scaleVolt;
	float   *scaleRelat;
	float   *offsetVolt;
};

unsigned
unitDataGetVersion(const UnitData *ud)
{
	return ud->version;
}

unsigned
unitDataGetNumChannels(const UnitData *ud)
{
	return ud->numChannels;
}

static void
check(const char *nm, const UnitData *ud, unsigned ch)
{
	if ( ud->numChannels <= ch ) {
		fprintf(stderr, "%s: invalid channel number %u (>= %u\n", nm, ch, ud->numChannels);
		abort();
	}
}

double
unitDataGetScaleVolt(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->scaleVolt[ch];
}

int
unitDataSetScaleVolt(UnitData *ud, unsigned ch, double value)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->scaleVolt[ch] = (float)value;
	return 0;
}

double
unitDataGetScaleRelat(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->scaleRelat[ch];
}


double
unitDataGetOffsetVolt(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->offsetVolt[ch];
}

static int
illFormed(const char *nm, const char *reason)
{
	fprintf(stderr, "Error: (%s) %s\n",	nm, reason);
	return -EINVAL;
}

static int
scanFloat(float *dstp, const char *funcName, const char *itemName, const uint8_t *srcp, int check)
{
int j;
union {
	uint32_t u;
	float    f;
} tmp;
	if ( check && ! isnan(*dstp) ) {
		fprintf(stderr, "Error: (%s) - %s item found multiple times\n", funcName, itemName);
		return -EINVAL;
	}
	tmp.u = 0;
	for ( j = sizeof(float) - 1; j >= 0; --j ) {
		tmp.u = (tmp.u << 8) | srcp[j];
	}
	*dstp = tmp.f;
	return 0;
}

static int
scanFloats(float **dstp, unsigned numChannels, const char *funcName, const char *itemName, const uint8_t *item)
{
int            i,st;
const uint8_t *srcp;

	if ( *dstp ) {
		fprintf(stderr, "Error: (%s) - %s item found multiple times\n", funcName, itemName);
		return -EINVAL;
	}
	if ( sizeof(float) * numChannels != item[IO_SIZ] ) {
		fprintf(stderr, "Error: (%s) - ill-formed %s item (unexpected size)\n", funcName, itemName);
		return -EINVAL;
	}
	if ( ! ( *dstp = malloc( sizeof(**dstp) * numChannels ) ) ) {
		fprintf(stderr, "Error: (%s) - no memory for item %s\n", funcName, itemName);
		return -EINVAL;
	}

	srcp = item + IO_DAT;
	for ( i = 0; i < numChannels; ++i ) {
		if ( (st = scanFloat( (*dstp) + i, funcName, itemName, srcp, 0 )) ) {
			return st;
		} 
		srcp += sizeof(float);
	}
	return 0;
}

/* Parse serialized unitData into an (abstract) object;
 * RETURNS:
 *   - 0 on success, UnitData in *result
 *   - nonzero error status and NULL in *result on error
 */
int
unitDataParse(const UnitData **result, const uint8_t *buf, size_t bufSize)
{
size_t    totSize;
int       st;
UnitData *ud = NULL;
unsigned  off, itemsize;
unsigned  layoutVersion;

	*result = NULL;

	if ( bufSize <= O_PAYLOAD ) {
		if ( (st = illFormed( __PRETTY_FUNCTION__, "incomplete data" )) ) {
			goto bail;
		}
	}
	/* empty flash/eeprom ? */
	if ( TAG_TERM == buf[O_VERSION] ) {
		st = -ENODATA;
		goto bail;
	}
	if ( MAKE_CHECK( buf[O_VERSION] ) != buf[O_CHECK] ) {
		if ( (st = illFormed( __PRETTY_FUNCTION__, "check byte does not match version" )) ) {
			goto bail;
		}
	}
	layoutVersion = LAYOUT_VERSION( buf[O_VERSION] );
	if ( LAYOUT_VERSION_1 != layoutVersion ) {
		fprintf(stderr, "Error: (%s) - unsupported \n", __PRETTY_FUNCTION__);
		st = -ENOTSUP;
		goto bail;
	}

	totSize = (buf[O_TOTSIZE + 1] << 8) | buf[O_TOTSIZE];
	if ( totSize > bufSize ) {
		if ( (st = illFormed( __PRETTY_FUNCTION__, "buffer size smaller than expected data size" ) ) ) {
			goto bail;
		}
	}
	ud = calloc( sizeof(*ud), 1 );
	if ( ! ud ) {
		fprintf(stderr, "Error: (%s) - no memory\n", __PRETTY_FUNCTION__);
		st = -ENOMEM;
		goto bail;
	}
	ud->numChannels = NUM_CHANNELS( buf[O_VERSION] );
	ud->version     = layoutVersion;
	for ( off = O_PAYLOAD; buf[off + IO_TAG] != TAG_TERM; off += itemsize ) {
		itemsize = IO_SIZ;
		if ( off + itemsize < totSize ) {
			itemsize = IO_DAT + buf[off + IO_SIZ];
		}
		// must be able to read the next tag
		if ( off + itemsize >= totSize ) {
			fprintf(stderr, "Error: (%s) - ill-formed item (too big)\n", __PRETTY_FUNCTION__);
			st = -EINVAL;
			goto bail;
		}
		switch ( buf[off + IO_TAG] ) {
			case TAG_SCALE_VOLTS:
				if ( (st = scanFloats( &ud->scaleVolt, ud->numChannels, __PRETTY_FUNCTION__, "ScaleVolt", buf + off)) < 0 ) {
					goto bail;
				}
				break;
			case TAG_OFFSET_VOLTS:
				if ( (st = scanFloats( &ud->offsetVolt, ud->numChannels, __PRETTY_FUNCTION__, "OffsetVolt", buf + off)) ) {
					goto bail;
				}
				break;
			case TAG_SCALE_RELAT:
				if ( (st = scanFloats( &ud->scaleRelat, ud->numChannels, __PRETTY_FUNCTION__, "ScaleRelat", buf + off)) ) {
					goto bail;
				}
				break;
			default:
				fprintf(stderr, "Warning: (%s) - unsupported tag 0x%02" PRIx8 "\n", __PRETTY_FUNCTION__, buf[off + IO_TAG]);
				break;
		}
	}

	if ( ! ud->scaleRelat ) {
		fprintf(stderr, "Error: (%s) - incomplete data; ScaleRelat not found\n", __PRETTY_FUNCTION__);
		st = -ENODATA;
		goto bail;
	}

	if ( ! ud->offsetVolt ) {
		fprintf(stderr, "Error: (%s) - incomplete data; OffsetVolt not found\n", __PRETTY_FUNCTION__);
		st = -ENODATA;
		goto bail;
	}

	if ( ! ud->scaleVolt ) {
		fprintf(stderr, "Error: (%s) - incomplete data; ScaleVolt not found\n", __PRETTY_FUNCTION__);
		st = -ENODATA;
		goto bail;
	}


	*result = ud;
	ud      = NULL;
	st      = 0;

bail:
	unitDataFree( ud );
	return st;
}

/*
 * Free object created by unitDataParse()
 */
void
unitDataFree(const UnitData *ud)
{
	if ( ud ) {
		free( ud->scaleRelat );
		free( ud->offsetVolt );
		free( (UnitData*)ud );
	}
}

UnitData *
unitDataCreate(unsigned numChannels)
{
UnitData *ud;
int       i;
	if ( numChannels > 15 ) {
		fprintf(stderr, "Error: (%s) num channels too big\n", __PRETTY_FUNCTION__);
		return NULL;
	}
	if ( ! (ud = calloc( sizeof(*ud), 1 )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		return NULL;
	}
	if ( ! (ud->scaleRelat = malloc( sizeof(*ud->scaleRelat) * numChannels )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		free( ud );
		return NULL;
	}
	if ( ! (ud->offsetVolt = malloc( sizeof(*ud->offsetVolt) * numChannels )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		free( ud->scaleRelat );
		free( ud );
		return NULL;
	}
	if ( ! (ud->scaleVolt = malloc( sizeof(*ud->scaleVolt) * numChannels )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		free( ud->offsetVolt);
		free( ud->scaleRelat );
		free( ud );
		return NULL;
	}
	ud->version     = LAYOUT_VERSION_1;
	ud->numChannels = numChannels;

	for ( i = 0; i < numChannels; ++i ) {
		ud->scaleRelat[i] = 1.0;
		ud->offsetVolt[i] = 0.0;
		ud->scaleVolt[i]  = 0.0/0.0;
	}
	return ud;
}

int
unitDataSetScaleRelat(UnitData *ud, unsigned ch, double value)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->scaleRelat[ch] = (float)value;
	return 0;
}

int
unitDataSetOffsetVolt(UnitData *ud, unsigned ch, double value)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->offsetVolt[ch] = (float)value;
	return 0;
}

size_t
unitDataGetSerializedSize(unsigned numChannels)
{
	return O_PAYLOAD + \
		IO_DAT + sizeof( *((UnitData*)NULL)->scaleVolt ) * numChannels + \
		IO_DAT + sizeof( *((UnitData*)NULL)->scaleRelat ) * numChannels + \
		IO_DAT + sizeof( *((UnitData*)NULL)->offsetVolt ) * numChannels + \
		1; /* terminating tag */
}

static int
serializeFloat(const float *srcp, size_t numChannels, uint8_t *item)
{
int i,j;
uint8_t *dstp;

	/* we already checked that ud->numChannels <= 15 */
	item[IO_SIZ] = sizeof(*srcp) * numChannels;
	dstp = item + IO_DAT;
	for ( i = 0; i < numChannels; ++i ) {
		union {
			uint32_t u;
			float    f;
		} tmp;
		tmp.f = srcp[i];
		for ( j = 0; j < sizeof(tmp.f); ++j ) {
			*dstp = (tmp.u & 0xff);
			tmp.u >>= 8;
			++dstp;
		}
	}
	return dstp - item;
}

int
unitDataSerialize(const UnitData *ud, uint8_t *buf, size_t bufSize)
{
size_t  totSize;
uint8_t *item;
	if ( (totSize = unitDataGetSerializedSize( ud->numChannels )) > bufSize ) {
		return -ENOSPC;
	}
	if ( LAYOUT_VERSION_1 != ud->version || ud->numChannels > 15 || totSize > 65535 ) {
		return -ENOTSUP;
	}
	buf[O_VERSION]     = MAKE_VERSION( LAYOUT_VERSION_1, ud->numChannels );
	buf[O_CHECK]       = MAKE_CHECK( buf[O_VERSION] );
	buf[O_TOTSIZE]     = (totSize & 0xff);
	buf[O_TOTSIZE + 1] = ((totSize >> 8) & 0xff);
	item               = buf + O_PAYLOAD;
	item[IO_TAG]       = TAG_SCALE_VOLTS;
	item              += serializeFloat( ud->scaleVolt, ud->numChannels, item );
	item[IO_TAG]       = TAG_OFFSET_VOLTS;
	item              += serializeFloat( ud->offsetVolt, ud->numChannels, item );
	item[IO_TAG]       = TAG_SCALE_RELAT;
	item              += serializeFloat( ud->scaleRelat, ud->numChannels, item );
	item[IO_TAG]       = TAG_TERM;
	++item;
	if ( item - buf != totSize ) {
		fprintf(stderr, "Internal Error (%s) - totSize miscalculated\n", __PRETTY_FUNCTION__);
		abort();
	}
	return totSize;
}
