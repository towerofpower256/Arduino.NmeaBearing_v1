/*
 * NMEA Bearing Reader v1
 * by David McDonald 24/01/2019
 * Someone wanted to be able to display HDT (true heading) / HDM (magnetic heading)
 * NMEA-0183 data to an LCD display so he could hook an Ardunio up to an NMEA-0183 network
 * and go from there.
 * 
 * This program & setup is designed to listen for HDT/HDM NMEA-0183 messages
 * on the hardware serial interface, and output them to the LCD display.
 * 
 * It'll also listen for a button press which will reset the remembered bearing.
 * Buttons are the ones on the Freetronics LCD Shield.
 */

#include <LiquidCrystal.h>
#include <Wire.h>

// Freetronics button stuff
#define BUTTON_ADC_PIN            A0  // A0 is the button ADC input
#define RIGHT_10BIT_ADC           0  // right
#define UP_10BIT_ADC            145  // up
#define DOWN_10BIT_ADC          329  // down
#define LEFT_10BIT_ADC          505  // left
#define SELECT_10BIT_ADC        741  // right
#define BUTTONHYSTERESIS         10  // hysteresis for valid button sensing window
//return values for ReadButtons()
#define BUTTON_NONE               0  // 
#define BUTTON_RIGHT              1  // 
#define BUTTON_UP                 2  // 
#define BUTTON_DOWN               3  // 
#define BUTTON_LEFT               4  // 
#define BUTTON_SELECT             5  // 

byte buttonJustPressed  = false;         //this will be true after a ReadButtons() call if triggered
byte buttonJustReleased = false;         //this will be true after a ReadButtons() call if triggered
byte buttonWas          = BUTTON_NONE;   //used by ReadButtons() for detection of button events

// Constants
const char MESSAGE_START_CHAR = '$'; // the character that the NMEA message should start with
const char MESSAGE_END_CHAR = '*';
const char SPACE_CHAR = ' ';
const char COMMA_CHAR = ',';

const unsigned int LCD_WIDTH = 16;
const unsigned int NMEA_PREFIX_MAX_LENGTH = 5;

const int READER_STATE_WAITING_START = 0;
const int READER_STATE_READING_PREFIX = 1;
const int READER_STATE_READING_CONTENT = 2;

// Messages
const char STARTUP_MSG[] = "Starting...";
const char READY_WAITING_MSG[] = "Awaiting input";

// Globals
String bearingMsg; // The bearing message
String nmeaPrefix; // The NMEA message prefix. SHould be 5 characters long, starting with '$' and ending with ','
unsigned int readerState; // The NMEA reader's state
bool resetButtonDown = false;


LiquidCrystal lcd( 8, 9, 4, 5, 6, 7); // Init the LCD object, with the default PINs

void setup() {
  //Serial
  Serial.begin(9600);
  Serial.flush();
  Serial.println(STARTUP_MSG);

  // LCD
  lcd.begin(LCD_WIDTH, 2); // Init, with correct dimensions (16 columns, 2 rows)
  lcd.clear();
  lcd.print(STARTUP_MSG);

  //Reserve memory for the message
  bearingMsg.reserve(LCD_WIDTH);
  nmeaPrefix.reserve(NMEA_PREFIX_MAX_LENGTH); //NMEA prefix are only 5 characters long tops

  // Set the reader state
  readerState = READER_STATE_WAITING_START;

  // Setup some button stuff
  pinMode( BUTTON_ADC_PIN, INPUT );         //ensure A0 is an input
  digitalWrite( BUTTON_ADC_PIN, LOW );      //ensure pullup is off on A0

  // Ready
  Serial.println("Ready!");
  Reset();
}

void loop() {
  // put your main code here, to run repeatedly:

  // Button stuff
  byte button = ReadButtons();
  if (button == BUTTON_SELECT && !resetButtonDown) {
    Serial.println("Reset button pressed");
    resetButtonDown = true;
    Reset();
  } else if (button != BUTTON_SELECT && resetButtonDown) {
    Serial.println("Reset button released");
    resetButtonDown = false;
  }
}

