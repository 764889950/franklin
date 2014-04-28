// vim: set foldmethod=marker :
#include "firmware.h"

//#define DEBUG_MOVE

void reset_pos ()	// {{{
{
#ifdef DEBUG_MOVE
	debug ("Recalculating current position");
#endif
	// Determine current location of extruder.
	if (printer_type == 0) { // {{{
		for (uint8_t a = 0; a < MAXAXES; ++a)
			axis[a].source = axis[a].current_pos == MAXLONG ? NAN : axis[a].current_pos / axis[a].motor.steps_per_mm;
	}
	// }}}
	else if (printer_type == 1) { // {{{
		//debug ("new delta position");
		// All axes' current_pos must be valid and equal, in other words, that x=y=0.
		if (axis[0].current_pos == MAXLONG || axis[1].current_pos == MAXLONG || axis[2].current_pos == MAXLONG || axis[0].current_pos != axis[1].current_pos || axis[0].current_pos != axis[2].current_pos) {
#ifdef DEBUG_MOVE
			debug ("Refusing to set new delta position from current_pos = (%d, %d, %d)", int (axis[0].current_pos), int (axis[1].current_pos), int (axis[2].current_pos));
#endif
			axis[0].source = NAN;
			axis[1].source = NAN;
			axis[2].source = NAN;
		}
		else {
			axis[0].source = 0;
			axis[1].source = 0;
			axis[2].source = axis[0].current_pos / axis[0].motor.steps_per_mm;
		}
		for (uint8_t a = 3; a < MAXAXES; ++a)
			axis[a].source = axis[a].current_pos == MAXLONG ? NAN : axis[a].current_pos / axis[a].motor.steps_per_mm;
	}
	// }}}
	else
		debug ("Error: invalid printer type %d", printer_type);
	for (uint8_t a = 0; a < MAXAXES; ++a) {
		axis[a].current = axis[a].source;
#ifdef DEBUG_MOVE
		debug ("New position for axis %d = %f", a, &axis[a].source);
#endif
	}
}
// }}}

static void apply_offsets (float *data) {
	// Apply offsets.
	for (uint8_t a = 0; a < num_axes; ++a) {
		data[AXIS0 + a] += axis[a].offset;
		if (!isnan (axis[a].axis_max) && data[AXIS0 + a] > axis[a].axis_max)
			data[AXIS0 + a] = axis[a].axis_max;
		if (!isnan (axis[a].axis_min) && data[AXIS0 + a] < axis[a].axis_min)
			data[AXIS0 + a] = axis[a].axis_min;
	}
}

// Set up:
// start_time		micros() at start of move.
// t0			time to do main part.
// tp			time to do connection.
// tq			time to do startup.  This cannot be smaller than tp.
// m->dist		total distance of this segment (mm).
// m->next_dist		total distance of next segment (mm).
// m->main_dist		distance of main part (mm).
// m->steps_done	(extruder only): steps of this segment that have been done in preparation.
// v0, vp		start and end velocity for main part. (fraction/s)
// vq			end velocity of connector part. (fraction/s)

