// Created by Besogonov Aleksei on 2019-05-25.
#include "OddSoftSer.h"
#include "FixedOled.h"
#include "EEPROM.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wwritable-strings"
#pragma ide diagnostic ignored "OCUnusedGlobalDeclarationInspection"
// Definitions
OddSoftSer blindsSerial(16, 15); // RX, TX
OLED oled;

#define BTN_PIN 18

// Somfy protocol stuff
#define DISCOVER_ALL_MOTORS 0xBFu
#define REPORT_MOTOR_STATUS 0xF3u
#define MOVE_MOTOR_TO_POS 0xFCu
#define HERE_IS_MOTOR 0x9Fu
#define HERE_IS_POSITION 0xF2u
#define MOVE_MOTOR_TO_LIMIT 0xFCu

//#define DEBUG_PRINT

// The shutters
struct Blinds {
	// Obfuscated wire address, see: https://blog.baysinger.org/2016/03/somfy-protocol.html
	byte addr1, addr2, addr3;
//	// Min and max settings (in ticks)
//	word minPos, maxPos;
	byte curPercentage;

	// Commanded position
	byte commanded, commandedPercent, commandReceived;
	dword commandedTime;

	// Health status
	dword lastTimeUpdated;
	byte isOffline;
};
#define MAX_BLINDS 4
#define OFFLINE_TIMEOUT 30000

Blinds blinds[MAX_BLINDS];
byte numBlinds = 0;

// The global mode
enum mode_t {DISCOVERY, JOINING, OPERATION};
mode_t globalMode;
dword lastInterestingTime, lastTimeRead;
dword lastReportSent;
byte oledIsOff;

void initOled();
void printStatus();

void runDiscoveryAttempt();
bool readWithTimeout(byte *string, word *pInt);
void initMotor(byte i, byte i1, byte i2);
bool readMotorStates();
void readMode();
void setMode(mode_t mode);
void loadBlinds();
void saveBlindSettings();

void setupChannels();

void processCommandedStatus(bool *hasCommanded, bool *shouldSendReport);

void sendReportThrottled(bool important);

void my_memzero(void *ptr, word sz) {
	for(word i=0; i<sz; ++i) {
		((byte*)ptr)[i] = 0;
	}
}

void real_setup() {
	initOled();
	pinMode(BTN_PIN, INPUT_PULLUP);  // set button pin as Input

	// Disable the hardware serial
	pinMode(7, INPUT);
	pinMode(8, INPUT);

	// Set up the software serial for blinds communication
	blindsSerial.begin(4800);
	// Debug serial
	Serial.begin(19200);

	lastReportSent = 0;

	my_memzero(blinds, sizeof(Blinds)*MAX_BLINDS);
	readMode();
	if (globalMode == JOINING || globalMode == OPERATION) {
		loadBlinds();
		setupChannels();
	} else {
		numBlinds = 0;
	}

	// Disable the green LED in the ops mode
	if (globalMode == OPERATION){
		word val = 0;
		zunoSaveCFGParam(2, &val);
		val = 5;
		zunoSaveCFGParam(11, &val);
	}

	printStatus();
}

bool differsBy(dword v1, dword v2, dword diff) {
	if (v1 >= v2) {
		return v1 - v2 >= diff;
	}
	return v2 - v1 >= diff;
}

void setupChannels() {
	ZUNO_START_CONFIG();
	// The overall switch
	ZUNO_ADD_CHANNEL(ZUNO_SWITCH_MULTILEVEL_CHANNEL_NUMBER, 0, 0)
	for(byte i=0; i<numBlinds; ++i) {
		ZUNO_ADD_CHANNEL(ZUNO_SWITCH_MULTILEVEL_CHANNEL_NUMBER, 0, 0)
	}
	ZUNO_COMMIT_CONFIG();
}

void readMode() {
	globalMode = (mode_t)EEPROM.read(1);
	if (globalMode > OPERATION) {
		globalMode = DISCOVERY;
	}
}

void setMode(mode_t mode) {
	if (globalMode != mode) {
		EEPROM.write(1, mode);
		globalMode = mode;
	}
}

void loadBlinds() {
	byte pos = 2;
	numBlinds = EEPROM.read(pos++);
	if (numBlinds > MAX_BLINDS) {
		numBlinds = 0;
	}
	for(byte i=0; i<numBlinds; ++i) {
		blinds[i].addr1 = EEPROM.read(pos++);
		blinds[i].addr2 = EEPROM.read(pos++);
		blinds[i].addr3 = EEPROM.read(pos++);
		blinds[i].curPercentage = 255;
		blinds[i].lastTimeUpdated = millis();
		blinds[i].commanded = 0;
	}
}