// Function to run whenever there's a serial event on the hardware serial RX
// Remember: Multiple bytes of data may be available
void serialEvent() {
  // While 
  while (Serial.available()) { // While there is data in the serial buffer
    char newChar = (char)Serial.read(); // Read the next char

    switch(readerState) {
      case READER_STATE_WAITING_START: // Waiting for the message start character
        if (newChar == MESSAGE_START_CHAR) {
          Serial.println("Got start char");

          // Reset buffers
          bearingMsg = String(); // Reset the bearing message
          nmeaPrefix = String();

          // Update the reader state
          readerState = READER_STATE_READING_PREFIX;  
        } else {
          //DEBUG
          Serial.print("Ignoring char: ");
          Serial.println(newChar, DEC);
        }
        break;
      case READER_STATE_READING_PREFIX:
        if (newChar == COMMA_CHAR) {
          // Hit the end of the prefix definition
          Serial.print("Got prefix end char: ");
          Serial.println(nmeaPrefix);

          // So, is the prefix one that we care about?
          // TODO: Fix this, kinda want it to be more flexible e.g. working from an aray of desired prefixes
          bool wantedPrefix = (nmeaPrefix.endsWith("HDT") || nmeaPrefix.endsWith("HDM"));

          if (wantedPrefix) {
            // This is a prefix that we want
            Serial.println("Prefix is wanted");
            readerState = READER_STATE_READING_CONTENT;
          } else {
            Serial.println("Prefix is not wanted");
            readerState = READER_STATE_WAITING_START;
          }
          
          break;
        }else if (nmeaPrefix.length() >= NMEA_PREFIX_MAX_LENGTH) {
          // Still reading NMEA prefix, but we've hit max length
          Serial.println("Hit max length for NMEA prefix, abandoning message");
          readerState = READER_STATE_WAITING_START;
        } else {
          nmeaPrefix += newChar;
        }
        break;
      case READER_STATE_READING_CONTENT:
        if (newChar == MESSAGE_END_CHAR) {
          // Got the message end char
          Serial.println("Got end char");

          // Reset the reader state
          readerState = READER_STATE_WAITING_START;
          
          UpdateLcd(); // Update the LCD with the completed message;
          break;
        } else if (bearingMsg.length() >= LCD_WIDTH) {
          // Have we hit the maximum width of the LCD? Ignore all other characters
          // This should be cleaner...
          Serial.println("Hit maximum message length");
          readerState = READER_STATE_WAITING_START;
          UpdateLcd();
          break;
        } else {
          bearingMsg += newChar;
        }
        
        break;
    }
    
  }
}

void UpdateLcd() {
  lcd.setCursor(0,0);
  byte length = lcd.print(bearingMsg);
  for (byte i = length; i < LCD_WIDTH; i++) lcd.write(SPACE_CHAR); // Pad the string so that it takes up the whole row
}

void Reset() {
  bearingMsg = String();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print(READY_WAITING_MSG);
}


byte ReadButtons()
{
   unsigned int buttonVoltage;
   byte button = BUTTON_NONE;   // return no button pressed if the below checks don't write to btn
   
   //read the button ADC pin voltage
   buttonVoltage = analogRead( BUTTON_ADC_PIN );
   //sense if the voltage falls within valid voltage windows
   if( buttonVoltage < ( RIGHT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_RIGHT;
   }
   else if(   buttonVoltage >= ( UP_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( UP_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_UP;
   }
   else if(   buttonVoltage >= ( DOWN_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( DOWN_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_DOWN;
   }
   else if(   buttonVoltage >= ( LEFT_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( LEFT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_LEFT;
   }
   else if(   buttonVoltage >= ( SELECT_10BIT_ADC - BUTTONHYSTERESIS )
           && buttonVoltage <= ( SELECT_10BIT_ADC + BUTTONHYSTERESIS ) )
   {
      button = BUTTON_SELECT;
   }
   //handle button flags for just pressed and just released events
   if( ( buttonWas == BUTTON_NONE ) && ( button != BUTTON_NONE ) )
   {
      //the button was just pressed, set buttonJustPressed, this can optionally be used to trigger a once-off action for a button press event
      //it's the duty of the receiver to clear these flags if it wants to detect a new button change event
      buttonJustPressed  = true;
      buttonJustReleased = false;
   }
   if( ( buttonWas != BUTTON_NONE ) && ( button == BUTTON_NONE ) )
   {
      buttonJustPressed  = false;
      buttonJustReleased = true;
   }
   
   //save the latest button value, for change event detection next time round
   buttonWas = button;
   
   return( button );
}
