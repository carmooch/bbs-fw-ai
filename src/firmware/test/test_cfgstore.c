#include "test.h"
#include "cfgstore.h"
#include "eeprom.h"
#include "mock_eeprom.h"

// Layout cfgstore.c writes: header_t {magic, version, length, checksum} at
// offsets 0-3, then the struct payload starting at offset 4.
#define HEADER_OFFSET_MAGIC		0
#define PAYLOAD_OFFSET			4

static void corrupt_config_page_byte(int offset, uint8_t value)
{
	eeprom_select_page(0); // EEPROM_CONFIG_PAGE
	eeprom_write_byte(offset, value);
	eeprom_end_write();
}

static int test_falls_back_to_defaults_on_fresh_eeprom(void)
{
	mock_eeprom_erase_all();

	cfgstore_init();

	ASSERT_EQ(42, g_config.low_cut_off_v);
	ASSERT_EQ(100, g_config.max_speed_kph);

	return 1;
}

static int test_round_trips_saved_config(void)
{
	mock_eeprom_erase_all();
	cfgstore_init();

	g_config.low_cut_off_v = 36;
	g_config.max_speed_kph = 45;
	ASSERT_TRUE(cfgstore_save_config());

	// Simulate a reboot: clear in-memory state, then load from "EEPROM" again.
	g_config.low_cut_off_v = 0;
	g_config.max_speed_kph = 0;
	cfgstore_init();

	ASSERT_EQ(36, g_config.low_cut_off_v);
	ASSERT_EQ(45, g_config.max_speed_kph);

	return 1;
}

// This is specifically what the magic byte buys over the old
// version+length+checksum-only header: corrupting only the magic byte leaves
// version, length, and the payload checksum all still self-consistent, so
// this would have been silently accepted before the magic check existed.
static int test_rejects_valid_checksum_with_wrong_magic(void)
{
	mock_eeprom_erase_all();
	cfgstore_init();

	g_config.low_cut_off_v = 36;
	ASSERT_TRUE(cfgstore_save_config());

	corrupt_config_page_byte(HEADER_OFFSET_MAGIC, 0x00);

	g_config.low_cut_off_v = 0;
	cfgstore_init();

	ASSERT_EQ(42, g_config.low_cut_off_v); // back to default, save was rejected

	return 1;
}

static int test_rejects_checksum_mismatch(void)
{
	mock_eeprom_erase_all();
	cfgstore_init();

	g_config.low_cut_off_v = 36;
	ASSERT_TRUE(cfgstore_save_config());

	// Flip the first payload byte without updating the stored checksum.
	corrupt_config_page_byte(PAYLOAD_OFFSET, 0xAB);

	g_config.low_cut_off_v = 0;
	cfgstore_init();

	ASSERT_EQ(42, g_config.low_cut_off_v); // back to default, save was rejected

	return 1;
}

void test_cfgstore_run(void)
{
	RUN_TEST(test_falls_back_to_defaults_on_fresh_eeprom);
	RUN_TEST(test_round_trips_saved_config);
	RUN_TEST(test_rejects_valid_checksum_with_wrong_magic);
	RUN_TEST(test_rejects_checksum_mismatch);
}