// Used from previous segment (if move_prepared == true): tp, tq, vq.
void next_move () {
	if (queue_start == queue_end)
		return;
#ifdef DEBUG_MOVE
	debug ("Next move; queue start = %d, end = %d", queue_start, queue_end);
#endif
	// Set everything up for running queue[queue_start].

	// Make sure printer state is good. {{{
	// If the source is unknown, determine it from current_pos.
	for (uint8_t a = 0; a < num_axes; ++a) {
		if (isnan (axis[a].source)) {
			reset_pos ();
			break;
		}
	}

	// For a delta, if any axis moves, all motors move; avoid trouble by setting none of them to nan.
	if (printer_type == 1 && (!isnan (queue[queue_start].data[AXIS0]) || !isnan (queue[queue_start].data[AXIS0 + 1]) || !isnan (queue[queue_start].data[AXIS0 + 2]))) {
		for (uint8_t a = 0; a < 3; ++a) {
			if (isnan (queue[queue_start].data[AXIS0 + a])) {
				// Offset will be added later; compensate for it.
#ifdef DEBUG_MOVE
				debug ("Filling delta axis %d", a);
#endif
				queue[queue_start].data[AXIS0 + a] = axis[a].source - axis[a].offset;
			}
		}
	}
	for (uint8_t a = 0; a < num_axes; ++a) {
		if (!isnan (queue[queue_start].data[AXIS0 + a]) && isnan (axis[a].source)) {
			debug ("Motor positions are not known, so move cannot take place; aborting move and flushing the queue.");
			abort_move ();
			flush_queue ();
			return;
		}
	}
	// }}}

	v0 = vq;
	f0 = v0 * tp / 2e6;
	// If tp was too short, the motors aren't up to speed yet; insert a fake segment to get there.
	if (move_prepared && tq > tp) { // {{{
#ifdef DEBUG_MOVE
		debug ("Last move wasn't good enough, building rest of preparation.");
#endif
		v0 = 0;
		vp = 0;
		t0 = 0;
		for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
			uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
			if (!motors[m])
				continue;
			motors[m]->dist = 0;
			motors[m]->main_dist = 0;
			if (mt >= num_axes)
				extruder[mt - num_axes].steps_done = f0 * motors[m]->next_dist;
		}
		current_move_has_cb = false;
#ifdef DEBUG_MOVE
		debug ("Extra segment has been set up: f0=%f v0=%f /s vp=%f /s vq=%f /s t0=%d ms tp=%d ms tq=%d ms", &f0, &v0, &vp, &vq, int (t0/1000), int (tp/1000), int (tq/1000));
#endif
		moving = true;
		start_time = micros () - tp;
		tp = tq;
		return;
	} // }}}
	float max_ap = INFINITY, max_aq = INFINITY, max_vp = INFINITY, max_vq = INFINITY; // [fraction/s²], [fraction/s]
	// If no move is prepared, do a fake segment with only the speed change at the end; don't pop anything off the queue.
	if (!move_prepared) { // {{{
#ifdef DEBUG_MOVE
		debug ("No move prepared, building preparation segment.");
#endif
		apply_offsets (queue[queue_start].data);

		// For requested motors, set dist to 0 and next_dist to correct value.
		bool action = false;
		for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
			uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
			if (!motors[m])
				continue;
			motors[m]->dist = 0;
			if (isnan (queue[queue_start].data[mt + 2])) {
				motors[m]->next_dist = 0;
				continue;
			}
			if (mt < num_axes)
				motors[m]->next_dist = queue[queue_start].data[mt + 2] - axis[mt].source;
			else
				motors[m]->next_dist = queue[queue_start].data[mt + 2];
			// For a delta, this multiplication isn't really allowed, but we're only looking at orders of magnitude, so it's ok.
			if (abs (motors[m]->next_dist) * motors[m]->steps_per_mm > .1)
				action = true;
#ifdef DEBUG_MOVE
			debug ("Preparing motor %d for a dist of %f", m, &motors[m]->next_dist);
#endif
		}
		// If no motors are moving for this segment, discard it and try the next.
		if (!action) {
#ifdef DEBUG_MOVE
			debug ("Skipping zero-distance move (from preparation)");
#endif
			if (((queue_end + 1) % QUEUE_LENGTH) == queue_start)
				continue_cb |= 1;
			if (queue[queue_start].cb) {
#ifdef DEBUG_MOVE
				debug ("Sending movecb for skipped move.");
#endif
				++num_movecbs;
			}
			queue_start = (queue_start + 1) % QUEUE_LENGTH;
			try_send_next ();
			moving = false;
			for (uint8_t m = 0; m < MAXOBJECT; ++m) {
				if (!motors[m])
					continue;
				motors[m]->dist = NAN;
			}
			return next_move ();
		}
		tq = 0;	// Previous value; new value is computed below.
		v0 = 0;
		vp = 0;
		f0 = 0;
		vq = queue[queue_start].data[F0] * feedrate;
		current_move_has_cb = false;
		move_prepared = true;
	}
	// }}}
	// Otherwise, we are prepared and can start the segment. {{{
	else {
		bool action = false;
		v0 = vq;
#ifdef DEBUG_MOVE
		debug ("Move was prepared with v0 = %f and tq = %d", &v0, int (tq / 1000));
#endif
		uint8_t n = (queue_start + 1) % QUEUE_LENGTH;
		if (n == queue_end) { // {{{
			// There is no next segment; we should stop at the end.
#ifdef DEBUG_MOVE
			debug ("Building final segment.");
#endif
			for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
				uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
				if (!motors[m])
					continue;
				motors[m]->dist = motors[m]->next_dist;
				// For a delta, this multiplication isn't really allowed, but we're only looking at orders of magnitude, so it's ok.
				if (abs (motors[m]->dist) * motors[m]->steps_per_mm > .001) {
					motors[m]->next_dist = 0;
					action = true;
				}
				else {
					motors[m]->next_dist = 0;
					motors[m]->dist = 0;
				}
#ifdef DEBUG_MOVE
				debug ("Last segment distance for motor %d is %f", m, &motors[m]->dist);
#endif
			}
			vp = queue[queue_start].data[F1] * feedrate;
			vq = 0;
			move_prepared = false;
		}
		// }}}
		else { // {{{
			// There is a next segment; we should connect to it.
#ifdef DEBUG_MOVE
			debug ("Building a connecting segment.");
#endif
			// Apply offsets to next segment.
			apply_offsets (queue[n].data);
			for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
				uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
				if (!motors[m])
					continue;
				motors[m]->dist = motors[m]->next_dist;
				if (isnan (queue[n].data[mt + 2]))
					motors[m]->next_dist = 0;
				else
					motors[m]->next_dist = mt < num_axes ? queue[n].data[mt + 2] - (axis[mt].source + motors[m]->dist) : queue[n].data[mt + 2];
				// For a delta, this multiplication isn't really allowed, but we're only looking at orders of magnitude, so it's ok.
				if (abs (motors[m]->next_dist) * motors[m]->steps_per_mm > .001 || abs (motors[m]->dist) * motors[m]->steps_per_mm > .001)
					action = true;
#ifdef DEBUG_MOVE
				debug ("Connecting distance for motor %d is %f, to %f", m, &motors[m]->dist, &motors[m]->next_dist);
#endif
			}
			vp = queue[queue_start].data[F1] * feedrate;
			vq = queue[n].data[F0] * feedrate;
			move_prepared = true;
		}
		// }}}
		current_move_has_cb = queue[queue_start].cb;
		if (((queue_end + 1) % QUEUE_LENGTH) == queue_start) {
			continue_cb |= 1;
			try_send_next ();
		}
		queue_start = n;
		if (!action) {
#ifdef DEBUG_MOVE
			debug ("Skipping zero-distance prepared move");
#endif
			if (current_move_has_cb) {
				++num_movecbs;
				try_send_next ();
			}
			moving = false;
			for (uint8_t m = 0; m < MAXOBJECT; ++m) {
				if (!motors[m])
					continue;
				motors[m]->dist = NAN;
			}
			return next_move ();
		}
	} // }}}

	// Currently set up:
	// f0: fraction of move already done by connection.
	// v0: previous move's final speed.
	// vp: this move's requested ending speed.
	// vq: next move's requested starting speed.
	// current_move_has_cb: if a cb should be fired after this segment is complete.
	// move_prepared: if this segment connects to a following segment.
