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

// Writes a version 5 config (header + payload) to EEPROM page 0, as firmware
// before this fork's version 6 would have left it.
static void store_v5_config(const config_t* cfg)
{
	const uint8_t* bytes = (const uint8_t*)cfg;
	uint8_t checksum = 0;
	for (int i = 0; i < CONFIG_V5_SIZE; ++i)
	{
		checksum += bytes[i];
	}

	g_hw.eeprom[0][0] = 5;
	g_hw.eeprom[0][1] = CONFIG_V5_SIZE;
	g_hw.eeprom[0][2] = checksum;
	for (int i = 0; i < CONFIG_V5_SIZE; ++i)
	{
		g_hw.eeprom[0][3 + i] = bytes[i];
	}
}

static int test_cfgstore_migrates_version_5_config(void)
{
	fake_hw_reset();
	cfgstore_init();

	// a rider's version 5 settings, different from the defaults
	g_config.max_current_amps = 33;
	g_config.low_cut_off_v = 44;
	g_config.assist_levels[0][3].target_current_percent = 42;
	g_config.assist_levels[1][9].max_speed_percent = 77;
	config_t v5 = g_config;

	fake_hw_reset();
	store_v5_config(&v5);
	cfgstore_init();

	// version 5 settings survive...
	ASSERT_EQ(33, g_config.max_current_amps);
	ASSERT_EQ(44, g_config.low_cut_off_v);
	ASSERT_EQ(42, g_config.assist_levels[0][3].target_current_percent);
	ASSERT_EQ(77, g_config.assist_levels[1][9].max_speed_percent);

	// ...the version 6 additions get defaults...
	ASSERT_EQ(1, g_config.pas_stop_predictive);
	ASSERT_EQ(1, g_config.pas_start_delay_pulses_rolling);
	ASSERT_EQ(10, g_config.assist_level_ext[0][3].power_start_w_div10);

	// ...and it's saved back as version 6, so the next boot reads it directly.
	ASSERT_EQ(CONFIG_VERSION, g_hw.eeprom[0][0]);
	ASSERT_EQ(sizeof(config_t), g_hw.eeprom[0][1]);
	g_config.max_current_amps = 0;
	cfgstore_init();
	ASSERT_EQ(33, g_config.max_current_amps);

	return 1;
}

static int test_cfgstore_corrupt_version_5_config_falls_back_to_defaults(void)
{
	fake_hw_reset();
	cfgstore_init();
	g_config.max_current_amps = 33;
	config_t v5 = g_config;

	fake_hw_reset();
	store_v5_config(&v5);
	g_hw.eeprom[0][3] ^= 0x01;
	cfgstore_init();

	ASSERT_EQ(30, g_config.max_current_amps);

	return 1;
}

void test_cfgstore_run(void)
{
	RUN_TEST(test_cfgstore_migrates_version_5_config);
	RUN_TEST(test_cfgstore_corrupt_version_5_config_falls_back_to_defaults);
	RUN_TEST(test_cfgstore_loads_defaults_on_erased_eeprom);
	RUN_TEST(test_cfgstore_round_trips_saved_config);
	RUN_TEST(test_cfgstore_corrupt_config_falls_back_to_defaults);
}
