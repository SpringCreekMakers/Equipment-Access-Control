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
#include <wiringPi.h>
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
#define DEBOUNCE_TIME 100
		// Time in mS between verifying RFID Token
#define REAUTHENTICATE_TIME 30000
		// Time in mS for disconnect warning
#define DISCONNECT_TIME 30000
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
int RFIDTokenSet;
char RFIDToken[11];
int authenticated;
int authorized;
int lastAuthenticated;
int disconnectTime;

	// State Variables
int IRInterruptState; // 1 - RFID Tag inserted, 0 - No tag inserted
int powerButtonState; // 1 - on, 0 - off
int powerRelayState; // 1 - Power on, 0 - Power off
int LEDState; // Check the LEDHandler function for a list of different states
int buzzerState; // "t" RFID tag error make sure its attached, "f" power button off, "a" not authorized, "A" authorized, "s" start up
int alertedPowerOff; // Has the user been alerted to power off state
int alertedAuthorized; // Has the user been alerted that they're authorized
	// Reservation Variables


// Function Declarations 
void initialize(void);
void IRInterrupt(void);
void powerInterrupt(void);
void setRFIDToken(void);
int authenticateRFIDToken(char*);
int compareRFIDTokens(char*);
int powerConnect(char);
int powerDisconnect(char);
void resetVariables(void);

PI_THREAD(accessControlHandler) {

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
						if(!(powerRelayState == 1) || !(powerButtonState == 1)) {

							// Is the power relay state true
							if(!(digitalRead(POWER_RELAY))) {

								//Run power connect
								powerConnect('A');
							} 

							// If the power button is off alert user
							if(!(powerButtonState)) {
								LEDState = 5;

								// Has the user been alerted by this buzzer
								if(!alertedPowerOff) {
									buzzerState = 1;
									alertedPowerOff = 1;
								}
							}

						// Everything is authorized, on, and working	
						} else {
							LEDState = 3;

							// Has the user been alerted by this buzzer
							if(!alertedAuthorized) {
								buzzerState = 3;
								alertedAuthorized = 1;
							}

							alertedPowerOff = 0; // Resetting buzzer alert
						}
						
						// Check to see if the currently inserted RFID Token needs to be re-authenticated
						if(millis() > (REAUTHENTICATE_TIME + lastAuthenticated)) {

							// Compare currently inserted RFID Token with currently authorized Token
							if(compareRFIDTokens(RFIDToken)) {
								lastAuthenticated = millis();

							// Currently inserted RFID Token does not match
							// Start disconnect time to give time to fix issue before power disconnect
							} else {

								// Has disconnect time expired? If so, start disconnect
								if(millis() > disconnectTime) {
									powerDisconnect('r');
									disconnectTime = millis();

								// Disconnect time has not expired or has not been set
								} else {
									LEDState = 1;
									buzzerState = 0;
									// Has disconnect time been set?
									if(disconnectTime < millis()) {
										disconnectTime = millis() + DISCONNECT_TIME;
									}
								}
							}
						}
					
					// Alert user that they are NOT authorized to use this Equipment
					} else {
						powerDisconnect('a');
						LEDState = 0;
						buzzerState = 2;
					}

				// RFID Token needs to be authenticated	
				} else {
					alertedAuthorized = 0; // Resetting alert 
					LEDState = 12;
					authenticateRFIDToken(RFIDToken);
				}

			// RFID Token needs to be read and set	
			} else {
				LEDState = 13;
				setRFIDToken();
			}

		// Wait for a RFID Token to be inserted
		} else {
			LEDState = 4;
			resetVariables();
		}
	}
	
}

PI_THREAD(LEDHandler) {

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

PI_THREAD(buzzerHandler) {

/****************** Different Buzzer States ****************************

* 0 = RFID Tag error during re authentication
* 1 = Power Button off after authorization
* 2 = Not Authorized
* 3 = Authorized
* 4 = Start up

********************************************************************/
}

// Setup Function 

void setup(void) {

	// Initialize functions and variables
	initialize();

	// Program interrupts to be executed any time pin IR_INTERRUPT or POWER_BUTTON chances state. 
	// Runs in a separate thread upon being fired
	wiringPiISR(IR_INTERRUPT, INT_EDGE_BOTH, &IRInterrupt); 
	wiringPiISR(POWER_BUTTON, INT_EDGE_BOTH, &powerInterrupt);

	// Fire Access Control Handler
	piThreadCreate(accessControlHandler);

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


void initialize() {
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

	// Setting Pins to their Initial State
	digitalWrite(POWER_RELAY, LOW);

		// LED Pins for Testing
	digitalWrite(LED_RED, HIGH);
	digitalWrite(LED_GREEN, LOW);
	digitalWrite(LED_BLUE, LOW);
	digitalWrite(LED_YELLOW, LOW);
	digitalWrite(LED_PURPLE, LOW);

	// Setting Variables to their Initial Values
	RFIDTokenSet = 0;
	authenticated = 0;
	authorized = 0;

		// State Variables
	IRInterruptState = 0;
	powerButtonState = 0;
	powerRelayState = 0; 
	LEDState = 14;
	buzzerState = 4;
	alertedPowerOff = 0;
	alertedAuthorized = 0;
}

void IRInterrupt(void) {
	int IRIDebounceTime = 0;
	int IRICurrentState;

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

	while(millis() < debounceTime) delay(1); // Waiting for debounce before we check current state  

	currentState = digitalRead(POWER_BUTTON);
	if(powerButtonState != currentState) {
		piLock(POWER_INTERRUPT_KEY);
		powerButtonState = currentState;
		piUnlock(POWER_INTERRUPT_KEY);
		debounceTime = millis() + DEBOUNCE_TIME;
	}
}

void setRFIDToken(void) {
	// we need an attempting read variable. same with authent and auth
	sleep(20);
	RFIDTokenSet = 1;
}

int authenticateRFIDToken(char* token) {
	sleep(20);
	authenticated = 1;
	authorized = 1;
}

int compareRFIDTokens(char* token) {

}

int powerConnect(char reason) {
	powerRelayState = 1;
}

int powerDisconnect(char reason) {
	powerRelayState = 0;
}

void resetVariables(void) {

}

// Backup power? LED blink yellow 
// Tempering evident? LED blink red
// reservations? LED approaching time blink blue, during solid blue, approaching cancellation fast blink blue

// register code for display