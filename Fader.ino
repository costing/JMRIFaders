#include <CapacitiveSensor.h>

// Motor shield ping mappings
#define DIRA 12
#define DIRB 13

#define SPEEDA 3
#define SPEEDB 11

#define CURRENTA A0
#define CURRENTB A1

#define BRAKEA 9
#define BRAKEB 8

// Which ADC pins read the fader position
#define SLIDERA A6
#define SLIDERB A7

// Source pin for CapacitiveSensor, common for both faders
#define SRC_SENSOR_PIN 4

// CapacitiveSensor destination pin for fader A
#define DST_SENSOR_A 2

// CapacitiveSensor destination pin for fader A
#define DST_SENSOR_B 6

// How many samples to collect from each fader. Default is 30 in the examples, 10 is faster and stable enough
#define SAMPLES 10

// Threshold value above which the sensor is definitely touched
#define TOUCHSENSE 100

struct Slider {
  int currentPos = -1;

  int dirPin;
  int speedPin;
  int posPin;
  int brakePin;

  Slider::Slider(int dir, int speed, int pos, int brake) {
    dirPin = dir;
    speedPin = speed;
    posPin = pos;
    brakePin = brake;

    pinMode(dirPin, OUTPUT);
    pinMode(speedPin, OUTPUT);
    pinMode(posPin, INPUT);
    pinMode(brakePin, OUTPUT);

    digitalWrite(dirPin, 0);
    digitalWrite(speedPin, 0);
    digitalWrite(brakePin, 0);

    setPos(0);
  }

  void Slider::setPos(int newPos) {
    // constrain the value to the acceptable range
    newPos = max(min(newPos, 255), 0);

    // was I already there
    if (newPos == currentPos)
      return;

    // how far are we from the target position
    int delta = readPos() - newPos;

    if (delta == 0) {
      // weird, we were already there, remember the position for next iteration
      currentPos = newPos;
      return;
    }

    // target position in absolute values
    int target = newPos * 4;

    // when the distance is larger than this, run at full speed. Below this distance, run slower
    const int limit = 900;

    int wasZero = 0;

    for (int i = 0; i < 3; i++) {
      int cnt = 0;

      digitalWrite(brakePin, LOW);

      int oldADelta = 0;

      int adelta;

      // The following algorithm tries to reach the target without overshooting too much
      // by starting at full speed then slowing down as it gets closer to the target.
      // Thresholds are experimentally set for the 12V power supply
      while ((adelta = abs(delta = analogRead(posPin) - target)) > 1) {
        // give the motor a full voltage to start moving
        digitalWrite(speedPin, 1);

        if (adelta < limit) {
          boolean highSpeed = false;

          if (adelta < 300) {
            if (oldADelta - adelta > 2) {
              // moving too fast, would overshoot the target position
              digitalWrite(brakePin, HIGH);
              highSpeed = true;
            }
            else
              digitalWrite(brakePin, LOW);
          }

          // after a short time of running at full speed to get the motor started, set speed by PWM
          delayMicroseconds(adelta > 200 ? 150 : 80);
          analogWrite(speedPin, map(adelta, 0, limit, highSpeed ? 0 : 100, 255));
        }

        digitalWrite(dirPin, delta > 0 ? 1 : 0);

        oldADelta = adelta;

        // don't insist for too long, bail out if we can't reach the target position in a reasonable time
        if (++cnt > 500)
          break;
      }

      digitalWrite(brakePin, HIGH);
      digitalWrite(speedPin, 0);

      if (cnt == 0 ) {
        if (wasZero){
          // was already in the target position, we're done
          break;
        }
        else
          wasZero = 1;
      }
      else
        wasZero = 0;

      delay(1);
    }

    currentPos = newPos;
  }

  int Slider::readPos() {
    // 256 values, 128 for each direction, same max number of speed steps as supported by loco decoders
    return analogRead(posPin) / 4;
  }
};

// Position reading and setting wrappers for the two faders
Slider sliderA = Slider(DIRA, SPEEDA, SLIDERA, BRAKEA);
Slider sliderB = Slider(DIRB, SPEEDB, SLIDERB, BRAKEB);

// Capacitive sensors to detect when a finger is placed on the fader
CapacitiveSensor cs_4_2 = CapacitiveSensor(4, 2);
CapacitiveSensor cs_4_6 = CapacitiveSensor(4, 6);

void setup() {
  Serial.begin(115200);

  // faster than default PWM for smoother motor control
  TCCR2B = TCCR2B & B11111000 | B00000001;

  cs_4_2.set_CS_Timeout_Millis(200);
  cs_4_6.set_CS_Timeout_Millis(200);

  // start at mid range -> neutral speed for the throttle
  sliderA.setPos(128);
  sliderB.setPos(128);
}

// new setting received from JMRI for one of the faders
int newValue = 0;

void loop() {
  long total1 =  cs_4_6.capacitiveSensor(SAMPLES);

  if (total1 > TOUCHSENSE) {
    // When channel A is touched it becomes the master and sends the new position to JMRI
    Serial.print("{A:");
    Serial.print(sliderA.readPos());
    Serial.println("}");
  }

  total1 = cs_4_2.capacitiveSensor(SAMPLES);

  if (total1 > TOUCHSENSE) {
    // When channel A is touched it becomes the master and sends the new position to JMRI
    Serial.print("{B:");
    Serial.print(sliderB.readPos());
    Serial.println("}");
  }

  if (Serial.available() > 0) {
    // New position is received from JMRI as a "<position><channel>" string
    int newByte = Serial.read();

    if (newByte >= '0' && newByte <= '9')
      newValue = newValue * 10 + newByte - '0';
    else {
      if (newByte == 'a' || newByte == 'A')
        sliderA.setPos(newValue);
      else if (newByte == 'b' || newByte == 'B')
        sliderB.setPos(newValue);

      newValue = 0;
    }
  }
}
