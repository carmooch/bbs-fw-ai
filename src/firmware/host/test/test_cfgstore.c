#include "test.h"
#include "fake_hw.h"
#include "cfgstore.h"
#include "fwconfig.h"

#include <string.h>

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

// Writes an older config (header + the payload's first size bytes) to EEPROM
// page 0, as earlier firmware would have left it.
static void store_old_config(const config_t* cfg, uint8_t version, uint8_t size)
{
	const uint8_t* bytes = (const uint8_t*)cfg;
	uint8_t checksum = 0;
	for (int i = 0; i < size; ++i)
	{
		checksum += bytes[i];
	}

	g_hw.eeprom[0][0] = version;
	g_hw.eeprom[0][1] = size;
	g_hw.eeprom[0][2] = checksum;
	for (int i = 0; i < size; ++i)
	{
		g_hw.eeprom[0][3 + i] = bytes[i];
	}
}

static void store_v5_config(const config_t* cfg)
{
	store_old_config(cfg, 5, CONFIG_V5_SIZE);
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
	ASSERT_EQ(DISPLAY_RANGE_FIELD_DATA, g_config.display_range_field);
	ASSERT_EQ(110, g_config.effort_mass_kg);
	ASSERT_EQ(0, g_config.assist_effort_x10[0][3]);

	// ...and it's saved back as the current version, so the next boot reads
	// it directly.
	ASSERT_EQ(CONFIG_VERSION, g_hw.eeprom[0][0]);
	ASSERT_EQ(sizeof(config_t), g_hw.eeprom[0][1]);
	g_config.max_current_amps = 0;
	cfgstore_init();
	ASSERT_EQ(33, g_config.max_current_amps);

	return 1;
}

static int test_cfgstore_migrates_version_6_config(void)
{
	fake_hw_reset();
	cfgstore_init();

	// version 6 settings, different from the defaults
	g_config.max_current_amps = 33;
	g_config.pas_stop_predictive = 0;
	g_config.cadence_lock_margin_rpm = 15;
	g_config.assist_level_ext[1][9].current_ramp_amps_s = 77;
	config_t v6 = g_config;

	fake_hw_reset();
	store_old_config(&v6, 6, CONFIG_V6_SIZE);
	cfgstore_init();

	// version 6 settings survive...
	ASSERT_EQ(33, g_config.max_current_amps);
	ASSERT_EQ(0, g_config.pas_stop_predictive);
	ASSERT_EQ(15, g_config.cadence_lock_margin_rpm);
	ASSERT_EQ(77, g_config.assist_level_ext[1][9].current_ramp_amps_s);

	// ...the version 7 additions get defaults (effort assist off)...
	ASSERT_EQ(DISPLAY_RANGE_FIELD_DATA, g_config.display_range_field);
	ASSERT_EQ(110, g_config.effort_mass_kg);
	ASSERT_EQ(80, g_config.effort_stroke_x100);
	ASSERT_EQ(25, g_config.effort_max_speed_kph);
	for (int m = 0; m < 2; ++m)
		for (int l = 0; l < 10; ++l)
			ASSERT_EQ(0, g_config.assist_effort_x10[m][l]);

	// ...and it's saved back as version 7
	ASSERT_EQ(CONFIG_VERSION, g_hw.eeprom[0][0]);
	ASSERT_EQ(sizeof(config_t), g_hw.eeprom[0][1]);

	return 1;
}

static int test_cfgstore_effort_pattern_round_trips_beside_config(void)
{
	fake_hw_reset();
	cfgstore_init();
	g_config.max_current_amps = 33;
	ASSERT_TRUE(cfgstore_save_config());

	uint8_t none[8];
	ASSERT_TRUE(!cfgstore_read_effort_pattern(none, sizeof(none)));

	uint8_t pattern[196], back[196];
	for (int i = 0; i < (int)sizeof(pattern); ++i)
		pattern[i] = (uint8_t)(i * 7 + 3);
	ASSERT_TRUE(cfgstore_write_effort_pattern(pattern, sizeof(pattern)));
	ASSERT_TRUE(cfgstore_read_effort_pattern(back, sizeof(back)));
	ASSERT_TRUE(memcmp(pattern, back, sizeof(pattern)) == 0);

	// a stored pattern of another size (an older layout) isn't used
	ASSERT_TRUE(!cfgstore_read_effort_pattern(back, 192));

	// the config is untouched
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
	RUN_TEST(test_cfgstore_migrates_version_6_config);
	RUN_TEST(test_cfgstore_effort_pattern_round_trips_beside_config);
	RUN_TEST(test_cfgstore_corrupt_version_5_config_falls_back_to_defaults);
	RUN_TEST(test_cfgstore_loads_defaults_on_erased_eeprom);
	RUN_TEST(test_cfgstore_round_trips_saved_config);
	RUN_TEST(test_cfgstore_corrupt_config_falls_back_to_defaults);
}
