/*
 *  An(other) mains switch control program ! 
 *
 * 
 */

 /*
	add these libraries:
		Big credits go the devs of these libs !
 */ 
 
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Bounce2.h>					// https://playground.arduino.cc/Code/Bounce
#include <AsyncMqttClient.h>			// https://github.com/marvinroger/async-mqtt-client
#include <ESP8266HTTPUpdateServer.h> 	// https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266HTTPUpdateServer
#include <EEPROM.h>					 	// https://github.com/esp8266/Arduino/tree/master/libraries/EEPROM
#include <TimeLib.h>					// https://github.com/PaulStoffregen/Time
#include <NtpClientLib.h>	    		// https://github.com/gmag11/NtpClient
#include <TimeAlarms.h>					// https://github.com/PaulStoffregen/TimeAlarms
#include <DHT.h>

#include "Common.h"

// START DEFINES:data that must be adapted  ############################################################################

// Type of ESP
// #define ESP01
// #define SONOFF
#define ESP12


#ifdef ESP12
// gpio2 should be left open cause defines some boot mode:http://bbs.espressif.com/viewtopic.php?t=774
#define IN_PIN  		   5	// D1, gpio 5
#define OUT_PIN 		   D2	// D2, gpio 4

// the temp sensors. all may be run in parallel
// #define DHTPIN  		   12	// D6, gpio 12: define type further below: #define  DHTTYPE DHT22
#define ONE_WIRE_BUS_PIN   13	// DS18...  Sensor : D7, gpio 13
// #define SI7021ADDR 0x40 		// SI7021 I2C address is 0x40(64)

// gpio 0. during temp reading, lit an led. Stays on if error with sensor occured
// #define TEMP_READING_INDICATOR_PINOUT D3

#endif


// NEVER name a function init() !!!

#ifdef SI7021ADDR
#include <Wire.h>
#define SDA 12
#define SCL 13
#endif

#ifdef DHTPIN
DHT dht;

// #define DHTTYPE DHT11 // DHT 11
// #define DHTTYPE DHT21 // DHT 21 (AM2301)
#define   DHTTYPE DHT22  // Sensor type DHT11/21/22/AM2301/AM2302

#endif

const char* ssid     = "yourSSID";
const char* password = "yourPW";


// END OF DEFINES #############################################################################################

#ifdef ESP01

#define DHTPIN  2  	// gpio2: 	DHT21 / AMS2301
#define OUT_PIN 0	// gpio0	flash and output
#define IN_PIN  		DHTPIN
#define TEMP_READING_INDICATOR_PINOUT 	OUT_PIN	// lits the pin0 for a short time to indicate reading. stays on if sensor error


#endif

#ifdef SONOFF
#define OUT_PIN 12	// relay fixed: do not change !
#define OUT_LED 13
#define IN_PIN  0
#endif

#ifdef  ONE_WIRE_BUS_PIN

// wiring:http://iot-playground.com/blog/2-uncategorised/41-esp8266-ds18b20-temperature-sensor-arduino-ide
#include <OneWire.h> // https://github.com/milesburton/Arduino-Temperature-Control-Library
#include <DallasTemperature.h>

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature sensors(&oneWire); // Pass our oneWire reference to Dallas Temperature.
#endif

#if !(defined ESP01 || defined SONOFF) // too small for ota updater, even with 1M of flash
// http://www.esp8266.com/wiki/doku.php?id=ota-over-the-air-esp8266
ESP8266HTTPUpdateServer httpUpdater;
#endif

MDNSResponder mdns;
ESP8266WebServer server(80);
String webPage = "";

static WiFiEventHandler WiFiConnectHandler, WiFiDisConnectHandler;

#if !(defined ESP01 && defined (DHTPIN))	// esp01 cannot handle both
Bounce  bouncer = Bounce();
#endif

char 			localHostname [30]; // we do not trust the network. further processing with the one given

TempStatus  ts [ OFS_LAST ];
bool 		ntpInit;
AlarmId  	timerId_TempReading =-1;
boolean 	localMode			= false; // * true:  localMode ON ,  false: localMode OFF

// !!! important : init to -1 cause '0' is a valid offset into the tables and the below init will not start
// cause Alarm.isAllocated(timerId_MqttConnect ) )  returns true with init 0
AlarmId  	timerId_MqttConnect =-1;
AlarmId 	AlarmIdLocalSwitchTimes [2]; 	// remember the last time daily switched in case of mqtt server failure offsets same as lastSwitchTime

// MQTT -----------------------------------------------------
AsyncMqttClient mqttClient;
char 	subscribeTopic [sizeof(config.MqttId) + 10 ];
char 	pubTopicGPIO   [sizeof(config.MqttId) + 10 ]; // = {"/IN/D/1"} = 10 ;
const 	char * 			initTopic = "INIT"; // when the openhab (OH) server starts, OH sends this INIT to tell all client to publish their state to OH

String text_html; 	// to EE only at runtime


