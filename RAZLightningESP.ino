//RAZLightning.ino v3.0 01/07/2020
//Placed on GITHUB Aug. 1 2018
//By R.J. de Kok - (c) 2019

#include "SparkFun_AS3935.h"
#include "EEPROM.h"
#include "WiFi.h"
#include "Wire.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <MQTT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

#define CE                   15
#define DC                    2
#define RST                  10
#define LED					 4
#define lineHeight			 8
#define LINECLR          0xF800
#define GRFCLR           0x07FF
#define startPix              5
#define AS3935_intPin		 25

#define AS3935_ADDR 		0x03 
#define INDOOR 				0x12 
#define OUTDOOR 			0xE
#define LIGHTNING_INT 		0x08
#define DISTURBER_INT 		0x04
#define NOISE_INT 			0x01

#define BUTTON               34 // Bij smalle ESP32 36
#define BEEPER               32

#define beepOn               1
#define beepOff              0
#define btnNone              0
#define btnRight             1
#define btnDown              2
#define btnLeft              3
#define btnUp                4

#define FROMTIME             0
#define FROMMENU             1
#define FROMLIGHTNING        2
#define FROMNOTHING          3

#define wdtTimeout          60
#define dispStat             0
#define dispHist             1
#define dispTijd             2
#define dispLogo             3
#define dispInfo		     4
#define dispMinute           5
#define dispHour             6
#define dispDay              7
#define dispMax              7

#define offsetEEPROM       0x10
#define EEPROM_SIZE        200

struct StoreStruct {  
	byte chkDigit;
	char ESP_SSID[16];
	char ESP_PASS[27];
	char MyCall[10];
	char mqtt_broker[50];
	char mqtt_user[25];
	char mqtt_pass[25];
	int mqtt_port;
	int domoticzDevice;
	byte AS3935_doorMode;
	byte AS3935_distMode;
	byte AS3935_capacitance;
	byte AS3935_noiseFloorLvl;
	byte AS3935_watchdogThreshold;
	byte AS3935_spikeRejection;
	byte AS3935_lightningThresh;
	byte beeperCnt;
	byte timeCorrection;
	byte dispScreen;
};

StoreStruct storage = {
		'%',
		"MARODEKWiFi",
		"MAROWiFi19052004!",
		"PA2RDK",
		"mqtt.rjdekok.nl",
		"Robert",
		"Mosq5495!",
		1883,
		235,
		0, //indoor
		1, //enabled
		96,
		6,
		7,
		8,
		9,
		2,
		0,
		0
};

char receivedString[128];
char chkGS[3] = "GS";

byte minutes[60];
byte maxMinute = 0;
byte divMinute = 1;

byte hours[48];
byte maxHour = 0;
byte divHour = 1;

byte days[30];
byte maxDay = 0;
byte divDay = 1;

uint16_t pulses = 0;
uint16_t distPulses = 0;
byte minuteBeeped = 0;
int majorVersion=3;
int minorVersion=1; //Eerste uitlevering 15/11/2017
bool hbSend = 0;
int updCounter = 0;

struct histData {
	byte dw;
	byte hr;
	byte mn;
	byte sc;
	byte dt;
};

histData lastData[10];

byte second = 0;
byte lastSecond = 0;
byte minute = 0;
byte lastMinute = 0;
byte hour = 0;
byte lastHour = 0;
byte dayOfWeek = 1;
byte lastDayOfWeek = 1;
byte fromSource = 0;
byte startPos = 0;
byte height;
byte btnPressed = 0;
uint32_t ledTime = 0;

SparkFun_AS3935 lightning(AS3935_ADDR);
StaticJsonBuffer<200> jsonBuffer;
Adafruit_ST7735 display = Adafruit_ST7735(CE,  DC, RST);
WiFiClient net;
MQTTClient client;
HTTPClient http;
hw_timer_t *timeTimer = NULL;

const unsigned char PROGMEM lightning_bmp[32] = {
		0x01, 0xE0, 0x02, 0x20, 0x0C, 0x18, 0x12, 0x24, 0x21, 0x06, 0x10, 0x02, 0x1F, 0xFC, 0x01, 0xF0,
		0x01, 0xC0, 0x03, 0x80, 0x07, 0xF8, 0x00, 0xF0, 0x00, 0xC0, 0x01, 0x80, 0x01, 0x00, 0x01, 0x00
};

void dispData() {
	if (storage.dispScreen == dispStat) printStat();
	if (storage.dispScreen == dispHist) printHist();
	if (storage.dispScreen == dispTijd) printTime();
	if (storage.dispScreen == dispLogo) printLogo();
	if (storage.dispScreen == dispInfo) printInfo();
	if (storage.dispScreen == dispMinute) printMinutes();
	if (storage.dispScreen == dispHour) printHours();
	if (storage.dispScreen == dispDay) printDays();
	digitalWrite(LED,0);
	ledTime = millis();
}

void printLogo() {
	display.clear();
	display.setTextColor(ST7735_YELLOW);
	display.setTextSize(2);
	display.setCursor(0, 0*lineHeight);
	display.print(F("   PI4RAZ"));
	display.setTextSize(1);
	display.setTextColor(ST7735_WHITE);
	display.setCursor(0,2*lineHeight);
	display.print(F("  Lightning"));
	display.setCursor(0, 3*lineHeight);
	display.print(F(" Detector v"));
	display.print(majorVersion);
	display.print(F("."));
	display.print(minorVersion);
}

