#include "test.h"
#include "fake_hw.h"
#include "throttle.h"

// ADC counts are mV * 256 / 5000, truncated:
//   1100mV -> 56, 4300mV -> 220, hard limits 500mV -> 25 and 4500mV -> 230.
#define MIN_MV		1100
#define MAX_MV		4300
#define MIN_ADC		56
#define MAX_ADC		220

static int test_throttle_maps_configured_range_to_1_to_100(void)
{
	fake_hw_reset();
	throttle_init(MIN_MV, MAX_MV);

	g_hw.throttle_adc = MIN_ADC;
	ASSERT_EQ(1, throttle_read());

	g_hw.throttle_adc = (MIN_ADC + MAX_ADC) / 2;
	ASSERT_EQ(50, throttle_read());

	g_hw.throttle_adc = MAX_ADC;
	ASSERT_EQ(100, throttle_read());

	// Above the configured max (but inside the hard limit) clamps to 100.
	g_hw.throttle_adc = MAX_ADC + 5;
	ASSERT_EQ(100, throttle_read());

	return 1;
}

static int test_throttle_low_end_hysteresis(void)
{
	fake_hw_reset();
	throttle_init(MIN_MV, MAX_MV);

	g_hw.throttle_adc = MAX_ADC;
	ASSERT_EQ(100, throttle_read());

	// One count below the start threshold holds at 1% instead of cutting out.
	g_hw.throttle_adc = MIN_ADC - 1;
	ASSERT_EQ(1, throttle_read());

	// Two counts below cuts to 0, and it stays there.
	g_hw.throttle_adc = MIN_ADC - 2;
	ASSERT_EQ(0, throttle_read());
	g_hw.throttle_adc = MIN_ADC - 1;
	ASSERT_EQ(0, throttle_read());

	return 1;
}

static int test_throttle_out_of_range_faults_after_tolerance(void)
{
	fake_hw_reset();
	throttle_init(MIN_MV, MAX_MV);

	// A released throttle reads below the start threshold: that's healthy.
	g_hw.throttle_adc = MIN_ADC - 10;
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	// Below the hard low limit (e.g. a broken wire) is tolerated for 100ms...
	g_hw.throttle_adc = 10;
	throttle_read();
	g_hw.now_ms += 100;
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	// ...and is a fault after that.
	g_hw.now_ms += 1;
	throttle_read();
	ASSERT_TRUE(!throttle_ok());

	// Back in range clears the fault.
	g_hw.throttle_adc = MIN_ADC;
	throttle_read();
	ASSERT_TRUE(throttle_ok());

	return 1;
}

static int test_throttle_response_curve_endpoints(void)
{
	ASSERT_EQ(0, throttle_map_response(0));
	ASSERT_EQ(100, throttle_map_response(100));
	return 1;
}

void test_throttle_run(void)
{
	RUN_TEST(test_throttle_maps_configured_range_to_1_to_100);
	RUN_TEST(test_throttle_low_end_hysteresis);
	RUN_TEST(test_throttle_out_of_range_faults_after_tolerance);
	RUN_TEST(test_throttle_response_curve_endpoints);
}