#ifdef DEBUG_MOVE
	debug ("Set up: tq = %d ms, v0 = %f /s, vp = %f /s, vq = %f /s", int (tq / 1000), &v0, &vp, &vq);
#endif

	// Limit vp and vq. {{{
	for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
		uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
		if (!motors[m])
			continue;
		// TODO: limit motors, not axes, for delta.
		float max = motors[m]->dist >= 0 ? motors[m]->max_v_pos : motors[m]->max_v_neg;
		float v = max / abs (motors[m]->dist);
		//debug ("Motor %d, dist = %f, max = %f mm/s, v = %f", m, &motors[m]->dist, &max, &v);
		if (!isinf (v) && max_vp > v)
			max_vp = v;
		//debug ("max_vp = %f /s", &max_vp);
		max = motors[m]->next_dist >= 0 ? motors[m]->max_v_pos : motors[m]->max_v_neg;
		v = max / abs (motors[m]->next_dist);
		//debug ("Motor %d, dist = %f, max = %f mm/s, v = %f", m, &motors[m]->next_dist, &max, &v);
		if (!isinf (v) && max_vq > v) {
			//debug ("changing max_vq from %f to %f /s", &max_vq, &v);
			max_vq = v;
		}
		//debug ("max_vq = %f /s", &max_vq);
		v = motors[m]->max_a / abs (motors[m]->dist);
		if (!isinf (v) && max_ap > v)
			max_ap = v;
		v = motors[m]->max_a / abs (motors[m]->next_dist);
		if (!isinf (v) && max_aq > v)
			max_aq = v;
	}
	if (vp > max_vp)
		vp = max_vp;
	if (vq > max_vq)
		vq = max_vq;
