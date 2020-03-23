This is the desktop counterpart of the Arduino code. It expects to run on the same machine where JMRI with
the web server module is running.

From command line arguments you can control the JMRI server address and the two throttles you want to map
to the two faders.

- `-h <hostname>` sets the host address where it should connect to, defaulting to `localhost`
- `-p <port>` is the target port, default `12080`
- `-a <address>` is the throttle address to map to fader A, default `50`
- `-b <address>` is the throttle address to map to fader B, default `60`

To compile and run the code set `CLASSPATH=$JMRI/lib/purejavacomm-1.0.1.jar:$JMRI/lib/jna-4.4.0.jar`
or the equivalent libraries that JMRI might upgrade to.