void EEPromWriteConfig (){
	EEPROM.begin(sizeof (Config));
	Serial.println("Writing EE " + String (sizeof (Config)) + " bytes");
	byte  * p = (byte *) & config;
	for (uint i  = 0; i < sizeof (Config); i++ ){
		EEPROM.write(i, *p++);
	}
	EEPROM.commit();
}

void EEPromReadConfig (){

	EEPROM.begin(sizeof (Config));

	byte  * p = (byte *) & config;
	Serial.println("Reading EE " + String (sizeof (Config)) + " bytes");

	for (uint i  = 0; i < sizeof (Config); i++ ){
		*p++ = EEPROM.read(i);
	}
}

void onWIFIConnectedGotIP(WiFiEventStationModeGotIP ipInfo) {

	Serial.println("Connected:IP:" + ipInfo.ip.toString() + ", WifiState:" + String (WiFi.status()));

	if ( strcmp(config.localIP, WiFi.localIP().toString().c_str())){
		// if different, save IP and write to ee.
		strcpy (config.localIP,  WiFi.localIP().toString().c_str());
		EEPromWriteConfig();
	}
	NTP.begin("fritz.box", 1, true); // or: NTP.begin("de.pool.ntp.org", 1, true);
	NTP.setInterval(3600);	 // 1hr	, 1 day 86400

	strncpy (localHostname, WiFi.hostname().c_str(), min (sizeof (localHostname), strlen(WiFi.hostname().c_str() )) );

	server.begin(); 			// Start the server
	Serial.write("Server started. Hostname:<");
	Serial.write(localHostname);
	Serial.write(">, IP<");
	Serial.write (config.localIP);
	Serial.write (">\n");

	if (!mdns.begin("esp8266", WiFi.localIP()))
		Serial.println(F("MDNS responder ERROR"));
#if !(defined ESP01 || defined SONOFF)
	httpUpdater.setup(&server);
	Serial.println("HTTPUpdateServer on http://" + WiFi.localIP().toString() + "/update");
#endif
	server.begin();
	MDNS.addService("http", "tcp", 80);
	mqtt_Timed_Connect();	// init the mqtt connection setup directly. donn't use timers as we're connected
}


// Manage network disconnection

void onWifiDisconnected(WiFiEventStationModeDisconnected event_info) {

	Serial.println("WLAN Disconnected from SSID " + event_info.ssid +", Reason:" + String ( event_info.reason ));
	// NTP.stop(); // not needed, re-establishes standalone
	ntpInit 	= false;
	switchLocalMode	(true);
	if (Alarm.isAllocated ( timerId_MqttConnect ) ) {	// without wifi it makes no sense to connect
		Alarm.free(timerId_MqttConnect);
		Serial.println(F("Removed MQTT reconnect timer"));
	}
}

// NTP  ---------------------------------------------------------------------
boolean 		NtpSyncEventTriggered = false; 	// True if a time even has been triggered
NTPSyncEvent_t 	ntpEvent; 						// Last triggered event

void NTP_onNTPprocessSyncEvent(NTPSyncEvent_t ntpEvent) {

	// Serial.println("onNTPprocessSyncEvent");

	if (ntpEvent) {
		Serial.print("onNTPprocessSyncEvent:Time Sync error:");
		if (ntpEvent == noResponse)
			Serial.println("NTP server not reachable");
		else if (ntpEvent == invalidAddress)
			Serial.println("Invalid NTP server address");
	}
	else {
		Serial.println( "NTP time OK. Last Sync:" +  NTP.getTimeDateString(NTP.getLastNTPSync()));
		// NTP to timelib connection

		if ( ntpInit == false) {			// init flag
			// setTime(NTP.getTime());		// not needed cause done by next line
			setSyncProvider(NTP_getNTPTimer);	// time lib from NTP
			setSyncInterval(3600);			// 300 minimum
			Serial.println("NTP init ok.");
			ntpInit 	= true;
			switchLocalMode (false);
		}
	}
}

time_t NTP_getNTPTimer() {

	time_t t = NTP.getTime();
	Serial.println("NTP Sync called:" + NTP.getTimeDateString(t));
	return t;
}

void switchOn (){
	setState(1);
}
void switchOff (){
	setState(0);
}
void 		(*switchMethods			[2])(); // function pointers to on and off methods above