#ifdef DEBUG_MOVE
	debug ("vq = %f /s", &vq);
	debug ("After limiting, vp = %f /s and vq = %f /s", &vp, &vq);
#endif
	// }}}
	// Already set up: v0, vp, vq, max_vp, max_vq, max_ap, max_aq, m->dist, m->next_dist.
	// To do: start_time, t0, tp, tq, m->main_dist, m->steps_done
#ifdef DEBUG_MOVE
	debug ("Preparation did f0 = %f", &f0);
#endif
	// Find shortest tp. {{{
	if (isinf (vp)) {
		vp = 0;
		tp = 0;
		t0 = 0;
	}
	else {
		t0 = long (abs (vp - v0) / max_ap * 1e6);
		tp = long (vp / max_ap * 1e6);
	}
#ifdef DEBUG_MOVE
	debug ("Required time for acceleration in main part: %d, in connector: %d.", int (t0 / 1000), int (tp / 1000));
#endif
	// }}}
	// Check if we're within the segment's limits. {{{ XXX
	float fp = vp / 2e6 * tp;
	float fmain = (v0 + vp) * t0 / 2e6;	// This is the minimum fraction for the main part.
	if (fmain + fp > 1 - f0) {
		// This move is too short for the requested vp.  Don't go too far.
		if (!isinf (max_ap)) {
			t0 = long ((sqrt ((1 - f0 + v0 * v0 / 2 / max_ap) / max_ap) - v0 / max_ap) * 1e6);
			tp = t0 + long (v0 / max_ap * 1e6);
			vp = v0 + max_ap * t0 / 1e6;
		}
		else {
			tp = 0;
			vp = v0;	// Strictly speaking, this should be max_vp, but that should be equal to max_vp, so don't bother with complexity.
			t0 = long ((1 - f0) / v0 * 1e6);
		}
	}
	else if (v0 > 1e-10 || vp > 1e-10) {
		// The current value of t0 is the minimum value; set the actual value.
		t0 = long ((1 - fp - f0) / (v0 + vp) * 2e6);
#ifdef DEBUG_MOVE
		debug ("This move had an initial v of %f, which is sustained %d ms.", &v0, int (t0 / 1000));
#endif
	}
	else
		t0 = 0;
	// }}}
	// Set up q. {{{
	tq = long (vq / max_aq * 1e6);
	if (tq < tp)
		tq = tp;
	if (isinf (vq)) {
		tq = 0;
		vq = 0;
	}
	float fq = vq * tq / 2e6;
	if (fq > .5) {
		// This segment is too short for this speed; slow it down.
		fq = .5;
		tq = long (sqrt (fq / max_aq) * 1e6);
		if (tq < tp) {
			// Now it doesn't take long enough; slow it down.
			tq = tp;
			vq = 1e6 / tq;
		}
		else
			vq = tq * max_aq / 1e6;
	} // }}}
	// If this is a preparation segment, give it time to actually do the preparation.
	if (tp == 0 && t0 == 0)
		tp = tq;
	// Finish. {{{
	for (uint8_t mt = 0; mt < num_axes + num_extruders; ++mt) {
		uint8_t m = mt < num_axes ? mt + 2 : mt + 2 + MAXAXES - num_axes;
		if (!motors[m])
			continue;
		if (motors[m]->dist != 0 || motors[m]->next_dist != 0) {
			SET (motors[m]->enable_pin);
			motors_busy |= 1 << m;
			/*if (mt < num_axes)
				debug ("Move motor %f from %f (really %f) over %f steps (f0=%f)", m, &axis[mt].source, &axis[mt].current, &motors[m]->dist, &f0);*/
		}
		motors[m]->main_dist = motors[m]->dist * (f0 + (v0 + vp) * t0 / 2e6);
#ifdef DEBUG_MOVE
		debug ("Motor %d dist %f main dist = %f", m, &motors[m]->dist, &motors[m]->main_dist);
#endif
		if (mt >= num_axes)
			extruder[mt - num_axes].steps_done = f0 * motors[m]->dist * motors[m]->steps_per_mm;
	}
