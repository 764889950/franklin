#include "firmware.h"

void Axis::load (int16_t &addr, bool eeprom)
{
	motor.load (addr, eeprom);
	limit_min_pin.read (read_16 (addr, eeprom));
	limit_max_pin.read (read_16 (addr, eeprom));
	sense_pin.read (read_16 (addr, eeprom));
	limit_pos = read_float (addr, eeprom);
	axis_min = read_float (addr, eeprom);
	axis_max = read_float (addr, eeprom);
	float f = read_float (addr, eeprom);
	motor_min = isnan (f) ? MAXLONG : f * motor.steps_per_mm;
	f = read_float (addr, eeprom);
	motor_max = isnan (f) ? MAXLONG : f * motor.steps_per_mm;
	park = read_float (addr, eeprom);
	delta_length = read_float (addr, eeprom);
	delta_radius = read_float (addr, eeprom);
	offset = read_float (addr, eeprom);
	SET_INPUT (limit_min_pin);
	SET_INPUT (limit_max_pin);
	SET_INPUT (sense_pin);
#define sin120 0.8660254037844386	// .5*sqrt(3)
#define cos120 -.5
	// Coordinates of axes (at angles 0, 120, 240; each with its own radius).
	if (this == &axis[0]) {
		x = delta_radius;
		y = 0;
		z = sqrt (delta_length * delta_length - delta_radius * delta_radius);
	} else if (this == &axis[1]) {
		x = delta_radius * cos120;
		y = delta_radius * sin120;
		z = sqrt (delta_length * delta_length - delta_radius * delta_radius);
	} else if (this == &axis[2]) {
		x = delta_radius * cos120;
		y = delta_radius * -sin120;
		z = sqrt (delta_length * delta_length - delta_radius * delta_radius);
	}
}

void Axis::save (int16_t &addr, bool eeprom)
{
	motor.save (addr, eeprom);
	write_16 (addr, limit_min_pin.write (), eeprom);
	write_16 (addr, limit_max_pin.write (), eeprom);
	write_16 (addr, sense_pin.write (), eeprom);
	write_float (addr, limit_pos, eeprom);
	write_float (addr, axis_min, eeprom);
	write_float (addr, axis_max, eeprom);
	write_float (addr, motor_min == MAXLONG ? NAN : motor_min / motor.steps_per_mm, eeprom);
	write_float (addr, motor_max == MAXLONG ? NAN : motor_max / motor.steps_per_mm, eeprom);
	write_float (addr, park, eeprom);
	write_float (addr, delta_length, eeprom);
	write_float (addr, delta_radius, eeprom);
	write_float (addr, offset, eeprom);
}
