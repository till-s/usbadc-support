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

#define LAYOUT_VERSION_1 0x1 /* obsolete */
#define LAYOUT_VERSION_2 0x2
#define NUM_CHANNELS( vers )   ( ((vers) >> 4 ) & 0xf )
#define LAYOUT_VERSION( vers ) ( ((vers) >> 0 ) & 0xf )
#define MAKE_VERSION( vers, numChannels ) ((uint8_t)((((numChannels)&0xf)<<4) | ((vers) & 0xf)))
#define MAKE_CHECK( version ) ( (uint8_t) ~ (version) )

#define IO_TAG 0
#define IO_SIZ 1
#define IO_DAT 2

#define TAG_SCALE_VOLTS  0x01
#define TAG_OFFSET_VOLTS 0x02
#define TAG_SCALE_RELAT  0x03
#define TAG_CALDATA      0x04
#define TAG_TERM 0xff

struct UnitData {
	unsigned      version;
	unsigned      numChannels;
	ScopeCalData *calData;
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
unitDataGetFullScaleVolt(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->calData[ch].fullScaleVolt;
}

int
unitDataSetFullScaleVolt(UnitData *ud, unsigned ch, double value)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->calData[ch].fullScaleVolt = value;
	return 0;
}

double
unitDataGetOffsetVolt(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->calData[ch].offsetVolt;
}

int
unitDataSetOffsetVolt(UnitData *ud, unsigned ch, double value)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->calData[ch].offsetVolt = value;
	return 0;
}

/* Array of all channels */
const ScopeCalData *
unitDataGetCalDataArray(const UnitData *ud)
{
	return ud->calData;
}

/* For convenience: just one channel */
const ScopeCalData *
unitDataGetCalData(const UnitData *ud, unsigned ch)
{
	check( __PRETTY_FUNCTION__, ud, ch );
	return ud->calData + ch;
}

int
unitDataCopyCalData(const UnitData *ud, unsigned ch, ScopeCalData *dest)
{
	if ( ch >= ud->numChannels || !dest ) {
		return -EINVAL;
	}
	*dest = ud->calData[ch];
	return 0;
}

/* For convenience: just one channel */
int
unitDataSetCalData(const UnitData *ud, unsigned ch, const ScopeCalData *cd)
{
	if ( ch >= ud->numChannels ) {
		return -EINVAL;
	}
	ud->calData[ch]  = *cd;
	return 0;
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
	for ( j = sizeof(tmp.f) - 1; j >= 0; --j ) {
		tmp.u = (tmp.u << 8) | srcp[j];
	}
	*dstp = tmp.f;
	return 0;
}

static int
scanDouble(double *dstp, const char *funcName, const char *itemName, const uint8_t *srcp, int check)
{
int j;
union {
	uint64_t u;
	double   d;
} tmp;
	if ( check && ! isnan(*dstp) ) {
		fprintf(stderr, "Error: (%s) - %s item found multiple times\n", funcName, itemName);
		return -EINVAL;
	}
	tmp.u = 0;
	for ( j = sizeof(tmp.d) - 1; j >= 0; --j ) {
		tmp.u = (tmp.u << 8) | srcp[j];
	}
	*dstp = tmp.d;
	return 0;
}

static int
scanCalData(ScopeCalData *dstp, const char *funcName, const char *itemName, const uint8_t *srcp, int check)
{
	int st;
	if ( (st = scanDouble( &dstp->fullScaleVolt, funcName, "fullScaleVolt", srcp, 0 )) ) {
		return st;
	}
	srcp += sizeof( dstp->fullScaleVolt );
	if ( (st = scanDouble( &dstp->offsetVolt, funcName, "offsetVolt", srcp, 0 )) ) {
		return st;
	}
	srcp += sizeof( dstp->offsetVolt );
	if ( (st = scanDouble( &dstp->postGainOffsetTick, funcName, "postGainOffsetTick", srcp, 0 )) ) {
		return st;
	}
	srcp += sizeof( dstp->postGainOffsetTick );
	return 0;	
}

