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

DISCOVER_ALL_MOTORS: *msgId=0xBF payload=0x80 0x80 0x80 0x00 0x00 0x00*
In reply to this message each motor sends a reply of this format:
HERE_IS_MOTOR: *msgId=0x9F payload=0xFF addr1 addr2 addr3*

REPORT_MOTOR_STATUS: *msgId=0xF3 payload=0x80 0x80 0x80 addr1 addr2 addr3*
In reply the motor specified by the address bytes sends the following message:
HERE_IS_POSITION: *msgId=0xF2 payload=0x80 0x80 0x80 0x00 0x00 0x00 0x00 0x00 0x00 POS*
where POS is 0xFF - shadePos, shadePos is the percentage of the shade position.

MOVE_TO_POSITION: *msgId=0xF2 payload=0x80 0x80 0x80 addr1 addr2 addr3 0xFB POS 0xFF 0xFF*
where POS is 0xFF - desiredPosInPercentage. A variant of this message can alsp be used
to move motor to one of the saved positions. By default there are two saved positions: fully
open and fully closed. The payloads for them are:

MOVE_TO_FULLY_OPEN:   *0x80 0x80 0x80 addr1 addr2 addr3 0xFE 0xFF 0xFF 0xFF*
MOVE_TO_FULLY_CLOSED: *0x80 0x80 0x80 addr1 addr2 addr3 0xFF 0xFF 0xFF 0xFF*

You can refer to the source code for more details on parsing and sending the messages, there's
also a helper calculator program in the *calculator.py* file.

If you want to discover more messages the easiest way is to download Somfy's commissioning 
utility and reverse engineer its protocol by spying on the serial traffic. You'll also need
this utility for the initial shade limits setup. There are two ways to spy on the serial traffic:

1. By using a Windows utility to intercept it. I haven't found free (or even cheap) ones.
2. You can utilize the fact that RS-485 is a broadcast medium and connect to it using a 
RS-485-to-USB converter. I eventually went this route.

The commissioning utility is written in VB.NET and you can fairly easily decompile it. Unfortunately,
it's heavily obfuscated by being badly written in VB.NET, so much that I found it easier to just
RE traffic. YMMV. 

## Hardware

You'll need Z-Uno chip from [Z-Wave.Me](https://z-uno.z-wave.me) with the 
[Shield](https://z-uno.z-wave.me/shield). You'll also need an [OLED display](https://www.adafruit.com/product/938),
this specific product is probably not required and any other I2C-driven 128x64 OLED display should
be fine.

## Somfy shades

You should connect Somfy shades to the same power adapter that is used to power the Z-Uno.
Somfy shades are powered by 24V DC. Fortunately Z-Uno shield can accept that voltage and 
transform it to 5V and 3.3V required to power the Z-Uno and the OLED display.  

I found out that 4-conductor solid (non-stranded) security cable works perfectly for 
shades connection. It's nicely color-coded (red/black, yellow/blue) and is rated for 
up to 48V. Regular CAT5 Ethernet cable should also work fine, although it's a little less
mechanically easy to work with.

### Serial port connection

First, set up the Z-Uno shield to operate in RS-485 mode by following the directions in the
manual. We can not use the built-in UART because of a highly unusual serial mode used by Somfy.
The clearly designed set the settings to confuse potential adversaries in case of alien invasion.

So we need to work around this by connecting *pin 7* to *pin 16* and *pin 8* to *pin 15*.
Then we can set up software serial with the required odd parity calculations on 
fast pins 15 and 16.  

Unfortunately, the built-in UART still interferes transmissions. I found that even with
disabled pins 7 and 8 there's still too much noise on the line to be useful, so I had to snip
these pins from the Z-Uno board :( You might not need to do it, try it first with the pins
intact.

If you're not using a ZUno shield then just ignore these directions and connect your RS-485
converter to pins *15* and *16*.

### OLED display connection

The built-in OLED support in Z-Uno uses a different I2C bus address for it, so I had to 
cut&paste the library from the Z-Uno support package and modify the address 
(SSD1306_ADDR definition).

Sidenote, the OLED library is under the CC BY-NC-SA 3.0 license that does not allow commercial
use. So please buy a license from http://www.RinkyDinkElectronics.com/ for it if you intend
to use it commercially.

OLED display needs to be switched to I2C mode by shorting the SJ1 and SJ2 pads.

OLED physical connection is straightforward:
1. Connect pins 9 and 10 to I2C pins on the OLED.
2. Connect ping 11 to the RST pin on the OLED (if your OLED has an RST pin).
3. Connect power and ground to the appropriate pins.

You can see the pinout on the attached photos.

## Software features

The software supports up to 4 blinds, although theoretically it can be modified to support more
by changing the MAX_BLINDS definition and tweaking the status display.

Each blinds is represented by a Z-Wave channel. However, since many hubs don't support composite
devices well, the first Z-Wave channel is used for collective movement. The value written to it
will is used to command all shades simultaneously. The read operations from it return the lowest
shade position. 

There is OLED screen-saving feature that turns OLED off after 1 minute of inactivity, to prevent
pixel burnout. The status display includes the current position of shades, the offline/online flag,
and the commanded position (if any).

All commands have 1 minute timeout, if a shade has not finished moving by that time it's considered
to be jammed.

### Setting up

The factory reset blinds is in the *discovery* state initially. In this state the board tries
to discover all accessible shades by spamming the *DISCOVER_ALL_SHADES* message and listening for
replies. Unfortunately, replies from multiple shades tend to come at exactly the same time 
so that the resulting shade address is garbled. Eventually shades de-synchronize enough to be
able to squawk the replies without stepping on each other's toes, but this can take up to 5 
minutes.

Once all the shades are discovered, press the *BTN* for a couple of seconds to switch to 
*ZWave inclusion* mode. In this mode the board goes into the inclusion mode until a hub accepts
it. Once the inclusion process is complete, the board goes into *service* mode and starts
accepting commands.

### Inclusion/exclusion and reset

You can exclude the board by using Z-Uno's exclusion mode:

Once the board is excluded, reset it and it'll go into the *ZWave inclusion* mode. The existing
shade configuration will not be affected. Additionally, you can always switch to the 
*ZWave inclusion* mode for 10 seconds by holding the *BTN* for *more than 2 but less than 6 seconds*.

However, if you want to actually reset the board to the initial settings, you can do 
this by pressing and holding *BTN* for at least 10 seconds. 

### Debugging and development

You can use the USB logging to get the sense of the general system state. Additionally, connecting
an RS-485-to-USB adapter to the cable allows to triage the connectivity issues.

You might notice that the main sketch file (Shutters.ino) is almost empty, as it simply calls the
functions defined in *Logic.cpp*. I did this mostly because I'm developing the code in 
IntelliJ CLion and it doesn't like *.ino* files. Moving everything into a .cpp file is just an
easy way to fool it.
