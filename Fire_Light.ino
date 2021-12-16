
//#define DEBUG
#include <EEPROM.h>

// ATTINY85 pinout here
int ledPin = 0; // 0
int photoPin = 1; // 1
int buttonPin = 4; // 4

int minTransitionTimeMillis = 100;
int maxTransitionMillis = 500;

int minBrightness = 25;
int maxBrightness = 255;

int minLightStateDelayMillis = 250;
int maxLightStateDelayMillis = 500;

int lightMeasurements[10];
int ambientLightThreshold = 250;
byte currentMeasurement = 0;
byte maxMeasurementsLightAnimation = 10;
byte maxMeasurementsLightBulb = 2;
unsigned long measurementDelayLightAnimationMillis = 10000; // 10 sesonds
unsigned long measurementDelayLightBulbMillis = 900000; // 15 minutes
unsigned long measurementDelayStartMillis = 0;

byte currentBrightnessValue = 0;
byte currentLightFrom = 255;
byte currentLightTo;
unsigned long currentLightTransition;

enum state {
  idleState,
  turnOnAnimation,
  lightAnimation,
  lightBulb,
  turnOffAnimation
};

enum substate {
  idleSubstate,
  transitionInProcess,
  transitionEnded
};

state currentState;
state savedMode = lightAnimation; // can be only: lightAnimation, lightBulb
state isrMode = lightAnimation;
substate currentSubstate;
#ifdef DEBUG
  state previousState;
  substate previousSubstate;
#endif
unsigned long currentTimeMillis;
unsigned long timerStartValueMillis;

#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

void setup() {
  #ifdef DEBUG
    Serial.begin(9600);
  #endif
  
  currentState = turnOnAnimation;
  currentSubstate = idleSubstate;
  savedMode = loadMode();
  isrMode = savedMode;
  
  pinMode(ledPin, OUTPUT);
  analogWrite(ledPin, 0);
  
  pinMode(photoPin, INPUT);
  
  pinMode(buttonPin, INPUT);
  sbi(GIMSK,PCIE); // Turn on Pin Change interrupt
  sbi(PCMSK,PCINT4); // Which pins are affected by the interrupt
}

state loadMode() {
  state value = EEPROM.read(0);
  if (value != lightAnimation && 
      value != lightBulb) {
    value = lightAnimation;
    EEPROM.update(0, lightAnimation);
  }

  return value;
}

state saveMode(state currentMode) {
  if (currentMode != lightAnimation && 
      currentMode != lightBulb) {
    currentMode = lightAnimation;
  }
  
  EEPROM.update(0, currentMode);
}

void loop() {
  #ifdef DEBUG
    if (currentState != previousState) {
      previousState = currentState;
      Serial.println();
      Serial.println("==========================");
      Serial.print("Current State: ");
      Serial.println(currentState);
    }

    if (currentSubstate != previousSubstate) {
      previousSubstate = currentSubstate;
      Serial.print("Substate: ");
      Serial.println(currentSubstate);
    }
  #endif
  
  currentTimeMillis = millis();
  
  switch (currentState) {
    case turnOnAnimation: 
      doTurnOnAnimation();
      break;
    case lightAnimation:
      doLightAnimation();
      doMeasureAmbientLight();
      break;
    case lightBulb:
      doLightBulb();
      doMeasureAmbientLight();
      break;
    case turnOffAnimation:
      doTurnOffAnimation();
      break;
    default:
      doMeasureAmbientLight();
      break;
  }

  if (isrMode != savedMode) {
    saveMode(isrMode);
    savedMode = isrMode;

    // Blinking for confirmation;
    for (int i = 0; i <= 5; i++) {
      setBrightness(0);
      delay(80);
      setBrightness(255);
      delay(80);
    }

    setBrightness(currentBrightnessValue);

    switch (currentState) {
      case lightAnimation:
        currentState = lightBulb;
        currentSubstate = idleSubstate;
        break;
      case lightBulb:
        currentState = lightAnimation;
        currentSubstate = idleSubstate;
        break;
    }
    
    sbi(GIMSK, PCIE); // Enabling interrupt again
  }
}

ISR(PCINT0_vect) {
  GIMSK = 0; // Turn Off Pin Change interrupt
  if (digitalRead(buttonPin) != LOW) { 
    sbi(GIMSK, PCIE); // Turn on Pin Change interrupt
    return; 
  }

  switch (savedMode) {
    case lightAnimation:
      isrMode = lightBulb;
      break;
    case lightBulb:
      isrMode = lightAnimation;
      break;
  }
  
  //sbi(GIMSK, PCIE); // Turn on interrupt after the saveMode function
}