void printInfo() {
	display.clear();
	display.setTextColor(ST7735_CYAN);
	display.setTextSize(2);
	display.setCursor(0, 0*lineHeight);
	display.print(F("Info:"));
	display.setTextSize(1);
	display.setTextColor(ST7735_WHITE);
	display.print(F("  HBC="));
	display.setCursor(0, 2*lineHeight);
	display.print(F("Call:"));
	display.print(storage.MyCall);
	display.setCursor(0, 3*lineHeight);
	display.print(F("Mode:"));
	if (storage.AS3935_doorMode == 0) display.print(F("Indoor")); else display.print(F("Outdoor"));
	display.setCursor(0, 4*lineHeight);
	display.print(F("Dist:"));
	if (storage.AS3935_distMode == 1) display.print(F("Enabled")); else display.print(F("Disabled"));
}

void printStat() {
	display.clear();
	display.setTextColor(ST7735_GREEN);
	display.setTextSize(2);
	display.setCursor(0, 0*lineHeight);
	display.print(F("Stats:"));
	display.setTextSize(1);
	display.setTextColor(ST7735_WHITE);
	display.setCursor(0, 2*lineHeight);
	display.print(F("Minute:"));
	display.print(minutes[0]);
	display.setCursor(0, 3*lineHeight);
	display.print(F("Hour  :"));
	display.print(hours[0]);
	display.setCursor(0, 4*lineHeight);
	display.print(F("Day   :"));
	display.print(days[0]);
	display.setCursor(0, 5*lineHeight);
	display.print(F("Pulses:"));
	display.print(pulses);
	display.print(F("/"));
	display.print(distPulses);
}

void printHist() {
	display.clear();
	display.setTextColor(ST7735_BLUE);
	display.setTextSize(2);
	display.setCursor(0, 0*lineHeight);
	display.print(F("History:"));
	display.setTextSize(1);
	display.setTextColor(ST7735_WHITE);
	int j=10;

	for (int i = 0; i < j; i++) {
		dispTime(i+2, lastData[i].dw, lastData[i].hr, lastData[i].mn, 100);
		display.setCursor(57, (i+2)*lineHeight);
		if (lastData[i].dt < 10) display.print(F(" "));
		display.print(lastData[i].dt); display.print(F("KM"));
	}
}

void printTime() {
	display.clear();
	display.setCursor(0, 0*lineHeight);
	display.setTextColor(ST7735_RED);
	display.setTextSize(2);
	switch (dayOfWeek) {
		case 1:
			display.print(F("Sun "));
			break;
		case 2:
			display.print(F("Mon "));
			break;
		case 3:
			display.print(F("Tue "));
			break;
		case 4:
			display.print(F("Wed "));
			break;
		case 5:
			display.print(F("Thu "));
			break;
		case 6:
			display.print(F("Fri "));
			break;
		case 7:
			display.print(F("Sat "));
			break;
	}
	if (hour < 10) display.print('0');
	display.print(hour, DEC);
	display.print(':');
	if (minute < 10) display.print('0');	
	display.print(minute, DEC);

	display.setTextSize(1);
	display.setTextColor(ST7735_WHITE);

	display.setCursor(0, 3*lineHeight);
	display.print(F("Minute:"));
	display.print(minutes[0]);
	display.setCursor(0, 4*lineHeight);
	display.print(F("Hour  :"));
	display.print(hours[0]);
	display.setCursor(0, 5*lineHeight);
	display.print(F("Day   :"));
	display.print(days[0]);
}

bool isASleep = 0;
void loop()
{
	isASleep = 0;
	if (millis()-ledTime>4000 && digitalRead(LED)==0){
		Serial.println("Lets sleep");
		delay(100);
		digitalWrite(LED,1);
		isASleep = 1;
		esp_light_sleep_start();
	} 

    updCounter = 0;

	if (isASleep == 1){
		isASleep = 0;	
		if (digitalRead(LED)==1){
			digitalWrite(LED,0);
			ledTime = millis();	
		}
		delay(100);
		getNTPData();
		Serial.print("Awoke by:");
		Serial.print(esp_sleep_get_wakeup_cause());
	}

	if (second != lastSecond){
		Serial.print('.');
		lastSecond = second;
	}

	fromSource = FROMNOTHING;
	delay(5);

	if (digitalRead(BUTTON) == 1) {
		float startButton = millis();
		bool hasBeeped = 0;
		while (digitalRead(BUTTON) == 1) {
			if (!hasBeeped){
				SingleBeep(1);
				hasBeeped = 1;
			}
			if (millis()-startButton>5000){
				SingleBeep(5);
				esp_restart();
			}
		}
		fromSource = FROMMENU;
		if (digitalRead(LED)==0) handleMenu();
	}

	int intVal = 0;
	if(digitalRead(AS3935_intPin) == HIGH){
		fromSource = FROMLIGHTNING;
		intVal = lightning.readInterruptReg();
		handleLighting(intVal);
	}

	if (lastMinute != minute)
	{
		lastMinute = minute;
		fromSource = FROMTIME;
		moveMinutes();
		if (hour != lastHour) {
			lastHour = hour;
			moveHours();
			DoHeartBeat();
		}
		if (dayOfWeek != lastDayOfWeek) {
			lastDayOfWeek = dayOfWeek;
			moveDays();
		}
	}

	delay(200);
	if (fromSource < FROMNOTHING) dispData();
	client.loop();
}