#ifdef DEBUG_MOVE
	debug ("Segment has been set up: f0=%f fp=%f fq=%f v0=%f /s vp=%f /s vq=%f /s t0=%d ms tp=%d ms tq=%d ms", &f0, &fp, &fq, &v0, &vp, &vq, int (t0/1000), int (tp/1000), int (tq/1000));
#endif
	moving = true;
	start_time = micros ();
	// }}}
}

void abort_move () { // {{{
	//debug ("try aborting move");
	if (moving) {
		debug ("aborting move");
		if (current_move_has_cb) {
			debug ("movecb 3");
			++num_movecbs;
		}
		for (uint8_t a = 0; a < MAXAXES; ++a)
			axis[a].source = axis[a].current;
		for (uint8_t m = 0; m < MAXOBJECT; ++m) {
			if (!motors[m])
				continue;
			motors[m]->continuous_steps_per_s = 0;
			motors[m]->f = 0;
			motors[m]->dist = NAN;
		}
		moving = false;
		move_prepared = false;
#ifdef DEBUG_MOVE
		debug ("move no longer prepared");
#endif
		if (t0 == 0 && queue_end != queue_start) {
			// This was probably the preparation for a move; remove the move itself from the queue.
			// If it wasn't, doing this doesn't really harm either.
			if (((queue_end + 1) % QUEUE_LENGTH) == queue_start) {
				continue_cb |= 1;
				try_send_next ();
			}
			if (queue[queue_start].cb) {
				debug ("movecb 4");
				++num_movecbs;
				try_send_next ();
			}
			queue_start = (queue_start + 1) % QUEUE_LENGTH;
		}
	}
} // }}}

void flush_queue () { // {{{
	pause_all = false;
	if (((queue_end + 1) % QUEUE_LENGTH) == queue_start) {
		continue_cb |= 1;
		try_send_next ();
	}
	while (queue_start != queue_end) {
		if (queue[queue_start].cb) {
			debug ("movecb 4");
			++num_movecbs;
			try_send_next ();
		}
		queue_start = (queue_start + 1) % QUEUE_LENGTH;
	}
} // }}}
