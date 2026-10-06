#include "test.h"
#include "fake_hw.h"
#include "cfgstore.h"

static int test_cfgstore_loads_defaults_on_erased_eeprom(void)
{
	fake_hw_reset();
	cfgstore_init();

	ASSERT_EQ(30, g_config.max_current_amps);
	ASSERT_EQ(3, g_config.assist_startup_level);

	return 1;
}

static int test_cfgstore_round_trips_saved_config(void)
{
	fake_hw_reset();
	cfgstore_init();

	g_config.max_current_amps = 22;
	ASSERT_TRUE(cfgstore_save_config());

	// Simulate a reboot: forget RAM, keep the EEPROM.
	g_config.max_current_amps = 0;
	cfgstore_init();
	ASSERT_EQ(22, g_config.max_current_amps);

	return 1;
}

static int test_cfgstore_corrupt_config_falls_back_to_defaults(void)
{
	fake_hw_reset();
	cfgstore_init();

	g_config.max_current_amps = 22;
	ASSERT_TRUE(cfgstore_save_config());

	// Flip bits in the stored payload (past the 3-byte header) so the
	// checksum no longer matches.
	g_hw.eeprom[0][3] ^= 0x01;

	cfgstore_init();
	ASSERT_EQ(30, g_config.max_current_amps);

	return 1;
}

void test_cfgstore_run(void)
{
	RUN_TEST(test_cfgstore_loads_defaults_on_erased_eeprom);
	RUN_TEST(test_cfgstore_round_trips_saved_config);
	RUN_TEST(test_cfgstore_corrupt_config_falls_back_to_defaults);
}
