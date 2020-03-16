# JMRIFaders
Motorized fader board for JMRI throttles

The project uses a couple of Yamaha LS9/01V96 motorized faders (rebranded ALPS Apline [RSA0N11M9A0J](https://tech.alpsalpine.com/prod/e/html/potentiometer/slidepotentiometers/rsn1m/rsa0n11m9a0j.html)) as JMRI throttles. 

The code here is for an Arduino Nano + a Nano motor shield that
- set the position as received from JMRI for each fader (when controlled from the computer screen or another connected device)
- read the changed position and send it back to JMRI, when the slider knob is touched

For touch sensing the project makes use of the [CapacitiveSensing](https://playground.arduino.cc/Main/CapacitiveSensor/) library, with 1M resistors connected to the sensing pin.