void saveBlindSettings() {
	byte pos = 2;
	EEPROM.write(pos++, numBlinds);
	for(byte i=0; i<numBlinds; ++i) {
		EEPROM.write(pos++, blinds[i].addr1);
		EEPROM.write(pos++, blinds[i].addr2);
		EEPROM.write(pos++, blinds[i].addr3);
	}
}

void initOled() {
	// Prepare the I2C pins
	pinMode(7, OUTPUT);
	digitalWrite(7, LOW);
	pinMode(8, OUTPUT);
	digitalWrite(8, LOW);

	// Reset the OLED display
	int oledResetPin = 11;
	pinMode(oledResetPin, OUTPUT);
	digitalWrite(oledResetPin, HIGH);
	delay(10);                  // VDD goes high at start, pause for 10 ms
	digitalWrite(oledResetPin, LOW);  // Bring reset low
	delay(10);                  // Wait 10 ms
	digitalWrite(oledResetPin, HIGH); // Bring out of reset
	delay(100);
	// Start the OLED
	oled.begin();
	delay(50);
	oled.setFont(SmallFont);
	oled.clrscr();
	oledIsOff = 0;
	oled.on();
}

void printPaddedHex(byte num) {
	if (num < 16){
		oled.print("0");
	}
	oled.print(num, 16);
}

void printPaddedPercentage(byte num) {
	if (num < 10) {
		oled.print(num);
		oled.print("%  ");
	} else if (num < 100) {
		oled.print(num);
		oled.print("% ");
	} else {
		oled.print(num);
		oled.print("%");
	}
}

// Print the current shutter status on OLED
void printStatus() {
	// Save the screen, disable it if nothing is happening.
	if (differsBy(millis(), lastInterestingTime, 60000)) {
		if (!oledIsOff) {
			oled.off();
			oledIsOff = 1;
		}
	} else {
		if (oledIsOff) {
			initOled();
		}
	}

	oled.gotoXY(0, 1);
	if (globalMode == DISCOVERY) {
		oled.println("Status: discovery");
		if (numBlinds>0) {
			oled.println("Press BTN to finish");
		}
	} else if (globalMode == JOINING) {
		oled.println("Status: zwave init");
	} else {
		oled.println("Status: working");
	}
	for (int i = 0; i < numBlinds; ++i) {
		// The wire address is obfuscated, deobfuscate it.
		printPaddedHex(~blinds[i].addr3);
		printPaddedHex(~blinds[i].addr2);
		printPaddedHex(~blinds[i].addr1);

		if (blinds[i].isOffline) {
			oled.println(": offline   ");
			continue;
		}

		if (blinds[i].curPercentage == 255) {
			oled.print(": N/A");
		} else {
			oled.print(": ");
			printPaddedPercentage(blinds[i].curPercentage);
		}

		if (blinds[i].commanded) {
			oled.print(" -> ");
			printPaddedPercentage(blinds[i].commandedPercent);
			oled.println();
		} else {
			oled.println("        ");
		}
	}
}

void sendSomfyMessage(byte msgId, byte *payload, byte payloadLen) {
	// Reserved byte is always 0xFF
	// [msgId, 0xFF - len(payload) - 5, reserved] + payload + checksum
	word checksum = 0;
	blindsSerial.drain();
	blindsSerial.startWrite();

	blindsSerial.write(msgId);
	checksum += msgId;

	blindsSerial.write(byte(0xFFu - payloadLen - 5));
	checksum += byte(0xFFu - payloadLen - 5);

	blindsSerial.write(byte(0xFFu));
	checksum += 0xFFu;

	for(byte i=0; i<payloadLen; ++i) {
		blindsSerial.write(payload[i]);
		checksum += payload[i];
	}

	blindsSerial.write(byte(checksum / 256));
	blindsSerial.write(byte(checksum % 256));

	blindsSerial.endWrite();
}

// Read the next byte from the serial, obeying the total time budget
bool readWithTimeout(byte *output, word *timeBudget) {
	while(!blindsSerial.available()) {
		if (*timeBudget == 0) {
			return false;
		}
		word curDelay = min(*timeBudget, 50);
		*timeBudget -= curDelay;
		delay(curDelay);
	}

	*output = blindsSerial.read();
#ifdef DEBUG_PRINT
	Serial.print(*output, 16);
	Serial.print(" ");
#endif
	return true;
}

