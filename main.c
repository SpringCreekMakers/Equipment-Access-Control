/*
  main.c - Equipment Access Control

  Copyright (c) 2016 Spring Creek Makers, LLC. All rights reserved.
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
	*  We will most likely be using a slave micro controller over Serial.
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
#define BANDRATE 9600
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
char *server = "10.1.10.61";
char *user = "testr_pac";
char *password = "Testing12345#";
char *database = "r_pac";
MYSQL *conn;

	// Authentication Variables 
int equipmentID = 1;
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

	// Serial Communication Variables
int fd;


	// LED Variables
int LEDCommunication = 0; // 0 = PIN, 1 = Serial
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

					// is the RFID Token authorized to use this Device 
					if(authorized == 1) {

						// Determining what to do based on the power relay and button states

							// Everything is authorized, on, and working
						if(powerRelayState && powerButtonState) {
							LEDState = 3;
							buzzerState = 3;

							// Power Relay is off
						} else if(!(powerRelayState)) {
							powerConnect('A');

							// Power button is off
						} else if(!(powerButtonState)) {
							LEDState = 5;
							buzzerState = 1;

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
					if(!authenticateRFIDToken()) { // Need to reattempt to authenticate ?
						authenticated = 1;
						authorized = 0;
						fprintf(stderr, "An error has occured during authentication\n");
					}
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
			if(powerRelayState == 1 && requestedPowerRelayState == 1) {
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
	int colorHEX;

	for(;;) {
		if(LEDState != LEDLastState) {
			// Clear all current LED pins
			//if(LEDCommunication == 0) {
				digitalWrite(LED_RED, LOW);
				digitalWrite(LED_GREEN, LOW);
				digitalWrite(LED_BLUE, LOW);
				digitalWrite(LED_PURPLE, LOW);
				digitalWrite(LED_YELLOW, LOW);
			//}
			serialPutchar(fd,2);
			serialPrintf(fd,"LED:clear\n");
			serialPutchar(fd,3);
			colorHEX;
			LEDLastState = LEDState;

			switch(LEDState) {
				case 0:
					led = LED_RED;
					rate = LED_SOLID;
					colorHEX = 1255000000;
				break;
				case 1:
					led = LED_RED;
					rate =LED_SLOW;
					colorHEX = 1255000000;
				break;
				case 2:
					led = LED_RED;
					rate = LED_FAST;
					colorHEX = 1255000000;
				break;
				case 3:
					led = LED_GREEN;
					rate = LED_SOLID;
					colorHEX = 1000255000;
				break;
				case 4:
					led = LED_GREEN;
					rate = LED_SLOW;
					colorHEX = 1000255000;
				break;
				case 5:
					led = LED_GREEN;
					rate = LED_FAST;
					colorHEX = 1000255000;
				break;
				case 6:
					led = LED_BLUE;
					rate = LED_SOLID;
					colorHEX = 1000000255;
				break;
				case 7:
					led = LED_BLUE;
					rate = LED_SLOW;
					colorHEX = 1000000255;
				break;
				case 8:
					led = LED_BLUE;
					rate = LED_FAST;
					colorHEX = 1000000255;
				break;
				case 9:
					led = LED_PURPLE;
					rate = LED_SOLID;
					colorHEX = 1128000128;
				break;
				case 10:
					led = LED_PURPLE;
					rate = LED_SLOW;
					colorHEX = 1128000128;
				break;
				case 11:
					led = LED_PURPLE;
					rate = LED_FAST;
					colorHEX = 1128000128;
				break;
				case 12:
					led = LED_YELLOW;
					rate = LED_SOLID;
					colorHEX = 1255255000;
				break;
				case 13:
					led = LED_YELLOW;
					rate = LED_SLOW;
					colorHEX = 1255255000;
				break;
				case 14:
					led = LED_YELLOW;
					rate = LED_FAST;
					colorHEX = 1255255000;
				break;
				// Error
				default:
					led = LED_RED;
					rate = LED_FAST;
					colorHEX = 1255000000;
			}
			serialPutchar(fd,2);
			serialPrintf(fd,"LED:%u\n",colorHEX);
			serialPutchar(fd,3);
			switch(rate) {
				// Solid
				case 0:
					serialPutchar(fd,2);
					serialPrintf(fd,"LEDRATE:%u\n",0);
					serialPutchar(fd,3);
				break;
				// Slow Blink
				case 1:
					serialPutchar(fd,2);
					serialPrintf(fd,"LEDRATE:%u\n",LED_SLOW_RATE);
					serialPutchar(fd,3);
				break;
				// Fast Blink
				case 2:
					serialPutchar(fd,2);
					serialPrintf(fd,"LEDRATE:%u\n",LED_FAST_RATE);
					serialPutchar(fd,3);
				break;
				// Error
				default:
					serialPutchar(fd,2);
					serialPrintf(fd,"LEDRATE:%u\n",LED_FAST_RATE);
					serialPutchar(fd,3);
			}
			
		}
		if(LEDCommunication == 0) {
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

	// Initializing wiringPi
	wiringPiSetup();

	// Initializing Serial Communication 
	fd = serialOpen("/dev/ttyAMA0", BANDRATE);
	if(fd < 0) {
		fprintf (stderr, "Unable to open serial device: %s\n", strerror (errno)) ;
	}

	// Initializing MySQL
	conn = mysql_init(NULL);

  	if (conn == NULL) {
		fprintf(stderr, "mysql_init() failed\n");
	}

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
	//int fd;
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

void finish_with_error(MYSQL *conn) {
	fprintf(stderr, "%s\n", mysql_error(conn));
	mysql_close(conn);        
}

int authenticateRFIDToken(void) {
	#define STRING_SIZE 50

	#define AUTHENTICATION_CALL "CALL device_authentication(?,?,?,?)"

	short database_authenticated;

	printf("Authenticating the RFID Token with user database...\n");
  
	conn = mysql_init(NULL);

  	if (conn == NULL) {
		fprintf(stderr, "mysql_init() failed\n");
		return 0;
	}

	if (mysql_real_connect(conn, server, user, password, database, 0, NULL, 0) == NULL) {
    	finish_with_error(conn);
    	return 0;
  	}

  	MYSQL_STMT    	*stmt;
	MYSQL_BIND    	call_bind[4];
	MYSQL_BIND 		result_bind[1];
	MYSQL_RES     	*prepare_meta_result;
	MYSQL_TIME    	ts;

	/* CALL Variables */
	int           	param_count;
	int 			mac_address_data, device_id_data;
	char 			device_type_data[STRING_SIZE], RFID_token_data[STRING_SIZE];
	unsigned long 	device_type_length, RFID_token_length;
	/* Return Variables */
	int 			column_count, row_count,num_fields;
	unsigned long 	length[1];
	my_bool       	is_null[1];
	my_bool       	error[1];
	short 			authenticated_data, results_status; 			

	/* Prepare a SELECT query to fetch data from test_table */
	stmt = mysql_stmt_init(conn);
	if (!stmt) {
		fprintf(stderr, " mysql_stmt_init(), out of memory\n");
		return 0;
	}

	if (mysql_stmt_prepare(stmt, AUTHENTICATION_CALL, strlen(AUTHENTICATION_CALL))) {
		fprintf(stderr, " mysql_stmt_prepare(), CALL failed\n");
		fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
		return 0;
	}
	fprintf(stdout, " prepare, CALL successful\n");

	/* Get the parameter count from the statement */
	param_count= mysql_stmt_param_count(stmt);
	fprintf(stdout, " total parameters in CALL: %d\n", param_count);

	if (param_count != 4) {/* validate parameter count */
		fprintf(stderr, " invalid parameter count returned by MySQL\n");
		return 0;
	}

	/* Bind the data for all 4 parameters */

	memset(call_bind, 0, sizeof(call_bind));

	/* Readers Mac Address - INTEGER PARAM */
	/* This is a number type, so there is no need
	   to specify buffer_length */
	call_bind[0].buffer_type= MYSQL_TYPE_LONG;
	call_bind[0].buffer= (char *)&mac_address_data;
	call_bind[0].is_null= 0;
	call_bind[0].length= 0;

	/* Device ID - INTEGER PARAM */
	/* This is a number type, so there is no need
	   to specify buffer_length */
	call_bind[1].buffer_type= MYSQL_TYPE_LONG;
	call_bind[1].buffer= (char *)&device_id_data;
	call_bind[1].is_null= 0;
	call_bind[1].length= 0;

	/* Device Type - STRING PARAM */
	call_bind[2].buffer_type= MYSQL_TYPE_STRING;
	call_bind[2].buffer= (char *)device_type_data;
	call_bind[2].buffer_length= STRING_SIZE;
	call_bind[2].is_null= 0;
	call_bind[2].length= &device_type_length;

	/* RFID Token - STRING PARAM */
	call_bind[3].buffer_type= MYSQL_TYPE_STRING;
	call_bind[3].buffer= (char *)RFID_token_data;
	call_bind[3].buffer_length= STRING_SIZE;
	call_bind[3].is_null= 0;
	call_bind[3].length= &RFID_token_length;

	/* Bind the buffers */
	if (mysql_stmt_bind_param(stmt, call_bind)) {
		fprintf(stderr, " mysql_stmt_bind_param() failed\n");
		fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
		return 0;
	}
	
	/* Specify the data values */
	mac_address_data = 123456;             /* integer */
	device_id_data = equipmentID;		/* integer */
	strncpy(device_type_data, "equipment", STRING_SIZE); /* string  */
	device_type_length = strlen(device_type_data);
	strncpy(RFID_token_data, RFIDToken, STRING_SIZE); /* string  */
	RFID_token_length = strlen(RFID_token_data);

	/* Execute the CALL query */
	if (mysql_stmt_execute(stmt)) {
	  fprintf(stderr, " mysql_stmt_execute(), failed\n");
	  fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
	  return 0;
	}

	/* process results until there are no more */
	do {

		
		num_fields = mysql_stmt_field_count(stmt);
		/* Get total columns in the query */
		
		if(num_fields > 0) {
			

				/* Fetch result set meta information */
			prepare_meta_result = mysql_stmt_result_metadata(stmt);
			if (!prepare_meta_result) {
				fprintf(stderr, " mysql_stmt_result_metadata(), returned no meta information\n");
				fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
				return 0;
			}

			column_count= mysql_num_fields(prepare_meta_result);
			fprintf(stdout, " total columns in CALL statement: %d\n",column_count);

			if (column_count != 1) {/* validate column count */
				fprintf(stderr, " invalid column count returned by MySQL\n");
				return 0;
			}

			/* Bind the result buffers for all 4 columns before fetching them */

			memset(result_bind, 0, sizeof(result_bind));

			/* Authenticated - SMALLINT COLUMN */
			result_bind[0].buffer_type= MYSQL_TYPE_SHORT;
			result_bind[0].buffer= (char *)&authenticated_data;
			result_bind[0].is_null= &is_null[0];
			result_bind[0].length= &length[0];
			result_bind[0].error= &error[0];
			
			/* Bind the result buffers */
			if (mysql_stmt_bind_result(stmt, result_bind)) {
				fprintf(stderr, " mysql_stmt_bind_result() failed\n");
				fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
				return 0;
			}

			/* Now buffer all results */
			if (mysql_stmt_store_result(stmt)) {
			  fprintf(stderr, " mysql_stmt_store_result() failed\n");
			  fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
			  return 0;
			}

			row_count= 0;
			fprintf(stdout, "Fetching results ...\n");
			while (!mysql_stmt_fetch(stmt))
			{ /* Could do this differently */
				row_count++;
				if(authenticated_data) {
		  			database_authenticated = 1;
		    		fprintf(stdout, "User is authorized to use this machine.\n");
		    		authenticated = 1;
					authorized = 1;
		  		} else {
		  			database_authenticated = 0;
		    		fprintf(stdout, "User is NOT authorized to use this machine.\n");
		    		authenticated = 1;
					authorized = 0;
		  		}
		  	}

		  	/* Validate rows fetched */
			fprintf(stdout, " total rows fetched: %d\n", row_count);
			if (row_count != 1) {
		  		fprintf(stderr, " MySQL failed to return all rows\n");
		  		return 0;
			}

			/* Free the prepared result metadata */
			mysql_free_result(prepare_meta_result);

		} else {
		    /* no columns = final status packet */
		    printf("End of procedure output\n");
		}
		/* more results? -1 = no, >0 = error, 0 = yes (keep looking) */
	  	results_status = mysql_stmt_next_result(stmt);
	} while (results_status == 0);

		/* Close the statement */
	if (mysql_stmt_close(stmt)) {
  		fprintf(stderr, " failed while closing the statement\n");
  		fprintf(stderr, " %s\n", mysql_stmt_error(stmt));
 		return 0;
	}

  	mysql_close(conn); 

	/*sleep(10);
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
	} */
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

	if(!(_reason == 'A')) {
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
	fprintf(stdout, "Warning: Power Disconnect Approaching\n");
	_disconnectWarning = millis() + DISCONNECT_WARNING;
		
	while(millis() < _disconnectWarning) {
		
		if(_reason == 'R' && !(IRInterruptState)) return 0;
		if(_reason == 'I' && !(powerRelayState)) return 0;
		if(IRInterruptState) {
			if(compareRFIDTokens(_reason)) {
				lastAuthenticated = millis();
				return 1;
			}
		}
	}
	
	powerDisconnect(_reason);

}

// Backup power? LED blink yellow 
// Tempering evident? LED blink red
// reservations? LED approaching time blink blue, during solid blue, approaching cancellation fast blink blue

// register code for display