// here it all starts -----------
void setup() {

	Serial.begin(115200);
	delay(100);

	text_html = F("text/html");

	Serial.println("ESP from " + String (version) + " starting.");
	// os_printf("ESP from %s starting.\n", version);

	// initialize OUT_PIN as an output and switch it off
	pinMode 	 (OUT_PIN, OUTPUT);
	digitalWrite (OUT_PIN, LOW);

#ifdef TEMP_READING_INDICATOR_PINOUT
	pinMode 	 (TEMP_READING_INDICATOR_PINOUT, OUTPUT);
	digitalWrite (TEMP_READING_INDICATOR_PINOUT, LOW);
#endif
#ifdef ESP01
	pinMode 	 	 ( IN_PIN, INPUT_PULLUP); 	// ? must be set this way. Also with pullup resistor
#else
	pinMode 	 	 ( IN_PIN, INPUT); 	// mini: ok.  input pin
#endif

#ifdef SONOFF
	pinMode 	 (OUT_LED, OUTPUT);
	digitalWrite (OUT_LED, HIGH);	// OFF
#endif

	digitalWrite	 ( IN_PIN, HIGH); 	// Activate internal pull-up (optional)

#if !(defined ESP01 && defined (DHTPIN))
	bouncer .attach	 ( IN_PIN );   		// After setting up the button, setup the object
	bouncer .interval(10);				//  5 - 10 ms
#endif

	EEPromReadConfig();

	if (strcmp(config.magicKey, magicEEKey)) {
		Serial.println(F("Init EEProm"));
		memset(&config, 0, sizeof(config));
		config.readingIntervallTemp = 300;
		strcpy(config.magicKey, magicEEKey);
		EEPromWriteConfig();
	}

//	Serial.println("EEPROM:LastIP"   + String (config.localIP)
//			+ ";Temp-ReadIntervall:" + String (config.readingIntervallTemp)
//			+ ";MqttId:<" 			 + String (config.MqttId) + ">"
//			+ ";Mqtt-Server:<" 		 + String (config.mqttServer) + ">");

	printf ("EEPROM:LastIP %s; Temp-ReadIntervall:%d; MqttId:<%s>; Mqtt-Server:<%s>\n"
					,config.localIP, config.readingIntervallTemp, config.MqttId, config.mqttServer);

	mqtt_SetTopics();

	WiFiConnectHandler 		= WiFi.onStationModeGotIP		 ( onWIFIConnectedGotIP);// As soon WiFi is connected, start NTP Client
	WiFiDisConnectHandler 	= WiFi.onStationModeDisconnected ( onWifiDisconnected);

	WiFi.hostname	( config.hostname); // seems to work
	WiFi.mode		( WIFI_STA);	  	// important !! station only
	WiFi.begin		( ssid, password);
	delay(100);

	NTP.onNTPSyncEvent([](NTPSyncEvent_t event) {
		ntpEvent = event;
		NtpSyncEventTriggered = true;
	});

	// callbacks inits for mqtt

	mqttClient.onConnect	( mqttOn_Connect 	);
	mqttClient.onDisconnect	( mqttOn_Disconnect );
	mqttClient.onMessage	( mqttOn_Message 	);

	// the the callbacks for the various HTTP posts. (removed formerly inline code)
	server.on ("/", 		 serverHandle_Root	  	);
	server.on ("/configRsp", serverHandle_ConfigRsp );
	server.on ("/cmd", 		 serverHandle_Cmd		);
	server.on ("/gpio_cmd",  serverHandle_Gpio	  	);
	server.onNotFound (		 serverHandle_NotFound	);

	switchMethods [0] = switchOff;
	switchMethods [1] = switchOn;

#ifdef DHTPIN
	dht.setup(DHTPIN);
	Serial.println("DHT Detected Typ:" + String ( dht.getModel()));
#endif
#ifdef ONE_WIRE_BUS_PIN
	sensors.begin();
#endif

#ifdef SI7021ADDR
	Wire.begin(SDA, SCL);
	delay(300);
#endif

	webPage.reserve(2000);	// typically 1793

	timerInit();
}

void serverHandle_Root(){
	// Serial.println("Root called");
	updateWEBPage();
	server.send(200, text_html, webPage);
}

void serverHandle_Cmd(){

	if(server.arg("button").equals("reboot")){
		server.send(200, text_html, "Reboot ok.");
		ESP.restart();
	}
	else
		if(server.arg("button").equals("reset")){
			Serial.println("Resetting ..." );
			memset  ( &config, 0, sizeof(config));
			EEPromWriteConfig();
			server.send(200,text_html, F("Reset ok. EEProm cleared."));
			ESP.restart();
		}
		else
			server.send(200, text_html, "CMD not found for 'button':" + server.arg("button") );
}

void serverHandle_Gpio() {

	// if missing its off, otherwise on. see remarks at updateWebpage()

	if (server.arg("gpio").length() != 0)
		setState ( 1 );
	else
		setState ( 0 );

	updateWEBPage();
	server.send(200, text_html, webPage);

}

void serverHandle_NotFound() {

	String message = "Not Found. URI:" + server.uri();
	message += ", Method: "  + (server.method() == HTTP_GET)?"GET":"POST";
	message += "\nArguments:" + server.args() ;
	server.send(404, "text/plain", message);
	Serial.println(message);
	// dump args
	int l  = server.args();
	for (int i = 0; i< l ; i++) {
		Serial.println("NotFound Args:" + String (i) + ", argN:" + server.argName(i) + ", argV:" + server.arg(i) );
	}
}

