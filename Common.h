/*
 * Common.h
 *
 *  Created on: 21.03.2017
 *      Author: gsi
 */

#ifndef COMMON_H_
#define COMMON_H_

const char version[] PROGMEM =  __DATE__ " " __TIME__;

char magicEEKey[] ={"1Q3bJ"}; 	// no magic: just the sign, that we have initialised the EE. Its very unlikely that the EE is delivered with this char seq.

struct Config {

	char 	magicKey 			[sizeof (magicEEKey)];
	char 	localIP 			[ 16 ] ;
	int 	readingIntervallTemp = 60;	// in secs, not #ifdef 'ed to keep config common
	char 	name				[ 20 ]; // name or id of this device. freely set by http:// ... /name?name=Steckdose
	char 	MqttId				[ 20 ];
	char 	mqttServer			[ 20 ];
	int     resetTimeMaxMinHr	;		// hh:mm:ss. easier to display
	int     resetTimeMaxMinMin	;
	time_t 	lastSwitchTime 		[ 2 ]; 	// offset 0 = OFF , offset 1 = ON  (= state of OUT_PIN)
	boolean allowLocalMode	;
	char    hostname 			[30 ];
}
config;

enum TEMP_SENS_OFS {			// ofsets into ts below
	OFS_DHT  =0,
	OFS_WIRE =1,
	OFS_SI702=2,
	OFS_LAST =3					// dummy to fill loops and dim fields
} TEMP_SENS_OFS;

typedef struct TempStatusType {

	char  MqttPubTopic [ sizeof (config.name) + 10 ] ; // the topic for MQTT publishing. Also serves as presence indicator of sensor

	float temp;
	bool  tempValid;
	float tempMax, tempMin;
	char  displayTemp [20];

	int	  hum;					// hum with decimals makes no sense
	bool  humValid;
	int   humMax, humMin;
	char  displayHum  [20];	// what to show
	char  errMsg 	  [30];
	int   errCode;				// 0 =ok

}TempStatus;




#endif /* COMMON_H_ */