void DoHeartBeat(){
	check_connection();
	getNTPData();
	sendToSite(0, 0);
}

void saveConfig() {
	for (unsigned int t = 0; t < sizeof(storage); t++)
		EEPROM.write(offsetEEPROM + t, *((char*)&storage + t));
	EEPROM.commit();
}

void loadConfig() {
	if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
		for (unsigned int t = 0; t < sizeof(storage); t++)
			*((char*)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
	if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit){
		for (unsigned int t = 0; t < sizeof(storage); t++)
			Serial.write(EEPROM.read(offsetEEPROM + t));
		Serial.println();
		setSettings(0);
	}
}

void setSettings(bool doAsk) {
	int i = 0;
	Serial.print(F("SSID ("));
	Serial.print(storage.ESP_SSID);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(15);
		if (receivedString[0] != 0) {
			storage.ESP_SSID[0] = 0;
			strcat(storage.ESP_SSID, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Password ("));
	Serial.print(storage.ESP_PASS);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(26);
		if (receivedString[0] != 0) {
			storage.ESP_PASS[0] = 0;
			strcat(storage.ESP_PASS, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Call ("));
	Serial.print(storage.MyCall);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(9);
		if (receivedString[0] != 0) {
			storage.MyCall[0] = 0;
			strcat(storage.MyCall, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("MQTT Broker ("));
	Serial.print(storage.mqtt_broker);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(49);
		if (receivedString[0] != 0) {
			storage.mqtt_broker[0] = 0;
			strcat(storage.mqtt_broker, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("MQTT Port ("));
	Serial.print(storage.mqtt_port);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.mqtt_port = i;
	}
	
	Serial.println();

	Serial.print(F("MQTT User ("));
	Serial.print(storage.mqtt_user);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(24);
		if (receivedString[0] != 0) {
			storage.mqtt_user[0] = 0;
			strcat(storage.mqtt_user, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("MQTT Password ("));
	Serial.print(storage.mqtt_pass);
	Serial.print(F("):"));
	if (doAsk == 1) {
		getStringValue(24);
		if (receivedString[0] != 0) {
			storage.mqtt_pass[0] = 0;
			strcat(storage.mqtt_pass, receivedString);
		}
	}
	Serial.println();

	Serial.print(F("Domoticz device ("));
	Serial.print(storage.domoticzDevice);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.domoticzDevice = i;
	}
	Serial.println();

	Serial.print(F("Indoors=0 or Outdoors=1 ("));
	if (storage.AS3935_doorMode == 0) {
		Serial.print(F("Indoors"));
	} else {
		Serial.print(F("Outdoors"));
	}
	Serial.print(F("): "));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_doorMode = i;
	}
	Serial.println();

	Serial.print(F("Disturber (0/1) ("));
	if (storage.AS3935_distMode == 0) {
		Serial.print(F("Disabled"));
	} else {
		Serial.print(F("Enabled"));
	}
	Serial.print(F("): "));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_distMode = i;
	}
	Serial.println();

	Serial.print(F("Capacity ("));
	Serial.print(storage.AS3935_capacitance);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_capacitance = i;
	}
	Serial.println();

	Serial.print(F("Noiselevel (1 - 8) ("));
	Serial.print(storage.AS3935_noiseFloorLvl);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_noiseFloorLvl = i;
	}
	Serial.println();
		
	Serial.print(F("Watchdog level (0 - 10) ("));
	Serial.print(storage.AS3935_watchdogThreshold);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_watchdogThreshold = i;
	}
	Serial.println();

	Serial.print(F("Spike rejection level (0 - 11) ("));
	Serial.print(storage.AS3935_spikeRejection);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_spikeRejection = i;
	}
	Serial.println();

	Serial.print(F("Lightning treshold (0, 1, 5, 9, 16) ("));
	Serial.print(storage.AS3935_lightningThresh);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.AS3935_lightningThresh = i;
	}
	Serial.println();

	Serial.print(F("#Beeper ("));
	Serial.print(storage.beeperCnt);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.beeperCnt = i;
	}
	Serial.println();

	Serial.print(F("Time correction ("));
	Serial.print(storage.timeCorrection);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.timeCorrection = i;
	}
	Serial.println();

	Serial.print(F("Screen 1 (0 - 7) ("));
	Serial.print(storage.dispScreen);
	Serial.print(F("):"));
	if (doAsk == 1) {
		i = getNumericValue();
		if (receivedString[0] != 0) storage.dispScreen = i;
	}
	Serial.println();
	Serial.println();

	if (doAsk == 1) {
		saveConfig();
		loadConfig();
	}
}

