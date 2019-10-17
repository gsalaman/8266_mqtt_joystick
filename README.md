# 8266_mqtt_joystick

## Overview
We need the following pieces:
- configurable connection to the net (eg, dawsonIOT)
- configurable connection to the MQTT broker
- i2c connection to the joystick itself.

Looks like the 8266 "remembers" the last connection...so I'm gonna write a quick "connect" app that the 8266 runs (via serial monitor) to set SSID and Password...that way that info is *not* being stored in Github. 

That app will also configure the broker...and looks like we can store that (as four numbers) in EEPROM.  Need to play with that library. 

We'll also need a unique client name...also in EEPROM.

Note I can make this app as part of the actually joystick program...if you have a serial connection, you can set SSID, Password, or MQTT address.  Otherwise, it runs outta what was set there previously.

With the MQTT handshake, we'll have the following states:
1) Startup.  Tries to connect to the last SSID/Password.
2) Broker connect.  Trying to connect to the broker.
3) Game registration.  Probably start by sending a dereg (to get rid of any zombie clients), followed by sending a registration.  
4) "connected".  Monitor the joystick i2c, and send the appropriate "up/down/left/right" commands to our passed "player".

Probably good to have a status LED so the joystick knows which state it's in.  Could do an RGB...red for (1), yellow for (2), blue for (3) and green for (4).
