//  Program: ArdCore EXPERIMENTAL ADSR ENVELOPE


//

//
//  I/O Usage:
//    Knob 1: Sustain
//    Knob 2: Release
//    Knob 3/Jack A2:   Attack / attack CV
//    Knob 4/Jack A3: Decay / decay CV
//    Digital Out 1: decay active (trigger delay)
//    Digital Out 2: master clock
//    Clock In:  Gate
//    Analog Out:ADSR OUT

//  Input Expander: unused
//  Output Expander:unused
//
//  Created:  FEB 2012 by Dan Snazelle
//  Modified  SEP 2015 Robert Efroymson
//  last adj  APR 2016 Robert Efroymson
//
//  ============================================================
//
//  License:
//
//  This software is licensed under the Creative Commons
//  "Attribution-NonCommercial license. This license allows you
//  to tweak and build upon the code for non-commercial purposes,
//  without the requirement to license derivative works on the
//  same terms. If you wish to use this (or derived) work for
//  commercial work, please contact 20 Objects LLC at our website
//  (www.20objects.com).
//
//  For more information on the Creative Commons CC BY-NC license,
//  visit http://creativecommons.org/licenses/
//
//

#define FALSE 0
#define TRUE 1
#define BOOL int

//  constants related to the Arduino Nano pin use
const int clkIn = 2;           // the digital (clock) input
const int digPin[2] = {3, 4};  // the digital output pins
const int pinOffset = 5;       // the first DAC pin (from 5-12)



int digState[2] = {LOW, LOW};
unsigned long currTime = 0;
unsigned long lastTime = 0;

BOOL lfoMode = FALSE;
BOOL waslfoMode = FALSE;

BOOL clkLight = FALSE;
BOOL releaseLock = TRUE;

int lastreleaseRaw = 0;
int clockLed = 1; // for slowing down rate to make blink visible

enum envMode {A, D, S, R, I}; // Attack, Decay, Sustain, Release, Idle
envMode currMode = I;
envMode lastMode = I;

enum clkMode {clkQuiet, clkRising, clkFalling};
volatile clkMode clkState = clkQuiet;

unsigned long clockInterval = 600; // a nice default to start with
float envVal = 0.0;
float sustain;
float decay;
unsigned long releaseRaw;
float releaseCooked;
float attack;

//  ==================== start of setup() ======================
void setup() {

  Serial.begin(9600);  // for debug only
  // set up the digital (clock) input
  pinMode(clkIn, INPUT);

  // set up the digital outputs
  for (int i = 0; i < 2; i++) {
    pinMode(digPin[i], OUTPUT);
    digitalWrite(digPin[i], LOW);
  }

  // set up the 8-bit DAC output pins
  for (int i = 0; i < 8; i++) {
    pinMode(pinOffset + i, OUTPUT);
    digitalWrite(pinOffset + i, LOW);
  }
  lastreleaseRaw = analogRead(1);
  // Note: Interrupt 0 is for pin 2 (clkIn)
  attachInterrupt(0, isr, CHANGE);  // to pick up both
}

//  ==================== start of loop() =======================

