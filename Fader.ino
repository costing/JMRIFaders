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

int absolute(int relative) {
  if (relative == 127 || relative == 128)
    return 511;

  int ret = round(relative * 3.75);

  if (relative > 128)
    ret += 66;

  return ret;
}

int relative(int absolute) {
  if (absolute <= 476)
    return round(absolute / 3.75);

  if (absolute >= 548)
    return round((absolute - 66) / 3.75);

  return 128;
}

struct Slider {
  int currentPos = -1;

  int dirPin;
  int speedPin;
  int posPin;
  int brakePin;
  char channelName;

  boolean wasTouched = false;

  Slider::Slider(int dir, int speed, int pos, int brake, char name) {
    dirPin = dir;
    speedPin = speed;
    posPin = pos;
    brakePin = brake;
    channelName = name;

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

    // target position in absolute values
    const int target = absolute(newPos);

    // how far are we from the target position
    int delta = analogRead(posPin) - target;

    if (abs(delta) < 2) {
      // weird, we were already there, remember the position for next iteration
      currentPos = newPos;
      return;
    }

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
        digitalWrite(dirPin, delta > 0 ? 1 : 0);
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
          analogWrite(speedPin, map(adelta, 0, limit, highSpeed ? 0 : 110, 255));
        }

        oldADelta = adelta;

        // don't insist for too long, bail out if we can't reach the target position in a reasonable time
        if (++cnt > 500)
          break;
      }

      digitalWrite(brakePin, HIGH);
      digitalWrite(speedPin, LOW);
      digitalWrite(dirPin, LOW);

      if (cnt == 0 ) {
        if (wasZero) {
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

    return relative(analogRead(posPin));
  }

  int lastReportedValue = -1;
  long lastReportedTime = -1;

  void touch() {
    currentPos = readPos();

    if (currentPos != lastReportedValue || abs(millis() - lastReportedTime) > 500) {
      Serial.print('{');
      Serial.print(channelName);
      Serial.print(':');
      Serial.print(currentPos);
      Serial.println('}');

      lastReportedValue = currentPos;
      lastReportedTime = millis();
    }

    if (currentPos == 127 || currentPos == 128) {
      // try to bring the slider to the center and keep it there
      const int delta = analogRead(posPin) - 511;

      const int adelta = abs(delta);

      if (adelta > 2) {
        digitalWrite(dirPin, delta > 0 ? 1 : 0);
        analogWrite(speedPin, map(adelta, 0, 40, 130, 100));
        wasTouched = true;
      }
      else {
        digitalWrite(speedPin, LOW);
      }
    }
    else {
      digitalWrite(speedPin, LOW);
    }
  }

  void release() {
    if (wasTouched) {
      digitalWrite(dirPin, LOW);
      digitalWrite(speedPin, LOW);
      wasTouched = false;

      const int pos = readPos();

      if (pos == 127 || pos == 128) {
        currentPos = -1;
        setPos(128);
      }
    }
  }
};

// Position reading and setting wrappers for the two faders
Slider sliderA = Slider(DIRA, SPEEDA, SLIDERA, BRAKEA, 'A');
Slider sliderB = Slider(DIRB, SPEEDB, SLIDERB, BRAKEB, 'B');

// Capacitive sensors to detect when a finger is placed on the fader
CapacitiveSensor cs_4_2 = CapacitiveSensor(4, 2);
CapacitiveSensor cs_4_6 = CapacitiveSensor(4, 6);

void reset() {
  cs_4_2.reset_CS_AutoCal();
  cs_4_6.reset_CS_AutoCal();
  
  sliderA.currentPos = -1;
  sliderB.currentPos = -1;
}

void version() {
  Serial.println("{Fader:v1}");
}

void setup() {
  Serial.begin(115200);

  // faster than default PWM for smoother motor control
  TCCR2B = TCCR2B & B11111000 | B00000001;

  cs_4_2.set_CS_Timeout_Millis(200);
  cs_4_6.set_CS_Timeout_Millis(200);

  reset();
}

// new setting received from JMRI for one of the faders
int newValue = 0;

void loop() {
  long total1 =  cs_4_6.capacitiveSensor(SAMPLES);

  boolean ignoreA;

  if (total1 > TOUCHSENSE) {
    // When channel A is touched it becomes the master and sends the new position to JMRI
    sliderA.touch();
    ignoreA = true;
  }
  else {
    sliderA.release();
    ignoreA = false;
  }

  total1 = cs_4_2.capacitiveSensor(SAMPLES);

  boolean ignoreB;

  if (total1 > TOUCHSENSE) {
    // When channel B is touched it becomes the master and sends the new position to JMRI
    sliderB.touch();
    ignoreB = true;
  }
  else {
    sliderB.release();
    ignoreB = false;
  }

  if (Serial.available() > 0) {
    // New position is received from JMRI as a "<position><channel>" string
    const int newByte = Serial.read();

    if (newByte == 'r') {
      reset();
    }
    else if (newByte == 'v') {
      version();
    }
    else if (newByte >= '0' && newByte <= '9')
      newValue = newValue * 10 + newByte - '0';
    else {
      if (newByte == 'a' || newByte == 'A') {
        if (!ignoreA)
          sliderA.setPos(newValue);
      }
      else if (newByte == 'b' || newByte == 'B') {
        if (!ignoreB)
          sliderB.setPos(newValue);
      }

      newValue = 0;
    }
  }
}