void serverHandle_ConfigRsp (){

	int l  = server.args();
	for (int i = 0; i< l ; i++) {
		Serial.println("ConfigRsp:" + String (i) + ", argN:" + server.argName(i) + ", argV:" + server.arg(i) );
	}

#if (defined (DHTPIN) || defined (ONE_WIRE_BUS_PIN) || defined (SI7021ADDR))

	config.readingIntervallTemp = server.arg("tempIntervall").toInt();
	Alarm.free(timerId_TempReading);	// free
	timerId_TempReading = Alarm.timerRepeat(config.readingIntervallTemp, timedReadTemperatures );	// set new
#endif

	// if ( strcmp(config.MqttId, server.arg("devid").c_str() ) > 0 ){ TODO does not work
	Serial.println("MQTT DevId changed from <" + String (config.MqttId) + " > to " + server.arg("devid"));
	strcpy (config.MqttId, server.arg("devid").c_str());	// changes also the topics
	// mqttClient.disconnect();	// init a new connect in main loop ->
	//}

	// if ( strcmp(config.mqttServer, server.arg( "mqttServerName").c_str() ) > 0 ){	// TODO does not work
	Serial.println("MQTT Server name changed from <" + String (config.mqttServer) + " > to " + server.arg("mqttServerName"));
	strcpy (config.mqttServer, server.arg("mqttServerName").c_str());	// changes also the topics

	Serial.println("Hostname from:<" + String (config.hostname) + " > to " + server.arg("hostNameId"));
	strcpy (config.hostname, server.arg("hostNameId").c_str());	// changes also the topics

	// argV: true | false
	// https://arduino-hannover.de/2016/01/01/html-kochbuch-mit-esp8266-und-arduino-ide/
	// checkbox is only present if set and then its true

	config.allowLocalMode =  (server.arg  ("AllowLocalMode").length() != 0) ?  true : false;
	Serial.println("AllowLocalMode:" + String  (config.allowLocalMode ));

	// ON OFF times
	char cr [ 8 ] ;
	strcpy (cr, server.arg("DailyON").c_str());
	int hrOn ,  minOn , sekOn, hrOff, minOff, sekOff;
	hrOn =  minOn =  sekOn =  hrOff =  minOff=  sekOff =0;

	char* token  	 		= strtok(cr, ":");	// destroys cr, therefore copy
	if ( token) hrOn 		= atoi(token);
	token		 	 		= strtok(NULL, ":");
	if ( token)  minOn 		= atoi(token);
	token		 			= strtok(NULL, ":");
	if ( token)  sekOn 		= atoi(token);

	strcpy (cr, server.arg("DailyOFF").c_str());
	token 		= strtok(cr, ":");	// destroys cr, therefore copy
	if ( token) hrOff 	= atoi(token);
	token		= strtok(NULL, ":");
	if ( token) minOff 	= atoi(token);
	token		= strtok(NULL, ":");
	if ( token) sekOff 	= atoi(token);

	if (localMode) {
		if (Alarm.isAllocated(AlarmIdLocalSwitchTimes [0]))
			Alarm.free(Alarm.isAllocated(AlarmIdLocalSwitchTimes [0]));
		if (Alarm.isAllocated(AlarmIdLocalSwitchTimes [1]))
			Alarm.free(Alarm.isAllocated(AlarmIdLocalSwitchTimes [1]));

		AlarmIdLocalSwitchTimes [0] = Alarm.alarmRepeat (hrOff, minOff, sekOff, switchMethods [0]);
		AlarmIdLocalSwitchTimes [1] = Alarm.alarmRepeat (hrOn,  minOn, 	sekOn,  switchMethods [1]);

	}
	// store in any case
	tmElements_t tm;

	memset (&tm, 0,sizeof(tm));	// stack is always dirty
	breakTime(now(), tm);

	tm.Hour 	= hrOff;
	tm.Minute 	= minOff;
	tm.Second 	= sekOff;
	config.lastSwitchTime [ 0 ] = makeTime(tm);

	tm.Hour 	= hrOn;
	tm.Minute 	= minOn;
	tm.Second 	= sekOn;
	config.lastSwitchTime [ 1 ] = makeTime(tm);

	Serial.println("Dev Name:" + String (config.MqttId) + ", Temp reading intervall:" + String (config.readingIntervallTemp));
	EEPromWriteConfig		();
	mqtt_SetTopics	 		();
	mqttClient.disconnect	();	// inits an connect, calls ondisconnect()
	// no mqtt_Timer_Connect(), cause that is done in the onDisconnect
	updateWEBPage();

	server.sendHeader("Connection", "close");
	server.sendHeader("Access-Control-Allow-Origin", "*");
	server.send(200, text_html, webPage);

}
/*
 * switch to local mode or back
 */