void getStringValue(int length) {
	serialFlush();
	receivedString[0] = 0;
	int i = 0;
	while (receivedString[i] != 13 && i < length) {
		if (Serial.available() > 0) {
			receivedString[i] = Serial.read();
			if (receivedString[i] == 13 || receivedString[i] == 10) {
				i--;
			}
			else {
				Serial.write(receivedString[i]);
			}
			i++;
		}
	}
	receivedString[i] = 0;
	serialFlush();
}

byte getCharValue() {
	serialFlush();
	receivedString[0] = 0;
	int i = 0;
	while (receivedString[i] != 13 && i < 2) {
		if (Serial.available() > 0) {
			receivedString[i] = Serial.read();
			if (receivedString[i] == 13 || receivedString[i] == 10) {
				i--;
			}
			else {
				Serial.write(receivedString[i]);
			}
			i++;
		}
	}
	receivedString[i] = 0;
	serialFlush();
	return receivedString[i - 1];
}

int getNumericValue() {
	serialFlush();
	byte myByte = 0;
	byte inChar = 0;
	bool isNegative = false;
	receivedString[0] = 0;

	int i = 0;
	while (inChar != 13) {
		if (Serial.available() > 0) {
			inChar = Serial.read();
			if (inChar > 47 && inChar < 58) {
				receivedString[i] = inChar;
				i++;
				Serial.write(inChar);
				myByte = (myByte * 10) + (inChar - 48);
			}
			if (inChar == 45) {
				Serial.write(inChar);
				isNegative = true;
			}
		}
	}
	receivedString[i] = 0;
	if (isNegative == true) myByte = myByte * -1;
	serialFlush();
	return myByte;
}

void serialFlush() {
	for (int i = 0; i < 10; i++)
	{
		while (Serial.available() > 0) {
			Serial.read();
		}
	}
}

void handleMenu() {
	delay(200);
	Serial.print(F("Display:")); Serial.println(storage.dispScreen);
	Serial.println(F("Button pressed:"));
	storage.dispScreen++;
	if (storage.dispScreen > dispMax) storage.dispScreen = 0;
}

void IRAM_ATTR updateTime() {
	second++;
	updCounter++;
	if (updCounter==wdtTimeout) esp_restart();

	if (second==60){
		second = 0;
		minute++;
        Serial.print(".");
		if (minute==60){
			minute = 0;
			hour++;
			if (hour == 24) {
				hour = 0;
				dayOfWeek++;
				if (dayOfWeek == 8) {
					dayOfWeek = 1;
				}
			}
		}
	}
}

void SingleBeep(int cnt) {
	int tl = 200;
	for (int i = 0; i < cnt; i++) {
		digitalWrite(BEEPER, beepOn);
		delay(tl);
		digitalWrite(BEEPER, beepOff);
		delay(tl);
	}
}

void handleLighting(uint8_t int_src) {
	showTime();
	display.clear();
	if (0 == int_src)
	{
		Serial.println(F("Distance estimation has changed"));
		display.clear();
		display.setCursor(0,0*lineHeight);
		display.print(F("Distance"));
		display.setCursor(0,1*lineHeight);
		display.print(F(" estimation"));
		display.setCursor(0,2*lineHeight);
		display.print(F(" has changed"));
		//display.display();
	}
	else if (LIGHTNING_INT == int_src)
	{
		uint8_t lightning_dist_km = lightning.distanceToStorm(); 
		Serial.print(F("Lightning detected! Distance to strike: "));
		Serial.print(lightning_dist_km);
		Serial.println(F(" kilometers"));
		display.clear();
		display.setCursor(0,0*lineHeight);
		display.print(F("Lightning"));
		display.setCursor(0,1*lineHeight);
		display.print(F("  Dist. "));
		display.print(lightning_dist_km);
		display.print(F(" KM"));
		//display.display();
		pulses++;
		minutes[0]++;
		hours[0]++;
		days[0]++;
		for (int i = 0; i < 9; i++) {
			lastData[9 - i] = lastData[8 - i];
		}
		lastData[0].dw = dayOfWeek;
		lastData[0].hr = hour;
		lastData[0].mn = minute;
		lastData[0].sc = second;
		lastData[0].dt = lightning_dist_km;

		sendToSite(1,lightning_dist_km);

		for (int i = 0; i < 10; i++) {
			Serial.print(lastData[i].dw); Serial.print(F(" "));
			Serial.print(lastData[i].hr); Serial.print(F(":"));
			Serial.print(lastData[i].mn); Serial.print(F(":"));
			Serial.print(lastData[i].sc); Serial.print(F(" Dist:"));
			Serial.print(lastData[i].dt); Serial.println(F(" KM"));
		}
		if (storage.beeperCnt > 0 && minutes[0] >= storage.beeperCnt && minuteBeeped == 0) {
				SingleBeep(5);
				minuteBeeped++;
		}
	}
	else if (DISTURBER_INT == int_src)
	{
		Serial.println(F("Disturber detected"));
		display.clear();
		display.setCursor(0,0*lineHeight);
		display.print(F("Disturber"));
		display.setCursor(0,1*lineHeight);
		display.print(F("  detected"));
		distPulses++;
		//display.display();
	}
	else if (NOISE_INT == int_src)
	{
		display.clear();
		display.setCursor(0,0*lineHeight);
		Serial.println(F("Noise level too high"));
		display.print(F("Noise level"));
		display.setCursor(0,1*lineHeight);
		display.print(F(" too high"));
		//display.display();
	}
	digitalWrite(LED,0);
	ledTime = millis();	
	lightning.PrintAllRegs();
	lightning.clearDistance(0x1F);
	lightning.clearStatistics(true);
	delay(500);
	lightning.PrintAllRegs();
}