static int
scanArray(void **dstp, unsigned numChannels, const char *funcName, const char *itemName, const uint8_t *item)
{
int            i,st;
const uint8_t *srcp;
size_t         itemsize;
uint8_t        tag = item[IO_TAG];

	switch ( tag ) {
		case TAG_SCALE_VOLTS  :/* fall through */
		case TAG_OFFSET_VOLTS :/* fall through */
		case TAG_SCALE_RELAT  :
				itemsize = sizeof( float        );
			break;
		case TAG_CALDATA      :
				itemsize = sizeof( ScopeCalData );
			break;
		default:
			fprintf(stderr, "Internal Error: (%s) - unsupported tag 0x%02x\n", funcName, tag);
			return -EINVAL;
	}

	if ( dstp ) {
		if ( *dstp ) {
			fprintf(stderr, "Error: (%s) - %s item found multiple times\n", funcName, itemName);
			return -EINVAL;
		}
		if ( itemsize * numChannels != item[IO_SIZ] ) {
			fprintf(stderr, "Error: (%s) - ill-formed %s item (unexpected size %u)\n", funcName, itemName, item[IO_SIZ]);
			return -EINVAL;
		}
		if ( ! ( *dstp = malloc( itemsize * numChannels ) ) ) {
			fprintf(stderr, "Error: (%s) - no memory for item %s\n", funcName, itemName);
			return -EINVAL;
		}
	} else {
		fprintf(stderr, "Warning: (%s) - %s no destination for item; skipping\n", funcName, itemName);
	}

	srcp = item + IO_DAT;
	for ( i = 0; i < numChannels; ++i ) {
		if ( dstp ) {
			switch ( tag ) {
				case TAG_SCALE_VOLTS  :/* fall through */
				case TAG_OFFSET_VOLTS :/* fall through */
				case TAG_SCALE_RELAT  :
					if ( (st = scanFloat( ((float*)(*dstp)) + i, funcName, itemName, srcp, 0 )) ) {
						return st;
					} 
					break;
				case TAG_CALDATA:
					if ( (st = scanCalData( ((ScopeCalData*)(*dstp)) + i, funcName, itemName, srcp, 0 )) ) {
						return st;
					} 
					break;
				default:
					fprintf(stderr, "Fatal Error: (%s) - unsupported tag 0x%02x\n", funcName, tag);
					abort(); /* checked above alread; can only happen if new tags were added but this branch forgotten */
				}
		}
		srcp += itemsize;
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
uint8_t   tag;

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
	if ( LAYOUT_VERSION_2 != layoutVersion ) {
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
		switch ( (tag = buf[off + IO_TAG]) ) {
			case TAG_CALDATA:
				if ( (st = scanArray((void**)&ud->calData, ud->numChannels, __PRETTY_FUNCTION__, "ScopeCalData", buf + off)) < 0 ) {
					goto bail;
				}
			break;
			/* tags not used but keep code around */
			case TAG_SCALE_VOLTS:
				if ( (st = scanArray(NULL, ud->numChannels, __PRETTY_FUNCTION__, "ScaleVolt", buf + off)) < 0 ) {
					goto bail;
				}
				break;
			case TAG_OFFSET_VOLTS:
				if ( (st = scanArray(NULL, ud->numChannels, __PRETTY_FUNCTION__, "OffsetVolt", buf + off)) ) {
					goto bail;
				}
				break;
			case TAG_SCALE_RELAT:
				if ( (st = scanArray(NULL, ud->numChannels, __PRETTY_FUNCTION__, "ScaleRelat", buf + off)) ) {
					goto bail;
				}
				break;
			default:
				fprintf(stderr, "Warning: (%s) - unsupported tag 0x%02" PRIx8 "\n", __PRETTY_FUNCTION__, buf[off + IO_TAG]);
				break;
		}
	}

	if ( ! ud->calData ) {
		fprintf(stderr, "Error: (%s) - incomplete data; CalData not found\n", __PRETTY_FUNCTION__);
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
		free( ud->calData );
		free( (UnitData*)ud );
	}
}

UnitData *
unitDataCreate(unsigned numChannels)
{
UnitData *ud;
int       ch;
	if ( numChannels > 15 ) {
		fprintf(stderr, "Error: (%s) num channels too big\n", __PRETTY_FUNCTION__);
		return NULL;
	}
	if ( ! (ud = calloc( sizeof(*ud), 1 )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		return NULL;
	}
	if ( ! (ud->calData = malloc( sizeof(*ud->calData) * numChannels )) ) {
		fprintf(stderr,"Error: (%s) no memory\n", __PRETTY_FUNCTION__);
		free( ud );
		return NULL;
	}
	ud->version     = LAYOUT_VERSION_2;
	ud->numChannels = numChannels;

	for ( ch = 0; ch < numChannels; ++ch ) {
		scope_cal_data_init( &ud->calData[ch] );
	}
	return ud;
}

size_t
unitDataGetSerializedSize(unsigned numChannels)
{
	return O_PAYLOAD + \
		IO_DAT + sizeof( *((UnitData*)NULL)->calData ) * numChannels + \
		1; /* terminating tag */
}

static int
serializeFloat(const float *srcp, uint8_t *item) __attribute__((unused));

static int
serializeFloat(const float *srcp, uint8_t *item)
{
int      j;
uint8_t *dstp;
union {
	uint32_t u;
	float    f;
}        tmp;

	dstp = item;
	tmp.f = *srcp;
	for ( j = 0; j < sizeof(tmp.f); ++j ) {
		*dstp = (tmp.u & 0xff);
		tmp.u >>= 8;
		++dstp;
	}
	return dstp - item;
}

static int
serializeDouble(const double *srcp, uint8_t *item)
{
int      j;
uint8_t *dstp;
union {
	uint64_t u;
	double   d;
}        tmp;

	dstp = item;
	tmp.d = *srcp;
	for ( j = 0; j < sizeof(tmp.d); ++j ) {
		*dstp = (tmp.u & 0xff);
		tmp.u >>= 8;
		++dstp;
	}
	return dstp - item;
}

static int
serializeCalData(const ScopeCalData *srcp, uint8_t *item)
{
int      incr;
uint8_t *dstp;
	dstp = item;
	if ( (incr = serializeDouble( &srcp->fullScaleVolt,      dstp )) < 0 ) {
		return incr;
	} 
	dstp += incr;
	if ( (incr = serializeDouble( &srcp->offsetVolt,         dstp )) < 0 ) {
		return incr;
	} 
	dstp += incr;
	if ( (incr = serializeDouble( &srcp->postGainOffsetTick, dstp )) < 0 ) {
		return incr;
	} 
	dstp += incr;
	return dstp - item;
}


int
unitDataSerialize(const UnitData *ud, uint8_t *buf, size_t bufSize)
{
size_t  totSize;
uint8_t *item;
unsigned ch;
int      status;
	if ( (totSize = unitDataGetSerializedSize( ud->numChannels )) > bufSize ) {
		return -ENOSPC;
	}
	if ( LAYOUT_VERSION_2 != ud->version || ud->numChannels > 15 || totSize > 65535 ) {
		return -ENOTSUP;
	}
	buf[O_VERSION]     = MAKE_VERSION( LAYOUT_VERSION_2, ud->numChannels );
	buf[O_CHECK]       = MAKE_CHECK( buf[O_VERSION] );
	buf[O_TOTSIZE]     = (totSize & 0xff);
	buf[O_TOTSIZE + 1] = ((totSize >> 8) & 0xff);
	item               = buf + O_PAYLOAD;
	item[IO_TAG]       = TAG_CALDATA;
	item[IO_SIZ]       = sizeof(ud->calData[0])*ud->numChannels;
	item              += IO_DAT;
	for ( ch = 0; ch < ud->numChannels; ++ch ) {
		if ( (status = serializeCalData( ud->calData + ch, item )) < 0 ) {
				return status;
		}
		item              += status;
	}
	item[IO_TAG]       = TAG_TERM;
	++item;
	if ( item - buf != totSize ) {
		fprintf(stderr, "Internal Error (%s) - totSize miscalculated; got %zd, expected %zd\n", __PRETTY_FUNCTION__, item - buf, totSize);
		abort();
	}
	return totSize;
}