void switchLocalMode( boolean newMode ){

	if (!config.allowLocalMode) {
		Serial.println(F("switchLocalMode:No Local Mode cause disabled"));
		return;
	}

	if (localMode == newMode)
		return;

	// if no valid time, it makes no sense to schedule something
	if (timeStatus() == timeNotSet) { // timeNeedsSync,
		// Serial.println("NTP Time not set. Cannot schedule local mode. Status:" + String (timeStatus()));
		return;
	}

	if ( newMode ) {	// switch to local mode and schedule a daily action
		tmElements_t tm;
		localMode 	= true;

		// get the last on/off hr, min and schedule a daily on/off

		for (int i =0;i <= 1 ; i++) { 		// loop the function ptr for switch on/off

			if (config.lastSwitchTime[ i ] > 0L) {

				breakTime( config.lastSwitchTime[i], tm);  // break time_t into elements stored in tm struct
				AlarmIdLocalSwitchTimes  [i] = Alarm.timerRepeat(tm.Hour, tm.Minute, tm.Second, switchMethods[i]);
				Serial.println("LocalMode Switch Time idx:" + String ( i==0 ? "OFF":"ON")  + ":" + NTP.getTimeDateString(Alarm.read(AlarmIdLocalSwitchTimes[i])));
			}
			else
				Serial.println("No last switch time found in idx:" + String (i));
		}
	}
	else{
		localMode = false;
		for (int i =0;i <= 1 ; i++) { 	// delete all timers
			if (Alarm.isAllocated ( AlarmIdLocalSwitchTimes [i]))	// defense programming
				Alarm.free 	  ( AlarmIdLocalSwitchTimes [i]);
			AlarmIdLocalSwitchTimes [i]  = -1;
		}
	}

	Serial.println("Local Mode switched " + String((localMode ? "ON":"OFF")));

}

// functions called by timer. Timer cannot store additional info. Therefore two methods are needed.
void timerInit(){ 		// start the timer

#if (defined (DHTPIN) || defined (ONE_WIRE_BUS_PIN) || defined (SI7021ADDR))
	timerId_TempReading  = Alarm.timerRepeat( config.readingIntervallTemp, timedReadTemperatures );
#endif

}

// for cmd
void updateWEBPage (){

	webPage   = F ("<html><head><h2>Steckdose Web Server </h2></head> <body>");
	webPage.concat("<h3>IP:"+ WiFi.localIP().toString());
	webPage.concat("<br>Hostname:" + String(localHostname));
	webPage.concat ("<br>MQTTId:"  + String (config.MqttId)  +", State:" + String (mqttClient.connected() ? "Connected" : "DISconnected"  ) + "</h3>");
	webPage.concat("Time: "     + NTP.getTimeDateString());
	webPage.concat("<br>Compile DateTime: " + String(version));
	webPage.concat("<br>FreeHeap:" + String (ESP.getFreeHeap()) +  ", ChipID:"  + String(ESP.getChipId()));

	TempStatusType * tsp = &ts [0];

	for (int i = 0 ; i < OFS_LAST; i++ , tsp++) {
		// Serial.println("SYSInfo: ofs: "+ String (i) + ",topic:" + tsp->MqttPubTopic + ";errCode:" + String(tsp->errCode));
		if(tsp->MqttPubTopic[0]){	// != 0 : if empty, not used
			if (tsp->errCode == 0){
				webPage.concat( "<br> topic:" + String (tsp->MqttPubTopic) +":");
				if ( tsp->tempValid)
					webPage.concat("Temperatur: " + String (tsp->temp,1) + "°C");
				if ( tsp->humValid)
					webPage.concat(", Feuchtigkeit:" + String (tsp->hum) + " %");
			}
			else
				webPage.concat (String (tsp->MqttPubTopic) + F(":Sensor Fehler"));
		}
	}

	// https://github.com/esp8266/Arduino/issues/1973
	// https://gist.github.com/bbx10/5a2885a700f30af75fc5
	// best: https://www.studiopieters.nl/arduino-web-server-control-a-led/

	// for bezieht sich auf id in der nächsten Zeile, name ist der Key der im Request gesendet wird.
	// GPIO Switch
	webPage.concat("<p><h3><FORM action=gpio_cmd method=post>"\
			"<b><label for=gpio_cb>Switch</label>"\
			"<input type=checkbox name=gpio id=gpio_cb value=1 onclick=submit(); "\
			+  String ( digitalRead ( OUT_PIN ) ? "checked" : "") + "></b>"\
			"</FORM></p></h3>");

	webPage.concat( "Local Mode State:" +  String ((localMode ? "ON" : "OFF")));

	// FORM
	webPage.concat("<h2>Configuration</h2>"\
			"<form action=configRsp method=post>"\

	// Hostname
			"<p><label for=hostNameId>Hostname (change requires reboot)</label>"\
			"<input id=hostNameId name=hostNameId type=text  maxlength=30  value=" + String(config.hostname) + ">"\
			"</p>");

	// MQTT Device ID
	webPage.concat("<p><label for=devid>Device ID (MQTT DevID)</label>"\
			"<input id=devName name=devid type=text  maxlength=19  value=" + String(config.MqttId) + ">"\
			"</p>");

	// MQTT Server
	webPage.concat("<label for=mqttServerName>MQTT Server Addr</label>"\
			"<input id=mqttServer name=mqttServerName type=text  maxlength=19  value=" + String(config.mqttServer) + ">:1899"\
			"<br>");
	// for bezieht sich auf id in der nächsten Zeile
	webPage.concat("<p><label for=AllowLocalMode>Allow Local Mode (If WIFI and/or MQTT down)</label>"\
			"<input type=checkbox  id=AllowLocalMode name=AllowLocalMode value=true " + String (config.allowLocalMode ? "checked" : "")
			+ "><br></p>");

	webPage.concat(F("<label for=LastOn>Local Mode: Daily ON</label>"));
	webPage.concat("<input type=time name=DailyON value=" + (NTP.getTimeStr(config.lastSwitchTime[1])) + ">");

	webPage.concat(F("<label for=LastOff>Daily OFF</label>"));
	webPage.concat("<input type=time name=DailyOFF value=" + (NTP.getTimeStr(config.lastSwitchTime[0])) + "><br>");

#if (defined (DHTPIN) || defined (ONE_WIRE_BUS_PIN) || defined (SI7021ADDR))
	webPage.concat("<label for=tempIntervall>Temperatur Reading Intervall secs </label>"\
			"<input id=temp name=tempIntervall type=number style=width:100px value=" + String(config.readingIntervallTemp) + ">"\
			"<br>");
#endif
	webPage.concat(F("<button name=task value=save>Save</button>"\
			"</form>"
			"<form action=cmd method=post>"\
			"<button name=button value=reboot>Reboot</button><br>"\
			"<button style=width:120px name=button value=reset>Reset Configuration</button><br>"\
			"</form>"\
			"</body></html>"));

	// Serial.println("WEB page size:" + String(webPage.length())); 		// -> 1743
}

