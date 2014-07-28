#include "firmware.h"

#if MAXAXES > 0 || MAXEXTRUDER > 0
void Motor::load (int16_t &addr, bool eeprom)
{
	bool e = GET (enable_pin, false);
	bool d = GET (dir_pin, false);
	bool s = GET (step_pin, false);
	step_pin.read (read_16 (addr, eeprom));
	dir_pin.read (read_16 (addr, eeprom));
	enable_pin.read (read_16 (addr, eeprom));
	steps_per_um = read_micro (addr, eeprom);
	limit_v = read_micro (addr, eeprom);
	max_v = read_micro (addr, eeprom);
	limit_a = read_milli (addr, eeprom);
	max_steps = read_8 (addr, eeprom);
	if (max_steps <= 0)
		max_steps = 1;
	SET_OUTPUT (step_pin);
	SET_OUTPUT (dir_pin);
	SET_OUTPUT (enable_pin);
	if (e)
		SET (enable_pin);
	else
		RESET (enable_pin);
	if (d)
		SET (dir_pin);
	else
		RESET (dir_pin);
	if (s)
		SET (step_pin);
	else
		RESET (step_pin);
}

void Motor::save (int16_t &addr, bool eeprom)
{
	write_16 (addr, step_pin.write (), eeprom);
	write_16 (addr, dir_pin.write (), eeprom);
	write_16 (addr, enable_pin.write (), eeprom);
	write_micro (addr, steps_per_um, eeprom);
	write_micro (addr, limit_v, eeprom);
	write_micro (addr, max_v, eeprom);
	write_milli (addr, limit_a, eeprom);
	write_8 (addr, max_steps, eeprom);
}
#endif
