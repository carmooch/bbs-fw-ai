#ifndef _MOCK_EEPROM_H_
#define _MOCK_EEPROM_H_

// Resets all mock EEPROM pages to an erased (0xFF-filled) state, mimicking
// a fresh chip. Call at the start of every test that exercises cfgstore.
void mock_eeprom_erase_all(void);

#endif
