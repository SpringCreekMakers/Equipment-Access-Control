/*
  main.c - Equipment Access Control

  Copyright (c) 2016 Spring Creek Think Tank, LLC. All rights reserved.
  Author Shane E. Bryan <shane.bryan@scttco.com>
  
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

// Libraries
// NOTE: These need to be included during compiling
#include <stdio.h> 
#include <string.h>
#include <errno.h> 
#include <wiringPi.h>
#include <wiringSerial.h>
#include <mysql.h>

// Defining Constants 
	// Pins
	/* NOTE: These pins do not change during runtime. So They're set as constants
	*  during programming. The pin number used is from wiringPi's pin references.
	*/
#define IR_INTERRUPT 0
#define POWER_BUTTON 2
#define POWER_RELAY 3
#define RFID_POWER 4
#define RFID_SERIAL 16 // Serial RX pin 

	// Temporary Pins
	/* NOTE: We're using these pins for testing while we figure out WS2812 functionality.
	*  We will most likely be using a slave micro controller over SPI.
	*/
#define LED_RED 12
#define LED_GREEN 1
#define LED_BLUE 5
#define LED_YELLOW 6
#define LED_PURPLE 10

	// Thread Keys (0-3)
#define IR_INTERRUPT_KEY 0
#define POWER_INTERRUPT_KEY 1

	// Other Defined Variables
		// Debounce time in mS
#define DEBOUNCE_TIME 200
		// RFID Consts
#define RFID_BANDRATE 9600
#define RFID_MAX_ATTEMPTS 5
		// Time in mS to attempt to read RFID Token
#define RFID_READ_TIME 10000
		// Time in mS between verifying RFID Token
#define REAUTHENTICATE_TIME 30000
		// Time in mS for disconnect warning
#define DISCONNECT_TIME 30000
#define DISCONNECT_WARNING 30000
		// LED Rate Setting
#define LED_SOLID 0
#define LED_SLOW 1
#define LED_FAST 2
		// LED Delays mS
#define LED_SLOW_RATE 1000
#define LED_FAST_RATE 100

// Global Variables
	// Database Variables
char *server = "localhost";
char *user = "root";
char *password = "PASSWORD";
char *database = "mysql";

	// Authentication Variables 
int equipmentID = 1234567;
char tempRFIDToken[13];
char RFIDToken[13];
int RFIDTokenSet;
int authenticated;
int authorized;

	// State Variables
int IRInterruptState; 
int powerButtonState; 
int powerRelayState; 
int RFIDPowerState;
int LEDState; // Check the LEDHandler function for a list of different states
int buzzerState; // "t" RFID tag error make sure its attached, "f" power button off, "a" not authorized, "A" authorized, "s" start up

	// Function Variables
int lastAuthenticated;
int RFIDAttempts;
int requestedPowerRelayState;
int requestedRFIDPowerState;

	// Reservation Variables


// Function Declarations 
void initialize(void);
void resetVariables(void);
void IRInterrupt(void);
void powerInterrupt(void);
int getRFIDToken(char);
int authenticateRFIDToken(void);
int compareRFIDTokens(char);
int powerConnect(char);
void powerDisconnect(char);
int powerWarning(char);

PI_THREAD(accessControlHandler) {
	/*************** Different Main States ********************
	* Waiting
	* Reading
	* Authenticating
	* Authorized
	**********************************************************/
	for(;;) {
		// Is there a RFID Token inserted? 
		if(IRInterruptState == 1) {

			// Has the RFID Token been set?
			if(RFIDTokenSet == 1) {

				// Has the RFID Token been authenticated?
				if(authenticated == 1) {

					// is the RFID Token authorized to use this Equipment 
					if(authorized == 1) {

						// Determining what to do based on the power relay and button states

							// Everything is authorized, on, and working
						if(powerRelayState && powerButtonState) {
							LEDState = 3;
							buzzerState = 3;

							// The power button is off
						} else if(!(powerButtonState)) {
							if(powerRelayState) powerDisconnect('B');
							LEDState = 5;
							buzzerState = 1;

							// The power button is on
						} else if(powerButtonState) {
							if(!(powerRelayState)) powerConnect('A');

							// There was an error
						} else {
							// Error handling
						}
						
						// Check to see if the currently inserted RFID Token needs to be re-authenticated
						if(millis() > (REAUTHENTICATE_TIME + lastAuthenticated)) {

							// Compare currently inserted RFID Token with currently authorized Token
							if(compareRFIDTokens('R')) {
								lastAuthenticated = millis();
						
							// Currently inserted RFID Token does not match
							// Start disconnect time to give time to fix issue before power disconnect
							} else {
								powerWarning('R');
							}
						}
					
					// Alert user that they are NOT authorized to use this Equipment
					} else {
						powerDisconnect('A');
					}

				// RFID Token needs to be authenticated	
				} else { 
					LEDState = 12;
					authenticateRFIDToken();
				}

			// RFID Token needs to be read and set	
			} else {
				LEDState = 13;
				if(RFIDAttempts < RFID_MAX_ATTEMPTS) {
					if(getRFIDToken('A')) {
						strncpy(RFIDToken, tempRFIDToken, 13);
					}
				} else {
					// Error handling
				}
			}

		// Wait for a RFID Token to be inserted
		} else {
			// Was the Token Removed
			if(powerRelayState == 1) {
				powerWarning('I');
			} else {
				LEDState = 4;
				resetVariables();
			}
		}
	}
}

