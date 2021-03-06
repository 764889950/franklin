#ifndef FRANKLIN_MODULE
#define FRANKLIN_MODULE

#define ARCH_INCLUDE "arch-avr.h"
#include "cdriver.h"

#define UseSpace(SpaceData) \
	static inline SpaceData &mySpace(Space *s) { return *reinterpret_cast <SpaceData *>(s->type_data); } \
	bool init_space(Space *s) { s->type_data = new SpaceData; return s->type_data != NULL; } \
	void free_space(Space *s) { delete reinterpret_cast <SpaceData *>(s->type_data); } \
	void free_space(Space *s)

#define UseAxis(AxisData) \
	static inline AxisData &myAxis(Space *s, int a) { return *reinterpret_cast <AxisData *>(s->axis[a]->type_data); } \
	void init_axis(Space *s, int a) { s->axis[a]->type_data = new AxisData; } \
	void free_axis(Space *s, int a) { delete reinterpret_cast <AxisData *>(s->axis[a]->type_data); } \
	void free_axis(Space *s, int a)

#define UseMotor(MotorData) \
	static inline MotorData &myMotor(Space *s, int m) { return *reinterpret_cast <MotorData *>(s->motor[m]->type_data); } \
	void init_motor(Space *s, int m) { s->motor[m]->type_data = new MotorData; } \
	void free_motor(Space *s, int m) { delete reinterpret_cast <MotorData *>(s->motor[m]->type_data); } \
	void free_motor(Space *s, int m)

static inline void save_int(int value) {
	shmem->ints[100 + current_int++] = value;
}

static inline int load_int() {
	return shmem->ints[100 + current_int++];
}

static inline void save_float(double value) {
	shmem->floats[100 + current_float++] = value;
}

static inline double load_float() {
	return shmem->floats[100 + current_float++];
}

static inline void save_string(const char *value) {
	strncpy(const_cast <char *>(shmem->strs[0 + current_string++]), value, PATH_MAX);
}

static inline void load_string(char *target) {
	strncpy(target, const_cast <char *>(shmem->strs[0 + current_string++]), PATH_MAX);
}

extern "C" {
	void motors2xyz(Space *s, const double *motors, double *xyz);
        void xyz2motors(Space *s);

        // Check if position is valid and if not, move it to a valid value.
        void check_position(Space *s, double *data);

        // Allocate data for space type.
        bool init_space(Space *s);
        void init_axis(Space *s, int a);
        void init_motor(Space *s, int m);

        // Fill allocated data with values from shmem.
        void load_space(Space *s);
        void load_axis(Space *s, int a);
        void load_motor(Space *s, int m);

        // Write current values back to shmem.
        void save_space(Space *s);
        void save_axis(Space *s, int a);
        void save_motor(Space *s, int m);
// Deallocate data for space type.
        void free_space(Space *s);
        void free_axis(Space *s, int a);
        void free_motor(Space *s, int m);

        // Safe speed for probing, should result in 1 step per iteration.
        double probe_speed(Space *s);
}

#endif
