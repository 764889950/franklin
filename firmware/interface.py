# vim: set foldmethod=marker :

# Imports.  {{{
import serial
import time
import math
import struct
# }}}

class Printer: # {{{
	# Internal stuff.  {{{
	# Constants.  {{{
	# Serial timeout for normal communication in seconds.
	default_timeout = .1
	# Masks for computing checksums.
	mask = [	[0xc0, 0xc3, 0xff, 0x09],
			[0x38, 0x3a, 0x7e, 0x13],
			[0x26, 0xb5, 0xb9, 0x23],
			[0x95, 0x6c, 0xd5, 0x43],
			[0x4b, 0xdc, 0xe2, 0x83]]
	# Single-byte commands.
	single = { 'ACK': '\x80', 'NACK': '\xe1', 'ACKWAIT': '\xd2', 'STALL': '\xb3', 'RESET': '\xf4', 'INIT': '\x95', 'ACKRESET': '\xa6', 'UNUSED': '\xc7' }
	command = {
			'BEGIN': 0x00,
			'GOTO': 0x01,
			'GOTOCB': 0x02,
			'RUN': 0x03,
			'SLEEP': 0x04,
			'SETTEMP': 0x05,
			'WAITTEMP': 0x06,
			'READTEMP': 0x07,
			'LOAD': 0x08,
			'SAVE': 0x09,
			'READ': 0x0a,
			'WRITE': 0x0b,
			'PAUSE': 0x0c,
			'PING': 0x0d}
	rcommand = {
			'START': '\x0e',
			'TEMP': '\x0f',
			'DATA': '\x10',
			'PONG': '\x11',
			'MOVECB': '\x12',
			'TEMPCB': '\x13',
			'CONTINUE': '\x14',
			'LIMIT': '\x15'}
	# }}}
	def __init__ (self, port = '/dev/ttyACM0'): # {{{
		self.printer = serial.Serial (port, baudrate = 115200, timeout = .01)
		self.printer.readall ()	# flush buffer.
		self.printer.setTimeout (2)
		assert self.printer.read () == self.single['INIT']
		self.printer.setTimeout (Printer.default_timeout)
		self.ff_in = False
		self.ff_out = False
		self.limits = set ()
		self.wait = False
		self.movewait = 0
		self.tempwait = set ()
		self.num_extruders = 0
		self.begin ()
		self.bed = Printer.Temp ()
		self.bed_read (self.read (5))
		self.axis = []
		for a in range (3):
			self.axis.append (Printer.Axis ())
			self.axis[a].read (self.read (a))
		self.extruder = [Printer.Extruder () for t in range (10)]
		for e in range (self.num_extruders):
			self.extruder[e].read (self.read (6 + e))
	# }}}
	def make_packet (self, data): # {{{
		data = chr (len (data) + 1) + data
		for t in range ((len (data) + 2) / 3):
			check = t & 0x7
			for bit in range (5):
				sum = check & Printer.mask[bit][3]
				for byte in range (3):
					sum ^= ord (data[3 * t + byte]) & Printer.mask[bit][byte]
				sum ^= sum >> 4
				sum ^= sum >> 2
				sum ^= sum >> 1
				if sum & 1:
					check |= 1 << (bit + 3)
			data += chr (check)
		#print ' '.join (['%02x' % ord (x) for x in data])
		return data
	# }}}
	def parse_packet (self, data): # {{{
		#print ' '.join (['%02x' % ord (x) for x in data])
		if (ord (data[0]) + 2) / 3 != (len (data) + 3) / 4:
			return None
		length = ord (data[0])
		checksum = data[length:]
		for t in range (len (checksum)):
			r = data[3 * t:3 * t + 3] + checksum[t]
			if (ord (checksum[t]) & 0x7) != (t & 7):
				return None
			for bit in range (5):
				sum = 0
				for byte in range (4):
					sum ^= ord (r[byte]) & Printer.mask[bit][byte]
				sum ^= sum >> 4
				sum ^= sum >> 2
				sum ^= sum >> 1
				if (sum & 1) != 0:
					return None
		return data[1:length]
	# }}}
	def send_packet (self, data): # {{{
		if self.ff_out:
			data = chr (ord (data[0]) | 0x80) + data[1:]
		self.ff_out = not self.ff_out
		data = self.make_packet (data)
		events = 0
		ready = 0
		while True:
			print ('writing %s' % repr (data));
			self.printer.write (data)
			while True:
				r = self.printer.read (1)
				print ('read %s' % repr (r))
				if r == self.single['ACK']:
					return	# Done.
				if r == self.single['NACK']:
					break	# Break from this loop, so the packet is sent again.
				if r == self.single['ACKWAIT']:
					self.wait = True
					return	# Done.
				assert r != self.single['STALL']	# This must not happen and if it does, there's no way to handle it, so raise an exception.
				assert r != self.single['RESET']	# This should only be sent to the firmware, not to the host.
				assert r != self.single['INIT']		# This means the printer has reset; that shouldn't happen (but should be handled better; TODO).
				assert r != self.single['ACKRESET']	# This should only happen after we request it.
				assert r != self.single['UNUSED']	# This must not be used at all.
				if (ord (r) & 0x80) != 0:
					print ('writing %s' % repr (self.single['NACK']));
					self.printer.write (self.single['NACK'])
					continue
				# Regular packet received.  Must be asynchronous; handle it.
				print repr (r)
				reply = self.recv_packet (r, True)
				print repr (reply)
				assert reply == ''
	# }}}
	def recv_packet (self, buffer = '', want_any = False): # {{{
		while True:
			while len (buffer) < 1:
				r = self.printer.read (1)
				print ('read %s' % repr (r))
				if r == '':
					print ('writing %s' % repr (self.single['NACK']));
					self.printer.write (self.single['NACK'])
					continue
				assert r != self.single['INIT']	# This should be handled more gracefully.  TODO.
				assert (ord (r) & 0x80) == 0	# This must not happen.
				buffer += r
			length = ((ord (buffer[0]) + 2) / 3) * 4
			while len (buffer) < length:
				r = self.printer.read (length - len (buffer))
				print ('read %s' % repr (r))
				if r == '':
					print ('writing %s' % repr (self.single['NACK']));
					self.printer.write (self.single['NACK'])
					buffer = ''
					break	# Start over.
				buffer += r
			else:
				# Entire buffer has been received.
				data = self.parse_packet (buffer)
				if data is None:
					print ('writing %s' % repr (self.single['NACK']));
					self.printer.write (self.single['NACK'])
					buffer = ''
					continue	# Start over.
				if bool (ord (data[0]) & 0x80) != self.ff_in:
					# This was a retry of the previous packet; accept it and retry.
					print ('writing %s' % repr (self.single['ACK']));
					self.printer.write (self.single['ACK'])
					buffer = ''
					continue	# Start over.
				# Packet successfully received.
				# Clear the flip flop.
				data = chr (ord (data[0]) & ~0x80) + data[1:]
				# Flip it.
				self.ff_in = not self.ff_in
				# Handle the asynchronous events here; don't bother the caller with them.
				if data[0] == self.rcommand['MOVECB']:
					for i in range (ord (data[1])):
						assert self.movewait > 0
						self.movewait -= 1
					if want_any:
						return ''
					buffer = ''
					continue	# Start over.
				if data[0] == self.rcommand['TEMPCB']:
					assert ord (data[0]) in self.tempwait
					self.tempwait.remove (ord (data[0]))
					if want_any:
						return ''
					buffer = ''
					continue	# Start over.
				if data[0] == self.rcommand['CONTINUE']:
					self.wait = False
					if want_any:
						return ''
					buffer = ''
					continue	# Start over.
				if data[0] == self.rcommand['LIMIT']:
					self.limits.add (ord (data[1]))
					if want_any:
						return ''
					buffer = ''
					continue	# Start over.
				return data
	# }}}
	def block (self, timout = 30): # {{{
		self.printer.setTimeout (timeout)
		r = self.printer.read ()
		print ('read %s' % repr (r))
		assert r != ''
		self.printer.setTimeout (Printer.default_timeout)
		assert self.recv_packet (r, True) == ''
	# }}}
	# Config stuff.  {{{
	class Temp: # {{{
		def read (self, data):
			self.temp, self.T0, self.adc0, self.radiation, self.power, self.buffer_delay, self.power_pin, self.thermistor_pin = struct.unpack ('<fffffHBB', data[:24])
			return data[24:]
		def write (self):
			return struct.pack ('<fffffHBB', self.temp, self.T0, self.adc0, self.radiation, self.power, self.buffer_delay, self.power_pin, self.thermistor_pin)
	# }}}
	class Motor: # {{{
		def read (self, data):
			self.step_pin, self.dir_pin, self.sleep_pin, self.steps_per_mm = struct.unpack ('<BBBf', data[:7])
			return data[7:]
		def write (self):
			return struct.pack ('<BBBf', self.step_pin, self.dir_pin, self.sleep_pin, self.steps_per_mm)
	# }}}
	class Axis: # {{{
		def __init__ (self):
			self.motor = Printer.Motor ()
		def read (self, data):
			data = self.motor.read (data)
			self.limit_min_pin, self.limit_max_pin = struct.unpack ('<BB', data)
		def write (self):
			return self.motor.write () + struct.pack ('<BB', self.limit_min_pin, self.limit_max_pin)
	# }}}
	class Extruder: # {{{
		def __init__ (self):
			self.motor = Printer.Motor ()
			self.temp = Printer.Temp ()
		def read (self, data):
			data = self.motor.read (data)
			data = self.temp.read (data)
			self.filament_heat, self.nozzle_size, self.filament_size = struct.unpack ('<fff', data)
		def write (self):
			return self.motor.write () + self.temp.write () + struct.pack ('<fff', self.filament_heat, self.nozzle_size, self.filament_size)
	# }}}
	def bed_read (self, data): # {{{
		self.num_extruders = struct.unpack ('<H', data[:2])[0]
		assert self.bed.read (data[2:]) == ''
	# }}}
	def bed_write (self): # {{{
		return struct.pack ('<H', self.num_extruders) + self.bed.write ()
	# }}}
	# }}}
	# Internal commands.  {{{
	def begin (self): # {{{
		self.send_packet (struct.pack ('<Bf', self.command['BEGIN'], 0))
		assert struct.unpack ('<Bf', self.recv_packet ()) == (ord (self.rcommand['START']), 0.0)
	# }}}
	def read (self, channel): # {{{
		self.send_packet (struct.pack ('<BB', self.command['READ'], channel))
		p = self.recv_packet ()
		assert p[0] == self.rcommand['DATA']
		return p[1:]
	# }}}
	# }}}
	# }}}
	# Useful commands.  {{{
	def goto (self, x = None, y = None, z = None, e = None, f0 = None, f1 = None, which = 0, cb = False): # {{{
		while self.wait:
			self.block ()
		if cb:
			self.movewait += 1
			p = chr (self.command['GOTOCB'])
		else:
			p = chr (self.command['GOTO'])
		targets = [0] * (((6 + self.num_extruders - 1) >> 3) + 1)
		args = ''
		if x is not None:
			targets[0] |= 1 << 0
			args += struct.pack ('<f', x)
		if y is not None:
			targets[0] |= 1 << 1
			args += struct.pack ('<f', y)
		if z is not None:
			targets[0] |= 1 << 2
			args += struct.pack ('<f', z)
		if f0 is not None:
			targets[0] |= 1 << 3
			args += struct.pack ('<f', f0)
		if f1 is not None:
			targets[0] |= 1 << 4
			args += struct.pack ('<f', f1)
		if e is not None:
			targets[0] |= 1 << (6 + which)
			args += struct.pack ('<f', e)
		self.send_packet (p + ''.join ([chr (t) for t in targets]) + args)
	# }}}
	def run (self, channel, speed):	# speed: float; 0 means off. # {{{
		self.send_packet (struct.pack ('<BBf', self.command['RUN'], channel, speed))
	# }}}
	def sleep (self, channel, sleeping): # {{{
		self.send_packet (struct.pack ('<BB', self.command['SLEEP'], (channel & 0x7f) | (0x80 if sleeping else 0)))
	# }}}
	def settemp (self, channel, temp): # {{{
		self.send_packet (struct.pack ('<BBf', self.command['SETTEMP'], channel, temp))
	# }}}
	def waittemp (self, channel, min, max): # {{{
		if min is None:
			min = float ('nan')
		if max is None:
			max = float ('nan')
		self.send_packet (struct.pack ('<BBff', self.command['WAITTEMP'], channel, min, max))
		if math.isnan (min) and math.isnan (max):
			self.tempwait.remove (channel)
		else:
			self.tempwait.add (channel)
	# }}}
	def readtemp (self, channel): # {{{
		self.send_packet (struct.pack ('<BB', self.command['READTEMP'], channel))
		return struct.unpack ('<f', self.recv_packet ())[0]
	# }}}
	def load (self, channel): # {{{
		self.send_packet (struct.pack ('<BB', self.command['LOAD'], channel))
		if channel < 3:
			self.axis[channel].read (self.read (channel))
		elif channel == 5:
			self.bed_read (self.read (channel))
		else:
			assert 6 <= channel < self.num_extruders
			self.extruder[channel - 6].read (self.read (channel))
	# }}}
	def save (self, channel): # {{{
		self.send_packet (struct.pack ('<BB', self.command['SAVE'], channel))
	# }}}
	def write (self, channel): # {{{
		if channel < 3:
			data = self.axis[channel].write ()
		elif channel == 5:
			data = self.bed_write ()
		else:
			assert 6 <= channel < self.num_extruders
			data = self.extruder[channel - 6].write ()
		self.send_packet (struct.pack ('<BB', self.command['WRITE'], channel) + data)
	# }}}
	def pause (self, pausing = True): # {{{
		self.send_packet (struct.pack ('<BB', self.command['PAUSE'], pausing))
	# }}}
	def ping (self, arg = 0): # {{{
		self.send_packet (struct.pack ('<BB', self.command['PING'], arg))
		assert struct.unpack ('<BB', self.recv_packet ()) == (self.command['PONG'], arg)
	# }}}
	# }}}
	# Presets.  {{{
	def set_ramps_pins (self): # {{{
		self.axis[0].limit_min_pin = 3
		self.axis[0].limit_max_pin = 2
		self.axis[0].motor.step_pin = 54
		self.axis[0].motor.dir_pin = 55
		self.axis[0].motor.sleep_pin = 38
		self.axis[1].limit_min_pin = 14
		self.axis[1].limit_max_pin = 15
		self.axis[1].motor.step_pin = 60
		self.axis[1].motor.dir_pin = 61
		self.axis[1].motor.sleep_pin = 56
		self.axis[2].limit_min_pin = 18
		self.axis[2].limit_max_pin = 19
		self.axis[2].motor.step_pin = 46
		self.axis[2].motor.dir_pin = 48
		self.axis[2].motor.sleep_pin = 62
		self.bed.power_pin = 8
		self.bed.thermistor_pin = 14
		self.extruder[0].temp.power_pin = 10
		self.extruder[0].temp.thermistor_pin = 13
		self.extruder[0].motor.step_pin = 26
		self.extruder[0].motor.dir_pin = 28
		self.extruder[0].motor.sleep_pin = 24
		self.extruder[1].temp.power_pin = 9
		self.extruder[1].temp.thermistor_pin = 15
		self.extruder[1].motor.step_pin = 36
		self.extruder[1].motor.dir_pin = 34
		self.extruder[1].motor.sleep_pin = 30
		for e in range (2, 10):
			self.extruder[e].temp.power_pin = 255
			self.extruder[e].temp.thermistor_pin = 255
			self.extruder[e].motor.step_pin = 255
			self.extruder[e].motor.dir_pin = 255
			self.extruder[e].motor.sleep_pin = 255
	# }}}
	def set_melzi_pins (self): # {{{
		self.axis[0].limit_min_pin = 18
		self.axis[0].limit_max_pin = 255
		self.axis[0].motor.step_pin = 15
		self.axis[0].motor.dir_pin = 21
		self.axis[0].motor.sleep_pin = 14
		self.axis[1].limit_min_pin = 19
		self.axis[1].limit_max_pin = 255
		self.axis[1].motor.step_pin = 22
		self.axis[1].motor.dir_pin = 23
		self.axis[1].motor.sleep_pin = 14
		self.axis[2].limit_min_pin = 20
		self.axis[2].limit_max_pin = 255
		self.axis[2].motor.step_pin = 3
		self.axis[2].motor.dir_pin = 2
		self.axis[2].motor.sleep_pin = 26
		self.bed.power_pin = 10
		self.bed.thermistor_pin = 6
		self.extruder[0].temp.power_pin = 13
		self.extruder[0].temp.thermistor_pin = 7
		self.extruder[0].motor.step_pin = 1
		self.extruder[0].motor.dir_pin = 0
		self.extruder[0].motor.sleep_pin = 14
		for e in range (1, 10):
			self.extruder[e].temp.power_pin = 255
			self.extruder[e].temp.thermistor_pin = 255
			self.extruder[e].motor.step_pin = 255
			self.extruder[e].motor.dir_pin = 255
			self.extruder[e].motor.sleep_pin = 255
	# }}}
	# }}}
# }}}

p = Printer ()