void moveMinutes() {
	for (int i = 0; i < 59; i++) {
		minutes[59 - i] = minutes[58 - i];
	}
	minutes[0] = 0;
	maxMinute = 0;
	for (int i = 0; i <= 59; i++) {
		if (minutes[i] > maxMinute) {
			maxMinute = minutes[i];
		}
	}
	divMinute = maxMinute / 30;
	if (divMinute < 1) {
		divMinute = 1;
	}
	minuteBeeped = 0;
}

void moveHours() {
	for (int i = 0; i < 23; i++) {
		hours[23 - i] = hours[22 - i];
	}
	hours[0] = 0;
	maxHour = 0;
	for (int i = 0; i <= 23; i++) {
		if (hours[i] > maxHour) {
			maxHour = hours[i];
		}
	}
	divHour = maxHour / 30;
	if (divHour < 1) {
		divHour = 1;
	}
}

void moveDays() {
	for (int i = 0; i < 29; i++) {
		days[29 - i] = days[28 - i];
	}
	days[0] = 0;
	maxDay = 0;
	for (int i = 0; i <= 29; i++) {
		if (days[i] > maxDay) {
			maxDay = days[i];
		}
	}
	divDay = maxDay / 30;
	if (divDay < 1) {
		divDay = 1;
	}
}

void showTime() {
	switch (dayOfWeek) {
	case 1:
		Serial.print(F("Sunday"));
		break;
	case 2:
		Serial.print(F("Monday"));
		break;
	case 3:
		Serial.print(F("Tuesday"));
		break;
	case 4:
		Serial.print(F("Wednesday"));
		break;
	case 5:
		Serial.print(F("Thursday"));
		break;
	case 6:
		Serial.print(F("Friday"));
		break;
	case 7:
		Serial.print(F("Saturday"));
		break;
	}
	Serial.print(' ');
	if (hour < 10)
	{
		Serial.print('0');
	}
	Serial.print(hour, DEC);
	Serial.print(':');
	if (minute < 10)
	{
		Serial.print('0');
	}
	Serial.print(minute, DEC);
	Serial.print(':');
	if (second < 10)
	{
		Serial.print('0');
	}
	Serial.print(second, DEC);
	Serial.print(' ');
}

void dispTime(byte line, byte dw, byte hr, byte mn, byte sc) {
	display.setCursor(0, line*lineHeight);
	switch (dw) {
	case 1:
		display.print(F("Sun"));
		break;
	case 2:
		display.print(F("Mon"));
		break;
	case 3:
		display.print(F("Tue"));
		break;
	case 4:
		display.print(F("Wed"));
		break;
	case 5:
		display.print(F("Thu"));
		break;
	case 6:
		display.print(F("Fri"));
		break;
	case 7:
		display.print(F("Sat"));
		break;
	default:
		display.print(F("***"));
		break;
	}

	display.setCursor(21, line*lineHeight);
	if (hr < 10)
	{
		display.print('0');
	}
	display.print(hr, DEC);
	display.print(':');
	if (mn < 10)
	{
		display.print('0');
	}
	display.print(mn, DEC);
	if (sc < 99) {
		display.print(':');
		if (sc < 10)
		{
			display.print('0');
		}
		display.print(sc, DEC);
	}
}