/****************** Different LED States ****************************

********** Red ************
* 0 = Solid Red
* 1 = Slowly blinking Red
* 2 = Fast blinking Red

********** Green **********
* 3 = Solid Green
* 4 = Slowly blinking Green
* 5 = Fast blinking Green

********** Blue ***********
* 6 = Solid Blue
* 7 = Slowly blinking Blue
* 8 = Fast blinking Blue

********** Purple *********
* 9 = Solid Purple
* 10 = Slowly blinking Purple
* 11 = Fast blinking Purple

********** Yellow *********
* 12 = Solid Yellow
* 13 = Slowly blinking Yellow
* 14 = Fast blinking Yellow
********************************************************************/
PI_THREAD(LEDHandler) {
// Code used for testing 
	int LEDLastState;
	int led;
	int rate;

	for(;;) {
		if(LEDState != LEDLastState) {
			// Clear all current LED pins
			digitalWrite(LED_RED, LOW);
			digitalWrite(LED_GREEN, LOW);
			digitalWrite(LED_BLUE, LOW);
			digitalWrite(LED_PURPLE, LOW);
			digitalWrite(LED_YELLOW, LOW);

			LEDLastState = LEDState;

			switch(LEDState) {
				case 0:
					led = LED_RED;
					rate = LED_SOLID;
				break;
				case 1:
					led = LED_RED;
					rate =LED_SLOW;
				break;
				case 2:
					led = LED_RED;
					rate = LED_FAST;
				break;
				case 3:
					led = LED_GREEN;
					rate = LED_SOLID;
				break;
				case 4:
					led = LED_GREEN;
					rate = LED_SLOW;
				break;
				case 5:
					led = LED_GREEN;
					rate = LED_FAST;
				break;
				case 6:
					led = LED_BLUE;
					rate = LED_SOLID;
				break;
				case 7:
					led = LED_BLUE;
					rate = LED_SLOW;
				break;
				case 8:
					led = LED_BLUE;
					rate = LED_FAST;
				break;
				case 9:
					led = LED_PURPLE;
					rate = LED_SOLID;
				break;
				case 10:
					led = LED_PURPLE;
					rate = LED_SLOW;
				break;
				case 11:
					led = LED_PURPLE;
					rate = LED_FAST;
				break;
				case 12:
					led = LED_YELLOW;
					rate = LED_SOLID;
				break;
				case 13:
					led = LED_YELLOW;
					rate = LED_SLOW;
				break;
				case 14:
					led = LED_YELLOW;
					rate = LED_FAST;
				break;
				// Error
				default:
					led = LED_RED;
					rate = LED_FAST;
			}
		}
		switch(rate) {
			// Solid
			case 0:
				digitalWrite(led, HIGH);
			break;
			// Slow Blink
			case 1:
				digitalWrite(led, HIGH);
				delay(LED_SLOW_RATE);
				digitalWrite(led, LOW);
				delay(LED_SLOW_RATE);
			break;
			// Fast Blink
			case 2:
				digitalWrite(led, HIGH);
				delay(LED_FAST_RATE);
				digitalWrite(led, LOW);
				delay(LED_FAST_RATE);
			break;
			// Error
			default:
				digitalWrite(LED_RED, HIGH);
				delay(LED_FAST_RATE);
				digitalWrite(LED_RED, LOW);
				delay(LED_FAST_RATE);
		}
	} 
}

/****************** Different Buzzer States ****************************

* 0 = RFID Tag error during re authentication
* 1 = Power Button off after authorization
* 2 = Not Authorized
* 3 = Authorized
* 4 = Start up

********************************************************************/
PI_THREAD(buzzerHandler) {
}

PI_THREAD(PowerRelayHandler) {
	for(;;) {
		int currentRelayState;

		currentRelayState = digitalRead(POWER_RELAY);

		if(requestedPowerRelayState != currentRelayState) {
			digitalWrite(POWER_RELAY, requestedPowerRelayState);
		} else {
			powerRelayState = currentRelayState;
		}
	}
}

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