bool readMessage(byte expectedType, byte *resultBuf, byte resultLen) {
	// We allocate 30 ms for the message reading to avoid ZWave radio timeouts
	word timeBudget = 30;

	// Try to read the next Somfy message. We want to start at the beginning
	// of the message, so we're looking for the msgId byte.
	while(timeBudget > 0) {
		byte msgId;
		if (readWithTimeout(&msgId, &timeBudget) && msgId == expectedType) {
			break;
		}
	}
	word checkSum = expectedType;

	// Now read the length byte
	byte lenByte;
	if (!readWithTimeout(&lenByte, &timeBudget)) {
		return false;
	}
	checkSum += lenByte;
	// The packet is: [msgId, 0xFF - payloadLen - 4, payload..., checksum1, checksum2]
	byte payloadLen = 0xFF - lenByte - 4; // Deobfuscate it.

	if (payloadLen > resultLen) {
		return false;
	}

	for(byte i=0; i<payloadLen; ++i) {
		byte payload;
		if (!readWithTimeout(&payload, &timeBudget)) {
			return false;
		}
		resultBuf[i] = payload;
		checkSum += payload;
	}

	// Now read the checksum and compare it to the computed
	byte checksum1, checksum2;
	if (!readWithTimeout(&checksum1, &timeBudget) ||
			!readWithTimeout(&checksum2, &timeBudget)) {
		return false;
	}

	bool checksumOk = (checksum1 == checkSum / 256) && (checksum2 == checkSum % 256);
#ifdef DEBUG_PRINT
	if (checksumOk) {
		Serial.println(" - ok");
	} else {
		Serial.println(" - failed");
	}
#endif
	return checksumOk;
}

void runDiscoveryAttempt() {
// DISCOVER_ALL
	byte discoverAllPayload[] = {0x80u, 0x80u, 0x80u, 00, 00, 00};
	byte resultBuf[20];

	blindsSerial.drain();
	sendSomfyMessage(DISCOVER_ALL_MOTORS, discoverAllPayload, 6);
	delay(100); // We have a nice buffer in the serial library, use it!

	// Listen for the HERE_IS_MOTOR blinds reply
	while(blindsSerial.available()) {
		if (readMessage(HERE_IS_MOTOR, resultBuf, 16)) {
			// The first 3 bytes of payload is the motor address
			initMotor(resultBuf[1], resultBuf[2], resultBuf[3]);
		}
	}
}

bool readMotorStates() {
	bool changed = false;
	// GET_MOTOR_STATUS payload buf
	byte getMotorStatus[] = {0x80u, 0x80u, 0x80u, 00, 00, 00};
	byte resultBuf[24];

	for(byte i=0; i<numBlinds; ++i) {
		// Interrogate each motor, use retries to compensate for bad network
		getMotorStatus[3] = blinds[i].addr1;
		getMotorStatus[4] = blinds[i].addr2;
		getMotorStatus[5] = blinds[i].addr3;

		for(int k=0; k<5; ++k) {
			sendSomfyMessage(REPORT_MOTOR_STATUS, getMotorStatus, 6);
			delay(80);
			if (readMessage(HERE_IS_POSITION, resultBuf, 16)) {
				byte newPos = 0xFF - resultBuf[9];
				blinds[i].lastTimeUpdated = millis();
				blinds[i].isOffline = false;

				if (blinds[i].curPercentage != newPos) {
					// The shades are moving, so the command was received
					blinds[i].commandReceived = 1;
					Serial.print("New pos for blind ");
					Serial.print(i); Serial.print(" is ");
					Serial.println(newPos);
					blinds[i].curPercentage = newPos;
					changed = true;
				}
				break;
			}
		}

		// Check for timeouts
		if (!blinds[i].isOffline &&
			differsBy(millis(), blinds[i].lastTimeUpdated, OFFLINE_TIMEOUT)) {

			changed = true;
			Serial.print("Shade "); Serial.print(i);
			Serial.println(" is offline");
			blinds[i].isOffline = true;
		}
	}
	return changed;
}