void setup()
{
	pinMode(BEEPER, OUTPUT);
	pinMode(LED, OUTPUT);
	pinMode(AS3935_intPin, INPUT);
	pinMode(BUTTON, INPUT_PULLDOWN);

	pinMode(RST, OUTPUT);
	pinMode(CE, OUTPUT);
	pinMode(DC, OUTPUT);

	configure_timer();	
	
	digitalWrite(BEEPER,beepOff);
	if (storage.beeperCnt>0) SingleBeep(2);

	Serial.begin(115200);
	Serial.print(F("Playing With Fusion: AS3935 Lightning Sensor, SEN-39001-R01  v"));
	Serial.print(majorVersion);
	Serial.print(F("."));
	Serial.println(minorVersion);
	Serial.println(F("beginning boot procedure...."));
	Serial.println(F("Start display"));

	display.begin(84, 48, 0);
	delay(200);
	printLogo();
	digitalWrite(LED,0);

	if (!EEPROM.begin(EEPROM_SIZE))
	{
		display.clear();
		display.setCursor(0, 0);
		display.println(F("failed to initialise EEPROM"));
		Serial.println(F("failed to initialise EEPROM"));
		while(1); 
	}
	if (EEPROM.read(offsetEEPROM) != storage.chkDigit || digitalRead(BUTTON)==1){
		Serial.println(F("Writing defaults...."));
		saveConfig();
	}

	loadConfig();
	printConfig();

	delay(1000);
	display.clear();
	display.setCursor(0, 0);
	display.println(F("Starting"));

	Serial.println(F("Type GS to enter setup:"));
	display.println(F("Wait for setup"));
	delay(5000);
	if (Serial.available()) {
		Serial.println(F("Check for setup"));
		if (Serial.find(chkGS)) {
			display.println(F("Setup entered"));
			Serial.println(F("Setup entered..."));
			setSettings(1);
			delay(2000);
		}
	}

	// setup for the the I2C library: (enable pullups, set speed to 400kHz)
	Wire.setClock(100000);
	Serial.println(F("Wire start"));
	Wire.begin();
	delay(2);
	display.println(F("Starting detector"));
	Serial.println(F("Lightning start"));
	if( !lightning.begin() ){ // Initialize the sensor. 
		display.println(F("Detector not started"));
		Serial.println ("Lightning Detector did not start up, freezing!"); 
		delay(5000);
		esp_restart();
	}

	display.println(F("Set detector params"));
	bool setAS3935 = check_AS3935();
	while (!setAS3935)
	{
		display.print(F("."));
		delay(1000);
		lightning.resetSettings();
		lightning.maskDisturber(storage.AS3935_distMode); 
		if (storage.AS3935_doorMode==0) lightning.setIndoorOutdoor(INDOOR); else lightning.setIndoorOutdoor(OUTDOOR); 
		lightning.setNoiseLevel(storage.AS3935_noiseFloorLvl);  
		lightning.watchdogThreshold(storage.AS3935_watchdogThreshold); 
		lightning.spikeRejection(storage.AS3935_spikeRejection); 
		lightning.lightningThreshold(storage.AS3935_lightningThresh); 
		lightning.tuneCap(storage.AS3935_capacitance); 
		setAS3935 = check_AS3935();
	}

	Serial.print(F("Clear array...."));
	for (int i = 0; i < 10; i++) {
		lastData[i].dw = 0;
		lastData[i].hr = 0;
		lastData[i].mn = 0;
		lastData[i].sc = 0;
		lastData[i].dt = 0;
		Serial.print(F("."));
	}
	Serial.println();
	Serial.println(F("Array cleared...."));
	display.println(F("Start MQTT"));
	Serial.println(F("Start MQTT"));
	client.begin(storage.mqtt_broker, storage.mqtt_port, net);
	//client.onMessage(messageReceived);
	Serial.println(F("Start WiFi"));
	display.println(F("Start WiFi"));
	check_connection();
	getNTPData();
	sendToSite(0, 0);
	display.clear();
	printInfo();
	ledTime = millis();
	esp_sleep_enable_ext1_wakeup(0x402000000,ESP_EXT1_WAKEUP_ANY_HIGH);
	esp_sleep_enable_timer_wakeup(15 * 60 * 1000000);
}

bool check_AS3935() {
	bool result = false;
	byte distMode = lightning.readMaskDisturber();
	Serial.print("Are disturbers being masked: "); 
	if (distMode == 1)
		Serial.println("YES"); 
	else if (distMode == 0)
		Serial.println("NO"); 

	byte doorMode = lightning.readIndoorOutdoor();
	byte doorValue;
	Serial.print("Are we set for indoor or outdoor: ");  
	if( doorMode == INDOOR ){
		Serial.println("Indoor.");  
		doorValue = 0;
	}
	else if( doorMode == OUTDOOR ){
		Serial.println("Outdoor.");
		doorValue = 1;
	}  
	else {
		Serial.println(doorMode, BIN);
		doorValue=-1;
	}

	byte noiseFloorLvl = lightning.readNoiseLevel();
	Serial.print("Noise Level is set at: ");
	Serial.println(noiseFloorLvl);

	byte watchdogThreshold = lightning.readWatchdogThreshold();
	Serial.print("Watchdog Threshold is set to: ");
	Serial.println(watchdogThreshold);

	byte spikeRejection = lightning.readSpikeRejection();
	Serial.print("Spike Rejection is set to: ");
	Serial.println(spikeRejection);

	byte lightningThresh = lightning.readLightningThreshold();
	Serial.print("The number of strikes before interrupt is triggerd: "); 
	Serial.println(lightningThresh); 

	byte capacitance = lightning.readTuneCap();
	Serial.print("Internal Capacitor is set to: "); 
	Serial.println(capacitance);

	byte divRatio = lightning.readDivRatio();
	Serial.print("Divider Ratio is set to: "); 
	Serial.println(divRatio);

	if (distMode==storage.AS3935_distMode)
		if (doorValue==storage.AS3935_doorMode)
			if (capacitance==storage.AS3935_capacitance)
				if (noiseFloorLvl==storage.AS3935_noiseFloorLvl)
					if (watchdogThreshold == storage.AS3935_watchdogThreshold)
						if (spikeRejection == storage.AS3935_spikeRejection)
							if (lightningThresh == storage.AS3935_lightningThresh)
								result = true;

	Serial.println();
	return result;
}

void configure_timer() {
	timeTimer = timerBegin(0, 80, true);                  	//timer 0, div 80
	timerAttachInterrupt(timeTimer, &updateTime, false);  	//attach callback
	timerAlarmWrite(timeTimer, 1000 * 1000, true); 			//set time in us
	timerAlarmEnable(timeTimer);                          	//enable interrupt
}

