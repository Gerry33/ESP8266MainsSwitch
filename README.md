# ESP8266 Mains Switch Controller Program
An ESP8266 mains switch control program. 
Features in brief:

- WIFI, MQTT with both automatic, schedules reconnects. No network flooding if MQTT- Server down.
- WEB - GUI with statistics 
- Firmware Update via OTA ("Over the Air" i.e. WIFI)
- Temperature,  humidity sensors supported:  SI7021, DHTx, DS20X (OneWire)
- NTP timer with automatic re-sychronization
- Full MQTT support. See WIKI for datapoints of mains switch, temperature and humidity.
- Interal temperature measure to switch off or raise an alarm if the housing gets too hot.
- External touch switch: switch manually if you're to lazy to apply your smartphone.
- Status LED: shows state of switch by LED
- Emergency mode:  if MQTT and/or WIFI is down, start an own internal schedule according to 
  the last switch times and follows them until MQTT/WIFI are present again.
- Manual timer: if no MQTT Server present, you just have a simple 24h- time relay.

This program contains the software only. You're free to implement any hardware you like

Find some description and a picture of my hardware implementation in the WIKI. 