void initMotor(byte addr1, byte addr2, byte addr3) {
	if (numBlinds == MAX_BLINDS) {
		return;
	}

	byte insertPos = 0;
	for(byte i=0; i<numBlinds; ++i) {
		if (blinds[i].addr1 == addr1 && blinds[i].addr2 == addr2 &&
			blinds[i].addr3 == addr3) {
			return;
		}

		if (blinds[i].addr1 < addr1 ||
			(blinds[i].addr1 == addr1 && blinds[i].addr2 < addr2) ||
			(blinds[i].addr1 == addr1 && blinds[i].addr2 == addr2 && blinds[i].addr3 < addr3)) {
			insertPos = i;
		}
	}
	Serial.print("Discovered new motor: ");
	Serial.print(addr3, HEX); Serial.print(" ");
	Serial.print(addr2, HEX); Serial.print(" ");
	Serial.print(addr1, HEX); Serial.print(" ");
	Serial.println();

	// Insert the motor into the correct position. First shift existing blinds
	// down if needed.
	for(byte i=numBlinds; i>insertPos; --i) {
		// The compiler chokes on structure copy, do it manually
		memcpy(&blinds[i], &blinds[i-1], sizeof(Blinds));
	}
	blinds[insertPos].addr1 = addr1;
	blinds[insertPos].addr2 = addr2;
	blinds[insertPos].addr3 = addr3;
	blinds[insertPos].curPercentage = 255;
	blinds[insertPos].lastTimeUpdated = millis();
	blinds[insertPos].isOffline = false;
	blinds[insertPos].commanded = 0;
	numBlinds++;

	oled.clrscr();
	printStatus();
}

// Check if we want a reset
void checkResetOrInclude() {
	dword start = millis();
	while(millis() - start < 10000) {
		if (digitalRead(BTN_PIN) != LOW) {
			break;
		}
		delay(100);
	}

	DWORD diff = millis() - start;
	if (diff > 2000 && diff < 6000) {
		oled.clrscr();
		oled.println("Learning");
		zunoStartLearn(10, 0);
		oled.clrscr();
	}

	if (diff > 8000) {
		// The button was held for 10 seconds, reset the mode to discovery.
		// Will still need to exclude the board.
		setMode(DISCOVERY);
		oled.clrscr();
		oled.println("Device is reset");
		oled.println("Triple-click to exclude");
		delay(5000);
		zunoReboot();
	}
}

void sendReportThrottled(bool important) {
	if (important) {
		Serial.println("Sending an important report");
		lastReportSent = millis();
		zunoSendUncolicitedReport(1);
		return;
	}

	// Send the report
	dword diff;
	if (!differsBy(lastInterestingTime, millis(), 30000)) {
		diff = 10000;
	} else {
		diff = 300000;
	}

	if (differsBy(lastReportSent, millis(), diff)) {
		Serial.println("Sending a routine report");
		zunoSendUncolicitedReport(1);
		lastReportSent = millis();
	}
}

void real_loop() { // run over and over
	if (globalMode == DISCOVERY) {
		runDiscoveryAttempt();

		if ((numBlinds != 0 && digitalRead(BTN_PIN) == LOW) ||	numBlinds == MAX_BLINDS) {
			// Button is pressed - switch into the join mode if
			// we have at least one shutter discovered.
			saveBlindSettings();
			setMode(JOINING);
			oled.clrscr();
			printStatus();
			zunoReboot();
		}
		delay(200);
		return;
	}

	if (digitalRead(BTN_PIN) == LOW) {
		lastInterestingTime = millis();
	}
	checkResetOrInclude();

	if (globalMode == OPERATION && !zunoInNetwork()) {
		setMode(JOINING);
		oled.clrscr();
		return;
	}

	if (globalMode == JOINING) {
		if (zunoInNetwork()) {
			setMode(OPERATION);
			oled.clrscr();
			return;
		}
		// Start the unsecure inclusion
		zunoStartLearn(5, 0);
	}

	// Avoid polling the motor states too often, once every 600 seconds for normal periods
	// and once every 6 seconds for interesting events. Also do it while the OLED is on.
	bool shouldReadStates = lastTimeRead == 0;
	if (differsBy(millis(), lastInterestingTime, 20000) && oledIsOff) {
		shouldReadStates |= differsBy(millis(), lastTimeRead, 600000);
	} else {
		shouldReadStates |= differsBy(millis(), lastTimeRead, 6000);
	}

	if (globalMode == OPERATION) {
		// Process the commands
		bool isCommanded = false, shouldReport = false;
		processCommandedStatus(&isCommanded, &shouldReport);
		if (isCommanded) {
			lastInterestingTime = millis();
			shouldReadStates = true;
		}
		if (shouldReport) {
			sendReportThrottled(true);
		}
	}

	if (shouldReadStates) {
		lastTimeRead = millis();
		if (readMotorStates()) {
			// Something has changed in the motor states - always treat it as an
			// interesting event.
			lastInterestingTime = millis();
		}
		printStatus();
		sendReportThrottled(false);
	}

	delay(300);
}

