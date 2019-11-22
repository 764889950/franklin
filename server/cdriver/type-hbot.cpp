/* type-hbot.cpp - H-bot geometry handling for Franklin
 * vim: set foldmethod=marker :
 * Copyright 2014-2016 Michigan Technological University
 * Copyright 2016-2019 Bas Wijnen <wijnen@debian.org>
 * Author: Bas Wijnen <wijnen@debian.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "cdriver.h"

// x = (u + v) / 2
// y = (u - v) / 2

// u = x + y
// v = x - y

static void xyz2motors(Space *s) { // {{{
	s->motor[0]->target_pos = s->axis[0]->target + s->axis[1]->target;
	s->motor[1]->target_pos = s->axis[0]->target - s->axis[1]->target;
} // }}}

static void motors2xyz(Space *s, const double *motors, double *xyz) { // {{{
	(void)(&s);
	xyz[0] = (motors[0] + motors[1]) / 2;
	xyz[1] = (motors[0] - motors[1]) / 2;
} // }}}

static void check_position(Space *s, double *data) { // {{{
	(void)&s;
	(void)&data;
} // }}}

static void load(Space *s) { // {{{
	if (!s->setup_nums(2, 2)) {
		debug("Failed to set up H-bot axes");
		s->cancel_update();
	}
} // }}}

static void aload(Space *s, int a) { // {{{
	(void)&s;
	(void)&a;
} // }}}

static void mload(Space *s, int m) { // {{{
	(void)&s;
	(void)&m;
} // }}}

static void save(Space *s) { // {{{
	(void)&s;
} // }}}

static void asave(Space *s, int a) { // {{{
	(void)&s;
	(void)&a;
} // }}}

static void msave(Space *s, int m) { // {{{
	(void)&s;
	(void)&m;
} // }}}

static bool init(Space *s) { // {{{
	(void)&s;
	return true;
} // }}}

static void ainit(Space *s, int a) { // {{{
	(void)&s;
	(void)&a;
} // }}}

static void minit(Space *s, int m) { // {{{
	(void)&s;
	(void)&m;
} // }}}

static void space_free(Space *s) { // {{{
	(void)&s;
} // }}}

static void axis_free(Space *s, int a) { // {{{
	(void)&s;
	(void)&a;
} // }}}

static void motor_free(Space *s, int m) { // {{{
	(void)&s;
	(void)&m;
} // }}}

static double change0(Space *s, int axis, double value) { // {{{
	(void)&s;
	(void)&axis;
	return value;
} // }}}

static double unchange0(Space *s, int axis, double value) { // {{{
	(void)&s;
	(void)&axis;
	return value;
} // }}}

static double probe_speed(Space *s) { // {{{
	if (s->num_motors >= 3)
		return 1e6 / settings.hwtime_step / s->motor[2]->steps_per_unit;
	return INFINITY;
} // }}}

static int follow(Space *s, int axis) { // {{{
	(void)&s;
	(void)&axis;
	return -1;
} // }}}

void Hbot_init(int num) { // {{{
	space_types[num].xyz2motors = xyz2motors;
	space_types[num].check_position = check_position;
	space_types[num].init_space = init;
	space_types[num].init_axis = ainit;
	space_types[num].init_motor = minit;
	space_types[num].load_space = load;
	space_types[num].load_axis = aload;
	space_types[num].load_motor = mload;
	space_types[num].save_space = save;
	space_types[num].save_axis = asave;
	space_types[num].save_motor = msave;
	space_types[num].free_space = space_free;
	space_types[num].free_axis = axis_free;
	space_types[num].free_motor = motor_free;
	space_types[num].change0 = change0;
	space_types[num].unchange0 = unchange0;
	space_types[num].probe_speed = probe_speed;
	space_types[num].follow = follow;
	space_types[num].motors2xyz = motors2xyz;
} // }}}
