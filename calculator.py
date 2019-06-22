import serial
import sys, time


class Message(object):
    def __init__(self, msgid, payload, reserved=0xFF):
        self.msgid = msgid
        self.payload = payload
        self.reserved = reserved

    def serialize(self):
        res = [self.msgid, 0xFF - len(self.payload) - 5, self.reserved] + self.payload
        return res + compute_checksum(res)

    def __str__(self):
        r = "ID: %02X, payload: [" % self.msgid
        r += ''.join('{:02X} '.format(x) for x in self.payload) + "]"
        return r

class MoveUpMessage(Message):
    def __init__(self, address):
        Message.__init__(self, 0xFC, [0x80, 0x80, 0x80] + compute_address(address) + [0xFE, 0xFF, 0xFF, 0xFF])

class MoveDownMessage(Message):
    def __init__(self, address):
        Message.__init__(self, 0xFC, [0x80, 0x80, 0x80] + compute_address(address) + [0xFF, 0xFF, 0xFF, 0xFF])

class MoveToPercent(Message):
    def __init__(self, address, percent):
        pos = 0xFF - percent
        Message.__init__(self, 0xFC, [0x80, 0x80, 0x80] + compute_address(address) + [0xFB, pos, 0xFF, 0xFF])

class DiscoverAll(Message):
    def __init__(self):
        Message.__init__(self, 0xBF, [0x80,0x80,0x80, 0,0,0])

class HereIsMotor(Message):
    MSG_ID = 0x9F

    def get_motor_address(self):
        return compute_address(self.payload[1:4])

    def __str__(self):
        return "HereIsMotor: %s" % print_address(self.get_motor_address())

class HereIsPosition(Message):
    MSG_ID = 0xF2

    def get_motor_address(self):
        return compute_address(self.payload[1:4])

    def get_percentage(self):
        return 0xFF - self.payload[9]

    def get_ticks(self):
        return self.payload[7] + self.payload[8]*256

    def __str__(self):
        return "HereIsPosition: %s, %d%%, %d ticks" % (print_address(self.get_motor_address()), 
            self.get_percentage(), self.get_ticks())

def compute_checksum(msg):
    checksum = 0
    for i in msg:
        checksum += i
    return [checksum//256, checksum%256]

def compute_address(a):
    res = [a[2], a[1], a[0]]
    return [~f for f in res]

def print_address(a):
    return "%02X%02X%02X" % (a[0] & 0xFF, a[1] & 0xFF, a[2] & 0xFF)

class Timeout(Exception):
    pass

def do_read(stream):
    res = stream.read(1)
    if len(res) == 0:
        raise Timeout()
    return res[0]

def read_msg(stream):
    msg_id = do_read(stream)
    ln = 0xFF - do_read(stream)
    payload = []
    for i in range(0, ln-4):
        payload += [do_read(stream)]

    checksum = [do_read(stream), do_read(stream)]
    computed = compute_checksum([msg_id, 0xFF-ln] + payload)
    if checksum != computed:
        print("Bad checksum")
        return None

    if msg_id == 0x9F:
        return HereIsMotor(msg_id, payload)

    if msg_id == 0xF2:
        return HereIsPosition(msg_id, payload)

    return Message(msg_id, payload)

DISCOVER_ALL = Message(0xBF, [0x80, 0x80, 0x80, 00, 00, 00])

moveUp = True

ser = serial.Serial('/dev/tty.usbserial-AC01QL0Z', 4800, timeout=1,
                    parity=serial.PARITY_ODD)  # open serial port
print("Discovering devices")

ser.write(DISCOVER_ALL.serialize())          # write a string

startTime = time.time()
devices = []
while time.time() - startTime < 2:
    try:
        m = read_msg(ser)
        if m is None:
            continue
        devices += [m.get_motor_address()]
        print(m)
    except Timeout:
        pass

if not devices:
    print("No devices found")
    sys.exit(0)

print("Found devices: %d count" % len(devices))
print("Sending command, moveUp is %s" % moveUp)

# Down
#arr = [0xFC, 0xF0, 0xFF, 0x80, 0x80, 0x80, 0x5F, 0xC0, 0xEC, 0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x72]
# Up
#arr = [0xFC, 0xF0, 0xFF, 0x80, 0x80, 0x80, 0x5F, 0xC0, 0xEC, 0xFE, 0xFF, 0xFF, 0xFF, 0x0A, 0x71]
# 50% FB, CD, FF, FF
# 30% FB, E1, FF, FF


devices.sort()
#d = devices[1]

#m = MoveDownMessage(d)
#ser.write(m.serialize())
#ser.flush()
#time.sleep(0.1)
#read_msg(ser)

#while True:
#    m = Message(0xDE, [0x80, 0x80, 0x80] + compute_address(d))
#    m = Message(0xF3, [0x80, 0x80, 0x80] + compute_address(d))
#    ser.write(m.serialize())
#    ser.flush()
#    time.sleep(0.1)
#    m2 = read_msg(ser)
#    print(m2)
#    time.sleep(0.5)
#
#exit(0)

for d in devices:
#    m = Message(0xAB, [0x80, 0xFF, 0xFF] + compute_address(d) + [0xFD, 0xFF, 0xFF])
# Up: 0x7f,0xf2,0xfa,0x01,0x00,0x00,0x49,0x55,0xfa,0xfb,0xfe,0x05,0xfd
# Down: 0x7f,0xf2,0xfa,0x01,0x00,0x00,0x49,0x55,0xfa,0xfb,0xfd,0x05,0xfc
#    m = Message(0x7F, [0x01, 0x00, 0x00] + compute_address(d)+[0xfe, 0xfe], 0xFA)
#    print(m.serialize())
#    ser.write(m.serialize())
#    ser.flush()
#    time.sleep(0.1)
    if moveUp:
        m = MoveUpMessage(d)
    else:
        m = MoveDownMessage(d)
    #m = MoveToPercent(d, 0)
    #print(["%02X" % f for f in m.serialize()])
    ser.write(m.serialize())
    ser.flush()
    time.sleep(1)
ser.close()             # close port
