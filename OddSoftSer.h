#pragma once

#include "Stream.h"

#define MAX_RCV_BUFFER 128 // !!! HAVE to be 2^n

class OddSoftSer : public Stream
{
private:
	word m_baud;
	word bit_time;
	s_pin m_tx_pin;

public:
	// Duplex version (TX&RX)
	OddSoftSer(s_pin tx_pin, s_pin rx_pin);

	void begin(word baud);

	void end();

	virtual uint8_t available(void);

	virtual int peek(void);

	virtual uint8_t read(void);

	virtual void flush(void);

	virtual void write(uint8_t);

	void startWrite();
	void endWrite();

	void drain();

	uint8_t write(unsigned long n) {
		write((uint8_t) n);
		return 1;
	}

	uint8_t write(long n) {
		write((uint8_t) n);
		return 1;
	}

	uint8_t write(unsigned int n) {
		write((uint8_t) n);
		return 1;
	}

	uint8_t write(int n) {
		write((uint8_t) n);
		return 1;
	}
};
