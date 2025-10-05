#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* volt = counts/maxCounts * fullScaleVolt */
typedef struct ScopeCalData {
	/* full-scale voltage (voltage to generate max ADC counts
	 * at max gain).
	 */
	double fullScaleVolt;
	/* offset of input stage (pre-gain) */
	double offsetVolt;
	/* offset post-gain (e.g., ADC offset) in ticks */
	double postGainOffsetTick;
} ScopeCalData;

void
scope_cal_data_init(ScopeCalData *);

#ifdef __cplusplus
};
#endif


