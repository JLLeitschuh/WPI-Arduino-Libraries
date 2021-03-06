/*
 * This sketch is sample code that implements a 'robot' for the RBE 2001 final project.
 * The specific purpose of it is to provide a baseline level of Bluetooth connectivity for
 * a team in communicating with the field control computer.
 *
 * This code demonstrates the sending and receiving of sample reactor protocol packets.
 * Not all possible packets are shown in this code.
 *
 * After power up or reset, the robot should only receives messages. It should not be
 * sending any messages until the 'Go' pushbutton has been pressed.  This is to make it
 * easier for the master Bluetooth module to pair up with the slave module. Once the
 * pairing has been completed, then the user may press the Go button.  That should allow
 * the robot to begin sending messages to the field control computer as well as allowing
 * it to begin moving around the playing field.
 *
 * Originally written by Benzun on 15th September 2012
 * Extensively modified by C. Putnam, August, 2013
 *
 * Modified by:
 *   C. Putnam, 2/7/14: Added requirement for external 10K pullups on the RX1 & TX1 lines
 *                    : General cleanup of code
 *
 *  C. Putnam, 9/15/14: Fixed the robot addressing problem
 *
 * Bluetooth-related circuitry:
 * - BT RX pin to digital pin 18 (TX1)
 * - BT TX pin to digital pin 19 (RX1)
 *
 * Spent fuel rod circuitry:
 * - Storage tube 1 LED to digital pin 46
 * - Storage tube 2 LED to digital pin 48
 * - Storage tube 3 LED to digital pin 50
 * - Storage tube 4 LED to digital pin 52
 *
 * Supply fuel rod circuitry:
 * - Supply tube 1 LED to digital pin 47
 * - Supply tube 2 LED to digital pin 49
 * - Supply tube 3 LED to digital pin 51
 * - Supply tube 4 LED to digital pin 53
 * 
 * Other I/O pins
 * - 'Go' LED on digital pin 2
 * - 'Go' pushbutton on digital pin 3
 */

// necessary include files
#include <BluetoothClient.h>
#include <BluetoothMaster.h>
#include <ReactorProtocol.h>
#include <TimerOne.h>

//various useful definitions
#define thisROBOT 21                           // <== CHANGE TO MATCH YOUR TEAM NUMBER!!!

#define goLED       2                          // pin for LED to indicate the robot is now OK'd to send data
#define goSW        3                          // pin for momentary pushbutton to initiate sending of data

#define onboardLED 13
#define spareLED   21                          // extra LED for <<whatever>>

#define storLED1  46                           // lights if storage tube bit 1 is set
#define storLED2  48                           // lights if storage tube bit 2 is set
#define storLED4  50                           // lights if storage tube bit 4 is set
#define storLED8  52                           // lights if storage tube bit 8 is set

#define splyLED1  47                           // lights if supply tube bit 1 is set
#define splyLED2  49                           // lights if supply tube bit 2 is set
#define splyLED4  51                           // lights if supply tube bit 4 is set
#define splyLED8  53                           // lights if supply tube bit 8 is set

// instantiate needed objects
ReactorProtocol pcol(byte(thisROBOT));         // instantiate the protocol object and set the robot/team source address
BluetoothClient bt;                            // instantiate a Bluetooth client object
BluetoothMaster btmaster;                      // ...and a master object

// set up module-wide variables
// these are 'volatile' as they are referenced in as well as outside of ISRs
volatile unsigned char tickCount;              // elapsed time in ticks (timer interrupts)
volatile unsigned long elapsedTime;            // ...and in seconds
volatile unsigned char hbCount;                // ticks since last heartbeat message was sent
volatile boolean sendHB;                       // flag indicating it is time to send a heartbeat message
volatile boolean go;                           // flag indicating it is OK to start transmitting messages

// these need to be module-wide variables (so they persist across iterations of loop()
byte storageData;                              // holds the bitmask for the storage tubes
byte supplyData;                               // ... and the supply tubes

/*
 * setup() runs once on power-up or reset
 */