// Setup Function 

void setup(void) {

	// Initialize functions and variables
	LEDState = 14;
	buzzerState = 4;
	initialize();
	resetVariables();


	// Program interrupts to be executed any time pin IR_INTERRUPT or POWER_BUTTON chances state. 
	// Runs in a separate thread upon being fired
	wiringPiISR(IR_INTERRUPT, INT_EDGE_BOTH, &IRInterrupt); 
	wiringPiISR(POWER_BUTTON, INT_EDGE_BOTH, &powerInterrupt);

	// Fire Access Control Handler
	piThreadCreate(accessControlHandler);

	// Fire Power Relay Handler
	piThreadCreate(PowerRelayHandler);

	// Fire RFID Power Handler
	piThreadCreate(RFIDPowerHandler);

	// Fire LED Handler
	piThreadCreate(LEDHandler);

	// Fire Buzzer Handler
	piThreadCreate(buzzerHandler);
}

// Main Function/Loop
int main(void) {

	setup();

	// Repeating Loop 
	for(;;) {
		sleep(1);
	}

	return 0;
}


void initialize(void) {
	printf("Initializing this machines Access Control System...\n");
	// Initializing MySQL 
	MYSQL *conn;
	MYSQL_RES *res;
	MYSQL_ROW row;
	conn = mysql_init(NULL);

	// Initializing wiringPi
	wiringPiSetup();

	// Declaring Pin Modes
	pinMode(IR_INTERRUPT, INPUT);
	pinMode(POWER_BUTTON, INPUT);
	pinMode(POWER_RELAY, OUTPUT);
	pinMode(RFID_POWER, OUTPUT);

	
		// LED Pins for Testing
	pinMode(LED_RED, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);
	pinMode(LED_BLUE, OUTPUT);
	pinMode(LED_YELLOW, OUTPUT);
	pinMode(LED_PURPLE, OUTPUT);

	// Setting LED Pins to their initial state
	digitalWrite(LED_RED, LOW);
	digitalWrite(LED_GREEN, LOW);
	digitalWrite(LED_BLUE, LOW);
	digitalWrite(LED_YELLOW, LOW);
	digitalWrite(LED_PURPLE, LOW);
}

void resetVariables(void) {

	// Resetting authentication variables to their initial values
	RFIDTokenSet = 0;
	authenticated = 0;
	authorized = 0;

	// State Variables
	IRInterruptState = digitalRead(IR_INTERRUPT);
	powerButtonState = digitalRead(POWER_BUTTON);
	powerRelayState = digitalRead(POWER_RELAY);
	RFIDPowerState = digitalRead(RFID_POWER);

	// Function Variables
	requestedPowerRelayState = 0;
	requestedRFIDPowerState = 0;
	RFIDAttempts = 0;
}

void IRInterrupt(void) {
	int IRIDebounceTime = 0;
	int IRICurrentState;

	printf("Change in the state of the IR Interrupt has been detected.\n");
	while(millis() < IRIDebounceTime) delay(1); // Waiting for debounce before we check current state  

	IRICurrentState = digitalRead(IR_INTERRUPT);
	if(IRInterruptState != IRICurrentState) {
		piLock(IR_INTERRUPT_KEY);
		IRInterruptState = IRICurrentState;
		piUnlock(IR_INTERRUPT_KEY);
		IRIDebounceTime = millis() + DEBOUNCE_TIME;
	}
}

void powerInterrupt(void) {
	int debounceTime = 0;
	int currentState;

	printf("Change in the state of the Power Button has been detected.\n");
	while(millis() < debounceTime) delay(1); // Waiting for debounce before we check current state  

	currentState = digitalRead(POWER_BUTTON);
	if(powerButtonState != currentState) {
		piLock(POWER_INTERRUPT_KEY);
		powerButtonState = currentState;
		piUnlock(POWER_INTERRUPT_KEY);
		debounceTime = millis() + DEBOUNCE_TIME;
	}
}

