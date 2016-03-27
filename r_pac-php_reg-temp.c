// Libraries
// NOTE: These need to be included during compiling
#include <stdio.h> 
#include <string.h>
#include <errno.h> 
#include <wiringPi.h>
#include <wiringSerial.h>

#define RFID_POWER 4
#define RFID_SERIAL 16 // Serial RX pin 
#define BANDRATE 9600
#define RFID_MAX_ATTEMPTS 5
		// Time in mS to attempt to read RFID Token
#define RFID_READ_TIME 30000

char tempRFIDToken[13];
int RFIDTokenSet;
int RFIDPowerState;
int RFIDAttempts;
int requestedRFIDPowerState;
	// Serial Communication Variables
int fd;



PI_THREAD(RFIDPowerHandler) {
	for(;;) {
		int currentRFIDPowerState;

		currentRFIDPowerState = digitalRead(RFID_POWER);

		if(requestedRFIDPowerState != currentRFIDPowerState) {
			digitalWrite(RFID_POWER, requestedRFIDPowerState);
		} else {
			RFIDPowerState = currentRFIDPowerState;
		}
	}
}
int main(void) {
// Initializing wiringPi
	wiringPiSetup();
// Fire RFID Power Handler
	piThreadCreate(RFIDPowerHandler);

	

	// Initializing Serial Communication 
	fd = serialOpen("/dev/ttyAMA0", BANDRATE);
	if(fd < 0) {
		fprintf (stderr, "Unable to open serial device: %s\n", strerror (errno)) ;
		return 0;
	}

	pinMode(RFID_POWER, OUTPUT);

	RFIDTokenSet = 0;

	RFIDPowerState = digitalRead(RFID_POWER);

	requestedRFIDPowerState = 0;
	
	int RFIDPowerCounter = 0;

	int RFIDReadTime = 0;
	char readChar;
	int startCharDetected = 0;
	int lastCharSet = 0;
	int endCharDetected = 0; 
	int success = 0;

	
	// Turn on RFID Reader
	requestedRFIDPowerState = 1;
	// Giving the RFIDPowerHandler a chance to change the power state
	while(!(RFIDPowerState) && RFIDPowerCounter < 5) {
		RFIDPowerCounter ++;
		sleep(1);
	}
	if(RFIDPowerCounter >= 5) {
		// Error handling
		fprintf(stderr, "Unable to start RFID Reader\n");
		return 0;
	}

	// Wait a little for the reader to start up
	delay(10);
	// Begin serial communication with RFID reader
	/*fd = serialOpen("/dev/ttyAMA0", RFID_BANDRATE);
	if(fd < 0) {
		fprintf (stderr, "Unable to open serial device: %s\n", strerror (errno)) ;
		requestedRFIDPowerState = 0;
		return 0 ;
	} */
	serialFlush(fd);
	do {
		if(success) {
			requestedRFIDPowerState = 0;
			serialFlush(fd);
			//serialClose(fd);
			RFIDTokenSet = 1;
			printf("%s\n", tempRFIDToken);
			return 1;
			
		} else {

			// NOTE: determine variables we need to monitor during loop
			

			// Has RFID read time been set? If not, set it
			if(RFIDReadTime < millis()) RFIDReadTime = millis() + RFID_READ_TIME;

			// Are there chars available?
			if(serialDataAvail(fd) > 0) {
				// Read in char and check read variables
				readChar = serialGetchar(fd);
				if(startCharDetected == 1 && endCharDetected == 1) {
					// Token has been set
					success = 1;
				} else if(startCharDetected) {
					// We've started to read token, check where we are 
					if(lastCharSet < 12 && lastCharSet >= 0) { // Reader supply's 12 chars. Start, End, and 10 chars in the actual token
						tempRFIDToken[lastCharSet] = readChar;
						lastCharSet ++;
					} else if (lastCharSet == 12 && readChar == 3) {
						tempRFIDToken[12] = '\0';
						endCharDetected = 1;
					} else { // Clear everything and try again
						serialFlush(fd);
						lastCharSet = 0;
						startCharDetected = 0;
						endCharDetected = 0;
					}
				} else if(!(startCharDetected)) {
					// Check to see if current char is the start char , if not discard
					if(readChar == 2) {
						startCharDetected = 1;
					}
				}
			} else if(startCharDetected == 1 && endCharDetected == 1) {
				// Token has been set
				success = 1;
			} else if(serialDataAvail(fd) < 0) {
				fprintf (stderr, "Unable to read serial data: %s\n", strerror (errno));
				requestedRFIDPowerState = 0;
				return 0;
			}
		}
	} while(millis() < RFIDReadTime);
	// If the end char has not been set by this point the RFID Token can not be read
	if(!(endCharDetected)) {
		fprintf (stderr, "Unable to read RFID Token: Allotted time has expired!\n");
		requestedRFIDPowerState = 0;
		serialFlush(fd);
		//serialClose(fd);
		return 0;
	}	
}