// the loop function runs over and over again forever

void loop() {

#if !(defined ESP01 && defined (DHTPIN)) 	// if ESP01 and DHT sensor, then no input pin can be used.

	if ( bouncer.update()) {
		bouncer.read(); 			// not essential necessary, if we have rose/fell methods
		if ( bouncer.rose()) {		// only if pressed
			setState ( 2 );
		}
	}
#endif

	if (NtpSyncEventTriggered) {
		NTP_onNTPprocessSyncEvent(ntpEvent);
		NtpSyncEventTriggered = false;
	}

	server	.handleClient();
	Alarm	.delay(0);

}
/*
 * timer procedure to start temp reading. for DHT it only init, the rest is done in  read_DHT_Sensors ()
 */
#if defined (DHTPIN) || defined (ONE_WIRE_BUS_PIN) || defined  (SI7021ADDR)

void timedReadTemperatures(){

#ifdef TEMP_READING_INDICATOR_PINOUT
	digitalWrite(TEMP_READING_INDICATOR_PINOUT, HIGH); // signify reading; when reading fails, stays lit until a successfull reading
// 	delayMicroseconds(2500);	 		// let the power come up again , hopefully buffered by a  capac.
#endif

#ifdef ONE_WIRE_BUS_PIN
	sensor_OneWire(); // synchron reading
#endif

#ifdef SI7021ADDR
	sensor_SI7021(); 			// synchron reading
#endif

#if defined (DHTPIN)
	sensor_DHT();		// asynchron reading; start, rest asyncrhon above
#endif

	mqtt_PublishTemps();

	// reset LED if all fine
	// check if all successfully done:if one is not defined, the struct is clean = 0

#ifdef 	TEMP_READING_INDICATOR_PINOUT

	for (int i= 0; i < OFS_LAST ; i++ ){
		if (ts[i].MqttPubTopic[0] != 0){	// only if used
			if (ts[i].errCode == 0) {		// if no error
				delayMicroseconds(25000);
				digitalWrite(TEMP_READING_INDICATOR_PINOUT, LOW); 	// signify reading end
			}
		}
	}
#endif

}
#endif

boolean setState (int  s) {

	if  (s == 0 || ( s  == 1))
		digitalWrite(OUT_PIN, s);
	else  if (s  == 2)
		digitalWrite(OUT_PIN, !digitalRead(OUT_PIN) );
	else{
		Serial.println("setStateErr:" + String (s) );
		return false;
	}
#ifdef SONOFF	//
	digitalWrite(OUT_LED, !digitalRead(OUT_PIN));
#endif

	mqtt_PublishGPIOState ();
	Serial.println(NTP.getTimeStr() + ": New state: GPIO:" + String (OUT_PIN)+ ":" +  String (digitalRead(OUT_PIN) ? "ON" : "OFF" ) );

	// save the last switch time: NTP.getTime() causes a call to onNTPSync ... weird ...
	if ( config.allowLocalMode){
		config.lastSwitchTime [ digitalRead(OUT_PIN) ] = NTP.getTime();	// offset 0 = on = false, offset 1 = off = false
		EEPromWriteConfig();	// save for the next day
	}
	return true;
}

