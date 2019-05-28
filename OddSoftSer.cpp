#include "OddSoftSer.h"
#include "Arduino.h"

extern s_pin g_rx_pin;
extern byte g_rcv_state;
extern byte g_cb;
extern byte g_rcv_buff[MAX_RCV_BUFFER];
extern byte g_write_pos;
extern byte g_read_pos;
extern byte g_parity;

#ifndef __CLION_IDE__
ZUNO_SETUP_ISR_GPTIMER(softserial_gpt_handler);
#endif

#define DIRECTION_CONTROL_PIN 2

#define CONST_USECONDS_OFFSET 10 // uS
#define START_BIT_1HALF       0
#define START_BIT_2HALF       1
#define PARITY_BIT_1HALF      18
#define PARITY_BIT_2HALF      19
#define STOP_BIT_1HALF        20
#define STOP_BIT_2HALF        21

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"

void softserial_gpt_handler() {
	if (g_rcv_state == START_BIT_1HALF) {
		if (!digitalRead(g_rx_pin)) {
			g_cb = 0;
			g_parity = 0;
			g_rcv_state++;
		}
		return;
	}
	if (g_rcv_state == START_BIT_2HALF) {
		g_rcv_state++;
		return;
	}
	if (g_rcv_state == PARITY_BIT_1HALF) {
		if (!!digitalRead(g_rx_pin) == !!g_parity) {
			// Parity mismatch - invert bits so that receiver will notice
			g_cb = !g_cb;
		}
		g_rcv_state++;
		return;
	}
	if (g_rcv_state == PARITY_BIT_2HALF) {
		g_rcv_state++;
		return;
	}
	if (g_rcv_state == STOP_BIT_1HALF) {
		g_rcv_buff[g_write_pos] = g_cb;
		g_rcv_state++;
		return;
	}
	if (g_rcv_state == STOP_BIT_2HALF) {
		g_write_pos++;
		g_write_pos &= (MAX_RCV_BUFFER - 1);
		g_rcv_state = 0;
		return;
	}
	if (!(g_rcv_state & 0x01)) {
		g_cb >>= 1;
		if (digitalRead(g_rx_pin)) {
			g_cb |= 0x80;
			g_parity = !g_parity;
		}
	}
	g_rcv_state++;
}

#pragma clang diagnostic pop

OddSoftSer::OddSoftSer(s_pin tx_pin, s_pin rx_pin) {
	m_tx_pin = tx_pin;
	g_rx_pin = rx_pin;
}

void OddSoftSer::begin(word baud) {
	// Pin 2 controls the send/receive mode
	pinMode(DIRECTION_CONTROL_PIN, OUTPUT);
	digitalWrite(DIRECTION_CONTROL_PIN, LOW);

	m_baud = baud;

	// Set up the timer interrupt for reading
	dword ticks = 4000000L; // Each tick is a 0.25uS
	ticks /= (baud * 2);
	zunoGPTEnable(0);
	pinMode(g_rx_pin, INPUT_PULLUP);
	zunoGPTInit(ZUNO_GPT_CYCLIC | ZUNO_GPT_IMWRITE);
	zunoGPTSet(word(ticks));
	zunoGPTEnable(1);

	// Set up the output pin for writing
	ticks = 1000000L; // uS
	ticks /= baud;
	bit_time = ticks;
	bit_time -= CONST_USECONDS_OFFSET;
	pinMode(m_tx_pin, OUTPUT);
	digitalWrite(m_tx_pin, HIGH);
}

void OddSoftSer::end() {
	zunoGPTEnable(0);
}

uint8_t OddSoftSer::available(void) {
	uint8_t res;
	if (g_write_pos < g_read_pos)
		res = (MAX_RCV_BUFFER - g_read_pos) + g_write_pos;
	else
		res = g_write_pos - g_read_pos;
	return res;
}

// Drain the read buffer
void OddSoftSer::drain() {
	g_read_pos = g_write_pos;
}

int OddSoftSer::peek(void) {
	return g_rcv_buff[g_read_pos];
}

uint8_t OddSoftSer::read(void) {
	byte val = g_rcv_buff[g_read_pos];
	// We use cyclic buffer here
	g_read_pos++;
	g_read_pos &= (MAX_RCV_BUFFER - 1);
	return val;
}

void OddSoftSer::flush(void) {
}

void OddSoftSer::startWrite() {
//	noInterrupts();
}

void OddSoftSer::endWrite() {
//	interrupts();
}

void OddSoftSer::write(uint8_t d) {
	byte i = 8;
	noInterrupts();
	// Set the send mode
	digitalWrite(DIRECTION_CONTROL_PIN, HIGH);

	// Start Bit
	digitalWrite(m_tx_pin, 0);
	delayMicroseconds(bit_time);
	delayMicroseconds(4);
	// Bit sequence from the LSB
	bool parity = false;
	while (i--) {
		if (d & 0x01) {
			digitalWrite(m_tx_pin, 1);
			parity = !parity;
		} else {
			digitalWrite(m_tx_pin, 0);
		}
		d >>= 1;
		delayMicroseconds(bit_time);
		delayMicroseconds(3);
	}

	digitalWrite(m_tx_pin, parity?0:1);
	delayMicroseconds(bit_time);
	delayMicroseconds(4);

	// Stop Bit
	digitalWrite(m_tx_pin, 1);
	delayMicroseconds(bit_time);
	delayMicroseconds(4);

	// Set the receive mode back
	digitalWrite(DIRECTION_CONTROL_PIN, LOW);

	interrupts();
}

s_pin g_rx_pin = 12;
byte g_rcv_state = 0;
byte g_cb;
byte g_rcv_buff[MAX_RCV_BUFFER];
byte g_write_pos = 0;
byte g_read_pos = 0;
byte g_parity = 0;
