//#include "OddSoftSer.h"
#include "Logic.h"

ZUNO_SETUP_SLEEPING_MODE(ZUNO_SLEEPING_MODE_ALWAYS_AWAKE);
ZUNO_SETUP_DEBUG_MODE(DEBUG_ON);

void setup() {
  real_setup();
}

void loop() { // run over and over
	real_loop();
}

void zunoCallback(void) {
	realZunoCallback();
}
