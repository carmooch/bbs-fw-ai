// Host-side stand-in for the page-based EEPROM driver (bbsx/eeprom.c,
// tsdz2/eeprom.c). Backs each page with an in-memory byte array so cfgstore.c
// can be exercised without real flash.

#include "mock_eeprom.h"
#include "eeprom.h"

#include <string.h>

#define MOCK_EEPROM_NUM_PAGES	2
#define MOCK_EEPROM_PAGE_SIZE	512

static uint8_t pages[MOCK_EEPROM_NUM_PAGES][MOCK_EEPROM_PAGE_SIZE];
static int selected_page = -1;

void mock_eeprom_erase_all(void)
{
	memset(pages, 0xFF, sizeof(pages));
	selected_page = -1;
}

void eeprom_init(void)
{
}

bool eeprom_select_page(int page)
{
	if (page >= 0 && page < MOCK_EEPROM_NUM_PAGES)
	{
		selected_page = page;
		return true;
	}

	return false;
}

int eeprom_read_byte(int offset)
{
	if (selected_page < 0 || offset < 0 || offset >= MOCK_EEPROM_PAGE_SIZE)
	{
		return -1;
	}

	return pages[selected_page][offset];
}

bool eeprom_erase_page(void)
{
	if (selected_page < 0)
	{
		return false;
	}

	memset(pages[selected_page], 0xFF, MOCK_EEPROM_PAGE_SIZE);
	return true;
}

bool eeprom_write_byte(int offset, uint8_t value)
{
	if (selected_page < 0 || offset < 0 || offset >= MOCK_EEPROM_PAGE_SIZE)
	{
		return false;
	}

	pages[selected_page][offset] = value;
	return true;
}

bool eeprom_end_write(void)
{
	return true;
}
