#pragma once

#include "Stream.h"

#define MAX_RCV_BUFFER 128 // !!! HAVE to be 2^n

// Bit-banged software serial port with negative parity support.
// It uses global variables under the hood, so only one instance of this class
// can exist.
class OddSoftSer : public Stream
{
private:
	word bit_time;
	s_pin m_tx_pin;

public:
	// Duplex version (TX&RX)
	OddSoftSer(s_pin tx_pin, s_pin rx_pin);

	void begin(word baud);

	virtual uint8_t available(void);

	virtual int peek(void);

	virtual uint8_t read(void);

	virtual void flush(void);

	virtual void write(uint8_t);

	// Clear the input buffer
	void drain();
};
