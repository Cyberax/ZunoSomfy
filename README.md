# Somfy Blinds RS-485 to ZWave gateway 

This project is a more-or-less complete bidirectional gateway to ZWave for Somfy shades that use the
RS-485 protocol. Apparently there are multiple iterations of it, the one I have works with these 
shades:

1. ILT-50
2. ILT-45

Feel free to notify me if there are any other compatible shades.

## Somfy protocol

Somfy shades use a fairly simple serial protocol over the RS-485 serial bus. However the 
major complication is the crazy port settings: 4800 baud speed with *negative* parity. This 
prevents us from using the built-in UART and requires the use of bit-banged software serial instead.
It's not particularly robust, but the protocol is compact enough for this to not matter.

The protocol consists of variable-sized frames. The structure is like this:

| 0    | 1   |  2   | ....... | N-1   | N-2   |
|------|-----|------|---------|-------|-------|
| MsgId| Len | 0xFF | payload | cksm1 | cksm2 |

Notes:
1. *Byte 2* is required to be present and set to 0xFF on all messages sent to the blinds.
2. Length is sent as (0xFF - msgLen), msgLen includes the checksum fields and the header (msgLen = len(payload) + 5).
3. Checksum is literally the sum of all the bytes in the message, written in big-endian order.

Typical payloads have the following structure:

|      0      |      1      |      2      |      3      |      4      |      5      | .... |
|-------------|-------------|-------------|-------------|-------------|-------------|------|
| group_addr1 | group_addr2 | group_addr3 | shade_addr1 | shade_addr2 | shade_addr3 | .... |

Group addresses are used for group commands and for our purposes are always set to 0x808080.

### Address obfuscation

Somfy for some strange reason obfuscates the addresses sent on the wire, however the obfuscation
system is trivial: bytes in the address are bitwise negated and reversed. You can find the address
printed on the side of the shades motor.

So the address (hex): *13 55 11* is sent as *EE AA EC*. 

### Notable messages

Here are some notable messages and replies.

DISCOVER_ALL_MOTORS: *msgId=0xBF, payload=0x80 0x80 0x80 0x00 0x00 0x00*
In reply to this message each motor sends a reply of this format:
HERE_IS_MOTOR: *msgId=0x9F, payload=0xFF addr1 addr2 addr3*

REPORT_MOTOR_STATUS: *msgId=0xF3, payload=0x80 0x80 0x80 addr1 addr2 addr3*
In reply the motor specified by the address bytes sends the following message:
HERE_IS_STATUS: *msgId=0xF2* payload=0x

## Hardware

You'll need Z-Uno chip from [Z-Wave.Me](https://z-uno.z-wave.me) with the 
[Shield](https://z-uno.z-wave.me/shield). You'll also need an [OLED display](https://www.adafruit.com/product/938),
this specific product is probably not required and any other I2C-driven 128x64 OLED display should
be fine.

Connect the OLED display to the board according to the diagram in the sketch.

### Serial port connection
   