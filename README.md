# ESP8266 Mains Switch Controller Program
An ESP8266 mains switch control program. 
Features in brief:

- WIFI, MQTT with both automatic, schedules reconnects. No network flooding if MQTT- Server down.
- WEB - GUI for configuration, statistics and state display
- Firmware Update via OTA ("Over the Air" i.e. WIFI)
- Temperature,  humidity sensors supported:  SI7021, DHTx, DS20X (OneWire)
- NTP timer with automatic re-sychronization
- Full MQTT support. See WIKI for datapoints of mains switch, temperature and humidity.
- Interal temperature measure to switch off or raise an alarm if the housing gets too hot.
- External touch switch: switch manually if you're to lazy to apply your smartphone.
- Status LED: shows state of switch by LED
- Emergency mode:  if MQTT and/or WIFI is down, start an own internal 24h scheduler according to 
  the last remembered switch times and follow them until MQTT/WIFI is  present again.
- Manual timer: A simple 24h- time relay. (Yes, that can be done cheaper 
 with a simple mechanical clock switch)

This project contains the software only. You're free to implement any hardware you like.
Find some description and a picture of my hardware implementation in the WIKI.