void mqtt_SetTopics(){

	mqttClient.setServer	( config.mqttServer, 1883);	// might be changed in between
	mqttClient.setKeepAlive ( 60 );
	mqttClient.setClientId	( config.MqttId);

	// set the subscription topics
	strcpy ( subscribeTopic	, config.MqttId);	// faster than sprintf
	strcat ( subscribeTopic ,"/OUT/#");

	strcpy ( pubTopicGPIO, config.MqttId );
	strcat ( pubTopicGPIO, "/IN/D/1");

	// temperature sensors
	memset(ts, 0, sizeof(ts));

// done rarely, therefore we can afford the cpu expensive sprintf
#ifdef DHTPIN
	sprintf (ts[OFS_DHT].MqttPubTopic, "%s/%s", config.MqttId,"TEMP/DHT");
	Serial.println ("DHT Topic:" + String(ts[OFS_DHT].MqttPubTopic));
#endif

#ifdef ONE_WIRE_BUS_PIN
	sprintf (ts[OFS_WIRE].MqttPubTopic, "%s/%s", config.MqttId,"TEMP/DS");
	Serial.println ("1WIRE Topic:" + String(ts[OFS_WIRE].MqttPubTopic));
#endif

#ifdef SI7021ADDR
	sprintf (ts[OFS_SI702].MqttPubTopic, "%s/%s", config.MqttId,"TEMP/SI");
	Serial.println ("SI7021 Topic:" + String(ts[OFS_SI702].MqttPubTopic));
#endif
}

/** MQTT:
 * ----------------------------------------------------------------------
 */
void  mqttOn_Connect (bool sessionPresent){

	Serial.println("MQTT connected");
	// set the subscription topics

	Serial.print("subscribeTopic:<");
	Serial.print(subscribeTopic);
	// subscriptions are announced to the server. so if connection fails, it must be renewed
	if ( mqttClient.subscribe  ( subscribeTopic, 0) ){
		mqttClient.subscribe   ( initTopic, 	 0);
		Serial.println(">OK");
	}
	else
		Serial.println(">ERROR");

	// announce our presence
	char  buf [  sizeof (config.localIP)  + sizeof (localHostname) + sizeof (config.MqttId) + 4];
	sprintf (buf, "%s;%s;%s", config.localIP, localHostname, config.MqttId);
	mqtt_Publish("ANNNOUNCE", buf); // in return Openhab could send an INIT to this device.

	// mqttPublishGPIOState(); 				// announce init state. later should be handled by openhab
	// timedReadTemperatures();
	switchLocalMode(false);  				// switch to remote mode

	Alarm.free(timerId_MqttConnect);	// works although this is the timer service routine
	Serial.println(F("Removed MQTT reconnect timer"));
}

/*
 *
 */
void   mqttOn_Disconnect(AsyncMqttClientDisconnectReason reason) {

	printf ("MQTT disconnected. Reason:%d\n", (int) reason);

	mqtt_StartConnectTimer(); 	// disconnected,start the reconnect timer.
}

void 	mqtt_StartConnectTimer(){

	if (config.mqttServer[0] == 0 || (config.mqttServer[0] != 0  && strlen(config.mqttServer) ==0))	// not - yet - configured
		return;

	if (!Alarm.isAllocated(timerId_MqttConnect ) ) {						// if !already scheduled
		timerId_MqttConnect = Alarm.timerRepeat(10, mqtt_Timed_Connect );	// start a reconnect every 10 secs
		Serial.println	(F("MQTT scheduled reconnect."));
		switchLocalMode (true); 							// switch to local mode
	}
}

/*
   	 Both used as the timer service routine and as init routine onReceivalofIP. That allows
  	  for faster connect directly after receiving an IP and thereby not starting/waiting for a timer.

  	  Q: what happens if that fails ? is mqttOn_Disconnect() called ?

 */

void mqtt_Timed_Connect() {

	// Serial.println("timed_MqttConnect: Trying to connect."); //to <" +  String(config.mqttServer) +">");
	// might  be changed in between
	mqttClient.connect ();	// ASYNC call, returns nothing.
}


void mqtt_Publish (const char * topic, const char * payload) {

	if (mqttClient.connected ()) {
		Serial.println (NTP.getTimeStr() + ":MqttPub:<" + String(topic) + ">,msg:<" + String(payload) +">");
		mqttClient.publish	( topic , 0, true , payload);
	}
}

void mqtt_PublishGPIOState (){

	char r [2] ;
	r[0]= digitalRead(OUT_PIN) + 0x30;	// fast ascii conversion
	r[1]= 0;
	mqtt_Publish (pubTopicGPIO, r);
}


void mqtt_PublishErrorState ( char const * msg1, char const  * msg2 ){
	char errmsg [50];
	sprintf (errmsg, "%s-%s,<%s>" ,config.MqttId, msg1, msg2);
	Serial.write ( errmsg);

	mqtt_Publish ( "ERR", errmsg);	// 'ERR': fixed topic in openhab
}


void mqttOn_Message(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total){

	char pl [10] ;
	memset(pl, 0, sizeof (pl));

	Serial.print("MQTT rcv msg topic:<");
	Serial.print(topic);
	Serial.print(">,payload:<");

	for (uint i=0;i<len;i++) {
			Serial.print((char)payload[i]);
			pl[i] = payload[i];
		}
	Serial.println(">");

	if ( len > sizeof(pl)){
		mqtt_PublishErrorState ("Error: Payload too long: Max 10", (const char *) payload);
		return;
	}


	// is it an init command from openhab ?
	if (strcmp(topic, initTopic) == 0 ){
		mqtt_PublishGPIOState	();
		mqtt_PublishTemps		();
		return;
	}

	int state = atoi (pl);

	if ( setState ( state ) == false) {
		Serial.println("State change err:"+ String (state) + ";payload:<" + String(pl) + ">");
		mqtt_PublishErrorState ("Unsupp. state change:", (const char *) pl);
	}
}