int getRFIDToken(char reason) {
	// A = initial read state
	// U = Comparison failed but is the new Token authorized? 
	// R = Re-authenticating Token (comparing Token)
	// P = PowerWarning  (comparing Token) 

	char _reason = reason; 
	int RFIDPowerCounter = 0;
	int fd;
	int RFIDReadTime = 0;
	char readChar;
	int startCharDetected = 0;
	int lastCharSet = 0;
	int endCharDetected = 0; 
	int success = 0;

	RFIDAttempts ++;
	printf("Attempting to read the RFID Token\n");
	// Turn on RFID Reader
	requestedRFIDPowerState = 1;
	// Giving the RFIDPowerHandler a chance to change the power state
	while(!(RFIDPowerState) && RFIDPowerCounter < 5) {
		RFIDPowerCounter ++;
		sleep(1);
	}
	if(RFIDPowerCounter >= 5) {
		// Error handling
		return 0;
	}

	// Wait a little for the reader to start up
	delay(10);
	// Begin serial communication with RFID reader
	fd = serialOpen("/dev/ttyAMA0", RFID_BANDRATE);
	if(fd < 0) {
		fprintf (stderr, "Unable to open serial device: %s\n", strerror (errno)) ;
		requestedRFIDPowerState = 0;
		return 0 ;
	}
	serialFlush(fd);
	do {
		if(success) {
			requestedRFIDPowerState = 0;
			serialFlush(fd);
			serialClose(fd);
			RFIDTokenSet = 1;
			printf("RFID Token: %s\n", tempRFIDToken);
			return 1;
		} else {

			// NOTE: determine variables we need to monitor during loop
			
			// If we're in the read state we do not need to continue if IRI goes low
			if(_reason == 'A' && IRInterruptState == 0) {
				requestedRFIDPowerState = 0;
				return 0;
			}

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
				// Token has been set, turn RFID reader off and return 0
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
		serialClose(fd);
		return 0;
	}	
}

int authenticateRFIDToken(void) {
	printf("Authenticating the RFID Token with user database...\n");
	sleep(10);
	char shane[13] = {'3','7','0','0','1','8','B','5','6','E','F','4'};
	int authCompare = strcmp(shane, RFIDToken);
	if(authCompare == 0) {
		printf("User is authorized to use this machine.\n");
		authenticated = 1;
		authorized = 1;
	} else {
		printf("User is NOT authorized to use this machine.\n");
		authenticated = 1;
		authorized = 0;
	}
}

int compareRFIDTokens(char reason) {
	char _reason = reason; // R = Reauthenticating P = powerWarning
	
	printf("Comparing authenticated token with currently inserted token\n");
	// Initializing Local Variables
	int stringCompare;

	// Reading currently inserted Token
	if(getRFIDToken(_reason)) {
		// Comparing Tokens
		stringCompare = strcmp(RFIDToken, tempRFIDToken);
		if(stringCompare == 0) {
			// Tokens are the same. Return true
			return 1;
		} else {
			// Tokens are different. Return False
			// NOTE: Here is where we need to authenticate the new token
			return 0;
		}
	}
}

int powerConnect(char reason) {
	int _reason = reason; // A = Authorized
	int powerRelayCounter = 0;

	printf("Connecting Power\n");
	requestedPowerRelayState = 1;
	while(!powerRelayState && powerRelayCounter < 5) {
		powerRelayCounter ++;
		sleep(1);
	}
	if(powerRelayCounter >= 5) {
		// Error handling
		return 0;
	}
	return 1;
}

void powerDisconnect(char reason) {
	char _reason = reason; // B = , A = , R = , I =
	int _disconnectTime;
	int powerRelayCounter = 0;

	if(powerRelayState) {
		if(_reason == 'R' || _reason == 'I') {
			LEDState = 0;
			buzzerState = 2;

			printf("Power Disconnect Eminent!\n");
			_disconnectTime = millis() + DISCONNECT_TIME;
		
			while(millis() < _disconnectTime) sleep(1);
		}
		requestedPowerRelayState = 0;
		while(!powerRelayState && powerRelayCounter < 5) {
			powerRelayCounter ++;
			sleep(1);
		}
		if(powerRelayCounter >= 5) {
			// Error handling

		}
		
		if(!(_reason == 'B')) {
			printf("Power Disconnected.\n");
			while(IRInterruptState) sleep(1);
			resetVariables();
		}
	} else if(_reason == 'A') {
		LEDState = 0;
		while(IRInterruptState) sleep(1);
		resetVariables();
	}
}

int powerWarning(char reason) {
	char _reason = reason; //R = Re-authentication, I = IRI
	int _disconnectWarning;

	LEDState = 1;
	buzzerState = 0;
	
	_disconnectWarning = millis() + DISCONNECT_WARNING;
		
	while(millis() < _disconnectWarning) {
		
		if(_reason == 'R' && !(IRInterruptState)) return 0;
		if(_reason == 'I' && !(powerRelayState)) return 0;
		if(compareRFIDTokens(_reason)) {
			lastAuthenticated = millis();
			return 1;
		}
	}
	
	powerDisconnect(_reason);

}

// Backup power? LED blink yellow 
// Tempering evident? LED blink red
// reservations? LED approaching time blink blue, during solid blue, approaching cancellation fast blink blue

// register code for display