void loop()
{
  currTime = micros();

//   DEBUG
/* if (releaseRaw != lastreleaseRaw){
  Serial.print(releaseRaw);
  Serial.print ("  ");
   Serial.print (clockInterval);
  Serial.print ("  ");
} */
/*  if (lastMode != currMode) {
    Serial.print (currMode);
    if (currMode == R) {
      Serial.print ("  ");
      Serial.print (releaseCooked);
      Serial.print ("  ");
    } 
    lastMode = currMode;
  }  */
  if (currTime > lastTime + clockInterval || clkState != clkQuiet) {
    switch (clkState) {
      case clkQuiet: // Woken by timer
        lastTime = currTime;
   //    if (clockLed++ %50 == 0){
  //       clockLed = 1;
         if (!clkLight) {
           digitalWrite(digPin[1], HIGH);
           clkLight = TRUE;
         }
          else {
           digitalWrite(digPin[1], LOW);
           clkLight = FALSE;
          }
 //       }
        break;

      case clkRising:
        currMode = A;
        envVal = 0; // retrigger
        clkState = clkQuiet;
        break;

      case clkFalling:
        if (lfoMode)
          currMode = D;
        else
          currMode = R;
        clkState = clkQuiet;
        break;
    }

    if (envVal < 1) // sanitize envelope
      envVal = 1;

    if (envVal > 1023)
      envVal = 1023;
    dacOutput (((int) floor (envVal)) >> 2);

    releaseRaw = analogRead(1); // raw
    sustain = analogRead(0); // raw, matches env
    decay = pow(1.007 , (analogRead(3) + 1)); // wider range of times
    releaseCooked = pow(1.007 , releaseRaw + 1); // wider range of times
    attack = pow(1.007 , (analogRead(2) + 1)); // wider range of times

    if (!lfoMode)
      lastreleaseRaw = releaseRaw;

    if (lfoMode)
      waslfoMode = TRUE;
    else
      waslfoMode = FALSE;
      
    if (sustain < 1)
      lfoMode = TRUE;
    else
      lfoMode = FALSE;
    if (!lfoMode && waslfoMode) {
      currMode = I; // avoid locking in S
      releaseLock = TRUE;  // reset lock
    }

    if (lfoMode && abs (releaseRaw - lastreleaseRaw) > 20)  // require good twist
      releaseLock = FALSE;

    if (lfoMode && !releaseLock)
      clockInterval = releaseRaw * 150 +1; // *15 -14 gives a 30 sec max LFO, approx

    switch (currMode) {
      case A:
        envVal = (envVal + attack); //step up a notch
        digitalWrite(digPin[0], LOW);
        if (envVal >= 1023)  // go to full
          currMode = D; //switch to decay
        break;

      case D:
        envVal = envVal - decay;
        digitalWrite(digPin[0], HIGH);

        if (envVal <= sustain){ //stop at sustain level
          envVal = sustain;
          if (lfoMode)
            currMode = A;
          else
            currMode = S;
        }
        break;

      case S:
        digitalWrite(digPin[0], LOW);
        envVal = sustain;
        break;

      case R:
        digitalWrite(digPin[0], LOW);
        if (envVal < 4)
          currMode = I; // envelope finished
        else
          envVal = envVal - releaseCooked;
        break;

      case I:
        envVal = 0; // safety
        digitalWrite(digPin[0], LOW);
        if (lfoMode) // respond to knob
          currMode = A;
        break;

      default:  // should never happen
        currMode = I; // sanity
    }

  }
}

//  =================== convenience routines ===================

void isr()
{
  // Note: you don't want to spend a lot of time here, because
  // it interrupts the activity of the rest of your program.
  // In most cases, you just want to set a variable and get
  // out.

  if (digitalRead(2) == HIGH)
    clkState = clkRising;
  else
    clkState = clkFalling;
}

//  dacOutput(long) - deal with the DAC output
//  ------------------------------------------
void dacOutput(long v)
{
  int tmpVal = v;
  bitWrite(PORTD, 5, tmpVal & 1);
  bitWrite(PORTD, 6, (tmpVal & 2) > 0);
  bitWrite(PORTD, 7, (tmpVal & 4) > 0);
  bitWrite(PORTB, 0, (tmpVal & 8) > 0);
  bitWrite(PORTB, 1, (tmpVal & 16) > 0);
  bitWrite(PORTB, 2, (tmpVal & 32) > 0);
  bitWrite(PORTB, 3, (tmpVal & 64) > 0);
  bitWrite(PORTB, 4, (tmpVal & 128) > 0);


  // debug
  //Serial.print ("currTime  ");
  //Serial.print (currTime);
  //Serial.print ("mode  ");
  //if (lastMode != currMode){
  //Serial.print (currMode);
  //if (currMode == R){
  //  Serial.print ("  ");
  //  Serial.print (release);
  //  Serial.print ("  ");
  //}
  //lastMode = currMode;
  //}

}
