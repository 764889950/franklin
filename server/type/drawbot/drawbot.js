var TYPE_DRAWBOT = 'drawbot';

function drawbot_get_value(ui, id) { // {{{
	return ui.machine.spaces[id[0][1][0]].motor[id[0][1][1]]['drawbot_' + id[1]];
} // }}}

function drawbot_set_value(ui, id, value, reply) { // {{{
	// [['module', [0, 1, 'motor'], 'drawbot'], 'x']
	var o = {type: 'drawbot'};
	o[id[1]] = value;
	ui.machine.call('set_motor', [[id[0][1][0], id[0][1][1]]], {module: o}, reply);
} // }}}

function drawbot_update(ui, index) { // {{{
	for (var m = 0; m < ui.machine.spaces[0].motor.length; ++m) {
		update_float(ui, [['module', [index, m, 'motor'], 'drawbot'], 'x']);
		update_float(ui, [['module', [index, m, 'motor'], 'drawbot'], 'y']);
	}
} // }}}

function drawbot_draw(ui, context) { // {{{
	var motor = ui.machine.spaces[0].motor;
	var pos = [[motor[0].drawbot_x, motor[0].drawbot_y], [motor[1].drawbot_x, motor[1].drawbot_y]];
	var dx = pos[1][0] - pos[0][0];
	var dy = pos[1][1] - pos[0][1];
	var len = [motor[0].home_pos, motor[1].home_pos, Math.sqrt(dx * dx + dy * dy)];
	var Pu = pos[0][1] - Math.sqrt(len[0] * len[0] - dx * dx);
	var Pv = pos[1][1] - Math.sqrt(len[1] * len[1] - dx * dx);
	var end_u_inner = Math.acos((len[0] * len[0] + len[2] * len[2] - len[1] * len[1]) / (2 * len[0] * len[2]));
	var end_u = end_u_inner - Math.atan(dy / dx);
	var start_v_inner = Math.asin(Math.sin(end_u_inner) * len[0] / len[1]);
	var start_v = Math.atan(dx / dy) - start_v_inner + Math.PI / 2;
	var center = [(pos[0][0] + pos[1][0]) / 2, (pos[0][1] - len[0] / 2 + pos[1][1] - len[1] / 2) / 2];
	var outline = function(ui, c) {
		c.beginPath();
		c.moveTo(pos[0][0], Pu);
		c.lineTo(pos[0][0], pos[0][1]);
		c.lineTo(pos[1][0], pos[1][1]);
		c.lineTo(pos[1][0], Pv);
		c.arc(pos[0][0], pos[0][1], len[0], -Math.acos(dx / len[0]), -end_u, true);
		c.arc(pos[1][0], pos[1][1], len[1], -start_v, Math.acos(dx / len[1]) - Math.PI, true);
		c.stroke();
	};
	machinewidth = pos[1][0] - pos[0][0];
	machineheight = Math.max(len[0], len[1]) * 1.4;
	return [machinewidth, machineheight, center, outline];
} // }}}

function drawbot_mload(machine, s, m, data) { // {{{
	machines[machine].spaces[s].motor[m].drawbot_x = data.x;
	machines[machine].spaces[s].motor[m].drawbot_y = data.y;
} // }}}

// UI modules. {{{
function Drawbot(ui, space, motor) { // {{{
	if (space != 0)
		return null;
	var e = [['x', 1], ['y', 1]];
	for (var i = 0; i < e.length; ++i) {
		var div = Create('div');
		div.Add(Float(ui, [['module', [space, motor, 'motor'], 'drawbot'], e[i][0]], e[i][1], 1));
		e[i] = div;
	}
	return make_tablerow(ui, motor_name(ui, space, motor), e, ['rowtitle2'], undefined, TYPE_DRAWBOT, space);
} // }}}

function setup_drawbot(desc, pos, top) { // {{{
	var ui = top.data;
	var ret = Create('div', 'setup expert');
	ret.update = function() { this.hide(ui.machine.spaces[0].type != TYPE_DRAWBOT); };
	ret.Add([make_table(ui).AddMultipleTitles([
		'Drawbot',
		UnitTitle(ui, 'X'),
		UnitTitle(ui, 'Y')
	], [
		'htitle2',
		'title2',
		'title2'
	], [
		null,
		'X coordinate of motor',
		'Y coordinate of motor'
	]).AddMultiple(ui, 'motor', Drawbot)]);
	return [ret, pos];
} // }}}
// }}}

AddEvent('setup', function () {
	space_types[TYPE_DRAWBOT] = 'Drawbot';
	type_info[TYPE_DRAWBOT] = {
		name: 'Drawbot',
		get_value: drawbot_get_value,
		set_value: drawbot_set_value,
		update: drawbot_update,
		draw: drawbot_draw,
		mload: drawbot_mload
	};
	ui_modules['Drawbot Setup'] = setup_drawbot;
});

// TODO: set_value id parsing
// vim: set foldmethod=marker :