void setup() {
  // configure the console comm port to 115200 baud
  Serial.begin(115200);
  
  // the slave bluetooth module is also configured to 115200 baud
  Serial1.begin(115200);
  
  // init various variables and flags
  go = false;
  tickCount = 0;
  hbCount = 0;
  elapsedTime = 0;
  sendHB = false;

  // set up the onboard and 'spare' LEDs and init them to the off state
  pinMode(onboardLED, OUTPUT);
  digitalWrite(onboardLED, LOW);
  pinMode(spareLED, OUTPUT);
  digitalWrite(spareLED, LOW);
  
  // set up the received packet data LEDs and init them to the off state
  pinMode(splyLED1, OUTPUT);                   // set the I/O pin to output
  digitalWrite(splyLED1, LOW);                 // ...and turn the LED off
  pinMode(splyLED2, OUTPUT);
  digitalWrite(splyLED2, LOW);
  pinMode(splyLED4, OUTPUT);
  digitalWrite(splyLED4, LOW);
  pinMode(splyLED8, OUTPUT);
  digitalWrite(splyLED8, LOW);
  pinMode(storLED1, OUTPUT);
  digitalWrite(storLED1, LOW);
  pinMode(storLED2, OUTPUT);
  digitalWrite(storLED2, LOW);
  pinMode(storLED4, OUTPUT);
  digitalWrite(storLED4, LOW);
  pinMode(storLED8, OUTPUT);
  digitalWrite(storLED8, LOW);
  
  // set up the GO button, the GO LED, and the external interrupt for the button
  pinMode(goLED, OUTPUT);                      // set the I/O pin to output
  digitalWrite(goLED, LOW);                    // ...and turn the LED off
  pinMode(goSW, INPUT_PULLUP);                 // init the external input line and enable the internal pullup
  attachInterrupt(1, extint_1ISR, FALLING);    // look for a falling edge to trigger external interrupt 1
 
  // set up the timer (it starts automatically)
  Timer1.initialize(100000);	               // set up a 100 millisecond timer period
  Timer1.attachInterrupt(timer1ISR);           // ...and specify the timer ISR
}  // end setup


/*
 * ISR for external interrupt 1 (a.k.a. the 'GO' button)
 */
void extint_1ISR(void) {
  // we are here because the GO button was pressed
  go = true;                                   // note that the button was pressed
  digitalWrite(goLED, HIGH);                   // ...and light the indicator LED
}  // end extint_1ISR


/*
 * ISR for timer1
 */
void timer1ISR(void) {
  // we are here because the timer expired and generated an interrupt
  tickCount++;                                 // increment the 100ms tick counter
  hbCount++;                                   // increment the heartbeat counter
  if (tickCount == 10) {                       // do the following once a second
    tickCount = 0;                             // reset the tick counter
    elapsedTime++;			       // increment the elapsed time counter (in seconds)
  }
  if (hbCount == 20) {                         // do the following every other second
    hbCount = 0;                               // reset the heartbeat counter
    sendHB = true;                             // note it is time to send a heartbeat message
  }
} // end timer1ISR


/*
 * main loop - this is executed repeatedly after setup() is done
 */
void loop() {
  byte pkt[10];                                // allocate memory for the bytes in the packet
  int sz;                                      // holds the size of the message (in bytes)
  byte type;                                   // hold the message type id
  byte data1[3];                               // holds any data associated with a message

  if (sendHB && go) {                          // execute if GO flag is set and it's time to generate a heartbeat message
    sendHB = false;                            // clear the heartbeat flag

    // generate and send the heartbeat message    
    digitalWrite(onboardLED, !digitalRead(onboardLED));  // flip the state of the HB LED
    pcol.setDst(0x00);			       // this will be a broadcast message
    sz = pcol.createPkt(0x07, data1, pkt);     // create a packet using the heartbeat type ID (there is no data)
    btmaster.sendPkt(pkt, sz);                 // send to the field computer

    // example of sending a radiation alert message (new fuel rod)
    delay(20);                                 // wait a little while so we don't spam the field control computer
    pcol.setDst(0x00);			       // this will be a broadcast message
    data1[0] = 0xFF;                           // indicate a new fuel rod
    sz = pcol.createPkt(0x03, data1, pkt);     // create a packet using the radiation alert type ID (1 byte of data used this time)
    btmaster.sendPkt(pkt, sz);                 // send to the field computer
  }
  
  // time to read messages
  // each time through loop() we check to see if a message is present

  // attempt to read a message (packet)
  // the only messages returned are those that are broadcast or sent specifically to this robot
  if (btmaster.readPacket(pkt)) {              // if we have received a message
    if (pcol.getData(pkt, data1, type)) {      // see if we can extract the type and data
      switch (type) {                          // process the message based on the type
      case 0x01:                               // received a storage tube message
        storageData = data1[0];                // extract and save the storage-related data (the byte bitmask)
        break;
      case 0x02:                               // received a supply tube message
        supplyData = data1[0];                 // extract and save the supply-related data (the byte bitmask)
        break;
      default:                                 // ignore other types of messages
        break;
      }

      // update the supply-related LEDs
      digitalWrite(splyLED1, supplyData & 0x01);  // light the LED if the corresponding bitmask bit is set
      digitalWrite(splyLED2, supplyData & 0x02);
      digitalWrite(splyLED4, supplyData & 0x04);
      digitalWrite(splyLED8, supplyData & 0x08);
          
      // update the storage-related LEDs
      digitalWrite(storLED1, storageData & 0x01); // light the LED if the corresponding bitmask bit is set
      digitalWrite(storLED2, storageData & 0x02);
      digitalWrite(storLED4, storageData & 0x04);
      digitalWrite(storLED8, storageData & 0x08);
    }
  }
}  // end main