void doMeasureAmbientLight() {
  if (currentState == lightAnimation && 
       currentBrightnessValue > 50) {
    return;
  }
  
  unsigned long elapsedTimeMillis = currentTimeMillis - measurementDelayStartMillis;
  if ((currentState == lightAnimation || currentState == idleState) &&
      elapsedTimeMillis < measurementDelayLightAnimationMillis) {
    return;
  }

  if (currentState == lightBulb &&
      elapsedTimeMillis < measurementDelayLightBulbMillis) {
    return;
  }

  byte maxMeasurements;
  switch (currentState) {
    case lightAnimation:
      maxMeasurements = maxMeasurementsLightAnimation;
      break;
    case lightBulb:
      maxMeasurements = maxMeasurementsLightBulb;
      break;
    default:
      maxMeasurements = maxMeasurementsLightAnimation; // 10
      break;
  }
  
  if (currentMeasurement > maxMeasurements - 1) {
    currentMeasurement = 0;

    unsigned long sum = 0;
    for (byte i = 0; i <= maxMeasurements - 1; i++)
    {
      sum += lightMeasurements[i];
    }

    unsigned long average = sum / maxMeasurements;
    
    // Turn Off light
    if ((currentState == lightAnimation || currentState == lightBulb) && 
         average > (ambientLightThreshold + 50)) {
      currentSubstate = idleSubstate;
      currentState = turnOffAnimation;
    }

    // Turn On light
    if (currentState == idleState && average < ambientLightThreshold) {
      currentSubstate = idleSubstate;
      currentState = turnOnAnimation;
    }
  
    return;
  }

  if (currentState == lightAnimation || 
      currentState == lightBulb) {
    setBrightness(5);
    delay(100);
  }
  
  int currentLightSensorValue = analogRead(photoPin);
  
  if (currentState == lightAnimation || 
      currentState == lightBulb) {
    setBrightness(currentBrightnessValue);
  }
  
  #ifdef DEBUG    
    Serial.print("#");
    Serial.print(currentMeasurement);
    Serial.print(" Light Sensor value: ");
    Serial.println(currentLightSensorValue);
    delay(100);
  #endif

  lightMeasurements[currentMeasurement] = currentLightSensorValue;
  currentMeasurement++;
  if (currentMeasurement <= maxMeasurements - 1) {
    measurementDelayStartMillis = millis();
  }
}

void doLightAnimation() {
  switch (currentSubstate) {
    case transitionInProcess:
      doBrightnessTransition(currentLightFrom, currentLightTo, currentLightTransition);
      break;
    case transitionEnded:
      currentLightFrom = currentLightTo;
      if (currentLightTo > 200) { 
        delay(random(minLightStateDelayMillis, maxLightStateDelayMillis)); 
      }
      currentSubstate = idleSubstate;
      break;
    default:
      currentLightTransition = random(minTransitionTimeMillis, maxTransitionMillis);
      currentLightTo = random(minBrightness, maxBrightness);
      doBrightnessTransition(currentLightFrom, currentLightTo, currentLightTransition);
      break;
  } 
}

void doLightBulb() {
  currentBrightnessValue = 255;
  setBrightness(currentBrightnessValue);
}

void doTurnOnAnimation() {
  switch (currentSubstate) {
    case transitionInProcess:
      doBrightnessTransition(0, 255, 10000);
      break;
    case transitionEnded:
      currentBrightnessValue = 255;
      currentState = savedMode; // next state
      currentSubstate = idleSubstate;
      break;
    default:
      doBrightnessTransition(0, 255, 10000);
      break;
  }
}

void doTurnOffAnimation() {
  switch (currentSubstate) {
    case transitionInProcess:
      doBrightnessTransition(255, 0, 10000);
      break;
    case transitionEnded:
      currentBrightnessValue = 0;
      currentState = idleState; // next state
      currentSubstate = idleSubstate;
      break;
    default:     
      doBrightnessTransition(255, 0, 10000);
      break;
  }
}

void doBrightnessTransition(byte from, byte to, unsigned long durationMillis) {  
  if (currentSubstate != transitionInProcess) {    
    currentSubstate = transitionInProcess;
    timerStartValueMillis = millis();
  }

  unsigned long elapsedTimeMillis = currentTimeMillis - timerStartValueMillis;
  
  if (elapsedTimeMillis >= durationMillis) {
    setBrightness(to);
    currentSubstate = transitionEnded;
    return;
  }
  
  currentBrightnessValue = map(elapsedTimeMillis, 
                               0, 
                               durationMillis, 
                               from, 
                               to);
                             
  setBrightness(currentBrightnessValue);
}

void setBrightness(byte value) {
  analogWrite(ledPin, value);
}
