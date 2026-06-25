#include "test.h"
#include "throttle.h"
#include "mock_hw.h"

// min_voltage_adc = 1100mV * 256 / 5000 = 56 (truncated)
// max_voltage_adc = 4300mV * 256 / 5000 = 220 (truncated)
#define MIN_MV 1100
#define MAX_MV 4300
#define MIN_ADC 56
#define MAX_ADC 220

static int test_throttle_maps_full_range(void)
{
	mock_reset();
	throttle_init(MIN_MV, MAX_MV, 0);

	mock_set_throttle_adc(MIN_ADC);
	ASSERT_EQ(1, throttle_read());

	mock_set_throttle_adc(MAX_ADC);
	ASSERT_EQ(100, throttle_read());

	return 1;
}

static int test_throttle_low_end_hysteresis_and_cutoff(void)
{
	mock_reset();
	throttle_init(MIN_MV, MAX_MV, 0);

	// Run up to full throttle first so the hysteresis nudge below has
	// a nonzero previous reading to react to.
	mock_set_throttle_adc(MAX_ADC);
	ASSERT_EQ(100, throttle_read());

	// One ADC count below the low threshold: hysteresis nudges the
	// reading back up to the threshold instead of snapping to 0.
	mock_set_throttle_adc(MIN_ADC - 1);
	ASSERT_EQ(1, throttle_read());

	// Two counts below: now it cuts to 0.
	mock_set_throttle_adc(MIN_ADC - 2);
	ASSERT_EQ(0, throttle_read());

	// Once at 0%, the hysteresis nudge no longer applies, so it stays at 0.
	mock_set_throttle_adc(MIN_ADC - 2);
	ASSERT_EQ(0, throttle_read());

	// Recovering back to the threshold resumes normal mapping.
	mock_set_throttle_adc(MIN_ADC);
	ASSERT_EQ(1, throttle_read());

	return 1;
}

static int test_throttle_hard_limit_fault_after_tolerance(void)
{
	mock_reset();
	mock_set_ms(1000);
	throttle_init(MIN_MV, MAX_MV, 0);

	// throttle_ok() requires a below-minimum reading to have been seen at
	// least once (throttle_low_ok) before it will report OK. Released
	// throttle (below MIN_ADC, but still within the hard limits) gives it
	// that one valid low reading.
	mock_set_throttle_adc(MIN_ADC - 10);
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	// Below the hard low limit (ADC 25), but still within the 100ms
	// tolerance window, so throttle_ok() should not trip yet.
	mock_set_throttle_adc(10);
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	mock_advance_ms(50);
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	// Past the tolerance window: now it's a fault.
	mock_advance_ms(60);
	throttle_read();
	ASSERT_TRUE(!throttle_ok());

	// Recovering to a valid reading clears the fault.
	mock_set_throttle_adc(MIN_ADC);
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	return 1;
}

static int test_throttle_map_response_uses_custom_curve(void)
{
	ASSERT_EQ(0, throttle_map_response(0));
	ASSERT_EQ(100, throttle_map_response(100));
	ASSERT_EQ(35, throttle_map_response(50));
	return 1;
}

static int test_throttle_upper_deadband_reaches_full_early(void)
{
	mock_reset();
	// 10% upper deadband. range = MAX_ADC - MIN_ADC = 164;
	// reduction = 164 * 10 / 100 = 16; effective max = 220 - 16 = 204.
	throttle_init(MIN_MV, MAX_MV, 10);

	// at the lowered effective max, throttle already reads 100%
	mock_set_throttle_adc(204);
	ASSERT_EQ(100, throttle_read());

	// the whole deadband region (above the effective max) also reads 100%
	mock_set_throttle_adc(MAX_ADC);
	ASSERT_EQ(100, throttle_read());

	// just below the effective max it is still under 100% (deadband starts at 204)
	mock_set_throttle_adc(203);
	ASSERT_TRUE(throttle_read() < 100);

	return 1;
}

static int test_throttle_zero_deadband_matches_full_travel(void)
{
	mock_reset();
	throttle_init(MIN_MV, MAX_MV, 0);

	// with no deadband, 100% is only reached at the true max travel
	mock_set_throttle_adc(MAX_ADC - 1);
	ASSERT_TRUE(throttle_read() < 100);

	mock_set_throttle_adc(MAX_ADC);
	ASSERT_EQ(100, throttle_read());

	return 1;
}

void test_throttle_run(void)
{
	RUN_TEST(test_throttle_maps_full_range);
	RUN_TEST(test_throttle_low_end_hysteresis_and_cutoff);
	RUN_TEST(test_throttle_hard_limit_fault_after_tolerance);
	RUN_TEST(test_throttle_map_response_uses_custom_curve);
	RUN_TEST(test_throttle_upper_deadband_reaches_full_early);
	RUN_TEST(test_throttle_zero_deadband_matches_full_travel);
}