boolean check_connection() {
	Serial.println("Check connection");
	updCounter = 0;
	if (WiFi.status() != WL_CONNECTED) {
		InitWiFiConnection();
	}

	if (WiFi.status() == WL_CONNECTED){
		if (!client.connected()){
			InitMQTTConnection();
		}
	}
	return (WiFi.status() == WL_CONNECTED) &&client.connected();
}

void InitWiFiConnection() {
	WlanReset();
	Serial.println(F("Reset WiFi"));

    WiFi.begin(storage.ESP_SSID,storage.ESP_PASS);
    while (((WiFi.status()) != WL_CONNECTED)){
        Serial.print(".");
        //WlanReset();
        delay(1000);
    }

	WlanStatus();

	if (WlanStatus()==WL_CONNECTED){
		Serial.print("WiFi Connected to: ");
		Serial.println(storage.ESP_SSID);
		Serial.print("IP address: ");
		Serial.println(WiFi.localIP());
	}
}

void InitMQTTConnection() {
	Serial.println(F("Connecting to MQTT..."));

  	while (!client.connect(storage.MyCall, storage.mqtt_user, storage.mqtt_pass)) {
    	Serial.print(".");
    	delay(1000);
	}

	if (client.connected()){
		Serial.print(F("MQTT connected to: "));
		Serial.println(storage.mqtt_broker);
	}
}

void WlanReset() {
	WiFi.persistent(false);
	WiFi.disconnect();
	WiFi.mode(WIFI_OFF);
	WiFi.mode(WIFI_STA);
	delay(1000);
}

int WlanStatus() {
	switch (WiFi.status()) {
	case WL_CONNECTED:
		Serial.print(F("WlanCStatus:: CONNECTED to:"));				// 3
		Serial.println(WiFi.SSID());
		WiFi.setAutoReconnect(true);				// Reconenct to this AP if DISCONNECTED
		return(3);
		break;

		// In case we get disconnected from the AP we loose the IP address.
		// The ESP is configured to reconnect to the last router in memory.
	case WL_DISCONNECTED:
		Serial.print(F("WlanStatus:: DISCONNECTED, IP="));			// 6
		Serial.println(WiFi.localIP());
		return(6);
		break;

		// When still pocessing
	case WL_IDLE_STATUS:
		Serial.println(F("WlanStatus:: IDLE"));					// 0
		return(0);
		break;

		// This code is generated as soonas the AP is out of range
		// Whene detected, the program will search for a better AP in range
	case WL_NO_SSID_AVAIL:
		Serial.println(F("WlanStatus:: NO SSID"));					// 1
		return(1);
		break;

	case WL_CONNECT_FAILED:
		Serial.println(F("WlanStatus:: FAILED"));					// 4
		return(4);
		break;

		// Never seen this code
	case WL_SCAN_COMPLETED:
		Serial.println(F("WlanStatus:: SCAN COMPLETE"));			// 2
		return(2);
		break;

		// Never seen this code
	case WL_CONNECTION_LOST:
		Serial.println(F("WlanStatus:: LOST"));					// 5
		return(5);
		break;

		// This code is generated for example when WiFi.begin() has not been called
		// before accessing WiFi functions
	case WL_NO_SHIELD:
		Serial.println(F("WlanStatus:: WL_NO_SHIELD"));				//255
		return(255);
		break;

	default:
		break;
	}
	return(-1);
}

void sendToSite(byte whichInt, byte dist) {
	if (check_connection()) {
		Serial.println("Send to site");
		//HTTPClient http; //Declare an object of class HTTPClient
		String getData = String("http://onweer.pi4raz.nl/AddEvent.php?Callsign=") +
		String(storage.MyCall) + 
		String("&IntType=") +
		whichInt +
		String("&Distance=") +
		dist;

		Serial.println(getData);
		http.begin(getData); //Specify request destination
		int httpCode = http.GET(); //Send the request
		if (httpCode > 0) { //Check the returning code
			String payload = http.getString(); //Get the request response payload
			Serial.println("----");
			Serial.println(payload); //Print the response payload
			Serial.println("----");
			hbSend = 1;
		}
		http.end();

		JsonObject& root = jsonBuffer.createObject();
		root["command"] = "udevice";
		root["idx"] = storage.domoticzDevice;
		if (dist == 0){
			root["nvalue"] = 1;
			root["svalue"] = String("Heartbeat");
			client.publish("rdkonweer/heartbeat", "1");
		}
		else{
			root["nvalue"] = 3;
			root["svalue"] = String("Onweer op ") + String(dist) + String(" KM");
			client.publish("rdkonweer/onweer", String(dist) + String(" KM"));
		}
		

		root.printTo(Serial);

		char jsonChar[100];
		root.printTo((char*)jsonChar, root.measureLength() + 1);

		//client.publish("rdkonweer/", (char*)jsonChar,0,1);
		jsonBuffer.clear();
	}
}