void processCommandedStatus(bool *hasCommanded, bool *shouldSendReport) {
	byte moveMotorUp[]   = {0x80u, 0x80u, 0x80u, 00, 00, 00, 0xFEu, 0xFF, 0xFF, 0xFF};
	byte moveMotorDown[] = {0x80u, 0x80u, 0x80u, 00, 00, 00, 0xFFu, 0xFF, 0xFF, 0xFF};
	byte moveMotor[]     = {0x80u, 0x80u, 0x80u, 00, 00, 00, 0xFBu, 0,    0xFF, 0xFF};

	for(byte i=0; i<numBlinds; ++i) {
		if (!blinds[i].commanded) {
			continue;
		}
		*hasCommanded = true; // We have commanded blinds, this is always an interesting event

		if (!differsBy(blinds[i].commandedPercent,  blinds[i].curPercentage, 3)) {
			blinds[i].commanded = 0;
			blinds[i].commandedTime = 0;
			*shouldSendReport = true;
			Serial.print("Blind "); Serial.print(i);
			Serial.println(" has finished moving");
			continue;
		}

		if (differsBy(millis(), blinds[i].commandedTime, 60000)) {
			Serial.print("Blind "); Serial.print(i);
			Serial.println(" has timed out while moving");
			// The command is taking too long - reset the commanded status
			blinds[i].commanded = 0;
			blinds[i].commandedTime = 0;
			*shouldSendReport = true;
			continue;
		}

		if (blinds[i].commandReceived) {
			// No need to send the command multiple times
			continue;
		}

		byte msgId, size;
		byte *msg;
		Serial.print("Commanding blind "); Serial.print(i);
		if (blinds[i].commandedPercent <= 6) {
			// Opening blinds fully
			Serial.println(" to move up to the limit");
			msgId = MOVE_MOTOR_TO_LIMIT;
			msg = moveMotorUp;
			size = sizeof(moveMotorUp);
		} else if (blinds[i].commandedPercent >= 94) {
			// Closing blinds fully
			Serial.println(" to move down to the limit");
			msgId = MOVE_MOTOR_TO_LIMIT;
			msg = moveMotorDown;
			size = sizeof(moveMotorDown);
		} else {
			Serial.print(" to move to position ");
			Serial.println(blinds[i].commandedPercent);
			// Send out the command to move
			msgId = MOVE_MOTOR_TO_POS;
			msg = moveMotor;
			msg[7] = 0xFF - blinds[i].commandedPercent;
			size = sizeof(moveMotor);
		}

		msg[3] = blinds[i].addr1;
		msg[4] = blinds[i].addr2;
		msg[5] = blinds[i].addr3;

		// Send the command multiple times to be sure
		for(int k=0; k<2; ++k) {
			sendSomfyMessage(msgId, msg, size);
			delay(40);
		}
	}
}

// ZWave getters
void realZunoCallback(void) {
	byte cb_min, i;
	switch(callback_data.type) {
		case ZUNO_CHANNEL1_GETTER:
			cb_min = 100;
			for(i=0; i<numBlinds; ++i) {
				if (blinds[i].curPercentage < cb_min) {
					cb_min = blinds[i].curPercentage;
				}
			}
			callback_data.param.bParam = cb_min;
			return;
		case ZUNO_CHANNEL1_SETTER:
			for(i=0; i<numBlinds; ++i) {
				blinds[i].commanded = 1;
				blinds[i].commandedPercent = min(100, callback_data.param.bParam);
				// Clamp to 100%
				if (blinds[i].commandedPercent > 97) {
					blinds[i].commandedPercent = 100;
				}
				blinds[i].commandedTime = millis();
				blinds[i].commandReceived = 0;
			}
			return;
		default:
			break;
	}

	// Try to check getters/setters for individual channels
	for(i=0; i<numBlinds; ++i) {
		// Getter for one of the blinds
		// See the definition of ZUNO_CHANNEL1_GETTER for context
		if (callback_data.type == ((i+1) << 1)) {
			callback_data.param.bParam = blinds[i].curPercentage;
			return;
		}
		// Setter
		// See the definition of ZUNO_CHANNEL1_SETTER for context
		if (callback_data.type == (((i+1) << 1) | SETTER_BIT)) {
			blinds[i].commanded = 1;
			blinds[i].commandedPercent = min(100, callback_data.param.bParam);
			blinds[i].commandedTime = millis();
			blinds[i].commandReceived = 0;
			return;
		}
	}
}

#pragma clang diagnostic pop