void mqtt_PublishTemps (  ) {

	char 	sensorData4MQTT [40];				// Preformatted buffer for MQTT
	struct TempStatusType  * t = & ts [ 0 ]; 	// ptr is faster

	for (int i= 0; i < OFS_LAST ; i++, t++){

		if (t->MqttPubTopic[0] != 0){	// only if used

			if (t->errCode == 0){

				if (t->tempValid == true)
					sprintf (sensorData4MQTT, "%d.%d;", (int) t->temp, (int)(t->temp * 10.0) % 10) ;

				if (t->humValid == true)
					// sprintf ( &sensorData4MQTT[strlen (sensorData4MQTT) ], "%d", (int)t->hum) ;
					itoa ((int)t->hum, &sensorData4MQTT[strlen (sensorData4MQTT) ], 10);
				mqtt_Publish (t->MqttPubTopic, sensorData4MQTT);	// send to mqtt
			}
			else
			{
				char err [5];
				itoa(t->errCode, err, 10);
				mqtt_PublishErrorState(t->errMsg, err);
			}
		}
	}
}

// TEMP Sensor routines -----------------------------------------------------------------------------------------

#ifdef SI7021ADDR

void sensor_SI7021() {

	unsigned int 	data[2];
	float humidity	=	-1;
	float temp 		= -127.;
	TempStatusType *t = &ts[OFS_SI702];	// ptr is faster

	t->readingstarted = true;	// note essential as this is all synchron, but for completeness

	Wire.beginTransmission(SI7021ADDR); //Send humidity measurement command
	Wire.write(0xF5);
	Wire.endTransmission();
	delay(500);

	// Request 2 bytes of data
	Wire.requestFrom(SI7021ADDR, 2);

	if(Wire.available() == 2) 	// Read 2 bytes of data to get humidity
	{
		data[0] = Wire.read();
		data[1] = Wire.read();
		// Convert the data
		humidity  = ((data[0] * 256.0) + data[1]);
		t->hum = ((125 * humidity) / 65536.0) - 6;
		t->humValid 	= true;

		Wire.beginTransmission(SI7021ADDR); // Send temperature measurement command
		Wire.write(0xF3);
		Wire.endTransmission();
		delay(500);

		// Request 2 bytes of data
		Wire.requestFrom(SI7021ADDR, 2);

		if(Wire.available() == 2) {

			data[0] = Wire.read(); 	// Read 2 bytes of data for temperature
			data[1] = Wire.read();

			// Convert the data
			temp  		= ((data[0] * 256.0) + data[1]);
			t->temp 	= ((175.72 * temp) / 65536.0) - 46.85;
			t->errCode 	= 0;
			t->errMsg[0]= '\0';
			t->tempValid = true;
			// Serial.println ("Temp:"+ String(temperatur,1) + " C; Hum:" + String(humidity,0) + " %");
		}
	}
	else {
		t->errCode 	 = -1;
		t->tempValid = t->humValid 	= false;
		strcpy(t->errMsg, "SI7021 Sensor lost" );
	}
	t->readingstarted = false;
	// mqtt_PublishTemps( t  );

}
#endif

#ifdef ONE_WIRE_BUS_PIN

// https://forum.arduino.cc/index.php?topic=403229.0
// Serial.println((__FlashStringHelper *) komment[ i ] );

void sensor_OneWire(){

	TempStatusType *t = &ts[OFS_WIRE];	// ptr is faster

	sensors.requestTemperatures(); // Send the command to get temperatures
	//	Serial.print("DS Temp:");
	//	Serial.println(sensors.getTempCByIndex(0),1);

	if (sensors.getTempCByIndex(0) <= -127.0) {
		t->errCode 	= 1;
		strcpy (t->errMsg, "DS Sensor Error" );
		t->tempValid = false;
	}
	else{
		t->temp 		 = sensors.getTempCByIndex(0);
		t->errCode 	 = 0;
		t->errMsg[0] ='\0';
		t->tempValid = true;
		t->hum 		 = -1;
	}

}
#endif

#ifdef DHTPIN

void sensor_DHT(){

	TempStatusType * t = &ts[OFS_DHT];

	float hum  	= dht.getHumidity	();
	float temp	= dht.getTemperature();

	DHT::DHT_ERROR_t status  = dht.getStatus(); 		// get DHT status

	// Serial.println("DHT-State:" + String ( status ));

	if (status == DHT::ERROR_NONE) {
		t->hum 		 = hum;
		t->temp		 = temp;
		t->tempValid = t->humValid 	= true;
		t->errCode 	 = t->errMsg[0] = 0;
	}
	else {
		t->errCode 	 	= status;
		t->tempValid 	= t->humValid 	= false;
		strcpy(t->errMsg,"DHT Sensor Err:");
		strcat(t->errMsg, dht.getStatusString());

	}
}
#endif