void getNTPData() {
	if (check_connection()) {
		Serial.println("Get time");
		//HTTPClient http; //Declare an object of class HTTPClient
		http.begin("http://onweer.pi4raz.nl/getTime.php"); //Specify request destination
		int httpCode = http.GET(); //Send the request
		Serial.println(httpCode);
		if (httpCode > 0) { //Check the returning code
			String payload = http.getString(); //Get the request response payload
			Serial.println(payload); //Print the response payload
	        JsonObject& root = jsonBuffer.parseObject(payload);
			if (root.success()){
				String dow = root["Time"][0]; 
				int year = root["Time"][1];
				int month = root["Time"][2];
				int day = root["Time"][3];
				hour = root["Time"][4];
				minute = root["Time"][5];
				second = root["Time"][6];		

				if (dow=="Sun") dayOfWeek = 1;
				if (dow=="Mon") dayOfWeek = 2;
				if (dow=="Tue") dayOfWeek = 3;
				if (dow=="Wed") dayOfWeek = 4;
				if (dow=="Thu") dayOfWeek = 5;
				if (dow=="Fri") dayOfWeek = 6;
				if (dow=="Sat") dayOfWeek = 7;

				hour = hour + storage.timeCorrection;
				if (hour > 23) {
					hour = hour - 24;
					dayOfWeek++;
					if (dayOfWeek > 7) dayOfWeek = 1;
				}
			}
			jsonBuffer.clear();
		}
		http.end();
    }
}

void printMinutes() {
	display.clear();
	display.setCursor(startPix, startPix);
	display.setTextColor(ST7735_YELLOW);
	display.setTextSize(2);
	display.print(F("Minutes"));
	display.setTextSize(1);
	display.setCursor(startPix, 20);
	display.print(F("Max/min:"));
	display.print(maxMinute);
	printGraph();
	display.setCursor(startPix, 113);
	display.print(F("M:"));

	display.setCursor(startPix+5+77, 113);
	display.print('0');
	display.setCursor(startPix+5+47, 113);
	display.print(F("15"));
	display.setCursor(startPix+5+17, 113);
	display.print(F("30"));
	startPos = 0;

	for (int i = 0; i < 30; i++) {
		height = minutes[(i + startPos)] / divMinute;
		if (height > 65) {
			height = 65;
			if (i == 0) {
				printArrow();
			}
		}
		display.drawLine(88 - (i * 2),  110 - (height),  88 - (i * 2), 110, GRFCLR);
	}
	display.setTextColor(ST7735_WHITE);
}

void printHours() {
	display.clear();
	display.setCursor(startPix, startPix);
	display.setTextColor(ST7735_GREEN);
	display.setTextSize(2);
	display.print(F("Hours"));
	display.setTextSize(1);
	display.setCursor(startPix, 20);
	display.print(F("Max/hour:"));
	display.print(maxHour);
	printGraph();
	display.setCursor(startPix, 113);
	display.print(F("H:"));

	display.setCursor(startPix+5+77, 113);
	display.print('0');
	display.setCursor(startPix+5+53, 113);
	display.print(F("12"));
	display.setCursor(startPix+5+29, 113);
	display.print(F("24"));
	startPos = 0;

	for (int i = 0; i < 24; i++) {
		height = hours[(i + startPos)] / divHour;
		if (height > 65) {
			height = 65;
			if (i == 0) {
				printArrow();
			}
		}
		display.drawLine(88 - (i * 2),  110 - (height),  88 - (i * 2), 110, GRFCLR);
	}
	display.setTextColor(ST7735_WHITE);
}

void printDays() {
	display.clear();
	display.setCursor(startPix, startPix);
	display.setTextColor(ST7735_BLUE);
	display.setTextSize(2);
	display.print(F("Days"));
	display.setTextSize(1);
	display.setCursor(startPix, 20);
	display.print(F("Max/day:"));
	display.print(maxDay);
	printGraph();
	display.setCursor(startPix, 113);
	display.print(F("D:"));

	display.setCursor(startPix+5+77, 113);
	display.print('0');
	display.setCursor(startPix+5+47, 113);
	display.print(F("15"));
	display.setCursor(startPix+5+17, 113);
	display.print(F("30"));

	for (int i = 0; i < 30; i++) {
		height = days[i] / divDay;
		if (height > 65) {
			height = 65;
			if (i == 0) {
				printArrow();
			}
		}
		display.drawLine(88 - (i * 2),  110 - (height),  88 - (i * 2), 110, GRFCLR);
	}
	display.setTextColor(ST7735_WHITE);
}

void printGraph() {
	display.setCursor(0, 45);
	display.print(100);
	display.setCursor(0, 65);
	display.print(75);
	display.setCursor(0, 85);
	display.print(50);
	display.setCursor(0, 105);
	display.print(25);
	display.drawLine( 20,  44,  25, 44, LINECLR);
	display.drawLine( 20,  64,  25, 64, LINECLR);
	display.drawLine( 20,  84,  25, 84, LINECLR);
	display.drawLine( 20,  104,  25, 104, LINECLR);
	display.drawLine( 25,  110,  89, 110, LINECLR);
	display.drawLine( 25,  111,  89, 111, LINECLR);
	display.drawLine( 25,  44,  25, 110, LINECLR);
	display.drawLine( 24,  44,  24, 111, LINECLR);
	display.setCursor(0, 0);
}

void printArrow() {
	display.drawLine( 86,  42,  88, 44, GRFCLR);
	display.drawLine( 86,  46,  88, 44, GRFCLR);
}