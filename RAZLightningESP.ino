// *************************************************************************************
//  V4.1  Inbouw MQTT switch & Strikes teller Frank
//  V4.0  Ombouw naar standaard tft print
//  Placed on GITHUB Aug. 1 2018
//  By R.J. de Kok - (c) 2018 - 2023
// *************************************************************************************

#include "SparkFun_AS3935.h"
//#include "AS3935.h"
#include "EEPROM.h"
#include "WiFi.h"
#include <WifiMulti.h>
#include "Wire.h"
#include <HTTPClient.h>
#include <MQTT.h>
#include <SPI.h>

#include "Free_Fonts.h" // Include the header file attached to this sketch
#include <TFT_eSPI.h>   // https://github.com/Bodmer/TFT_eSPI
#include "NTP_Time.h"


#define LED           14
#define lineHeight    18
#define AS3935_intPin 26

#define AS3935_ADDR   0x03
#define INDOOR        0x12
#define OUTDOOR       0xE
#define LIGHTNING_INT 0x08
#define DISTURBER_INT 0x04
#define NOISE_INT     0x01

#define BEEPER        32
#define beepOn        1
#define beepOff       0

#define FROMTIME      0
#define FROMMENU      1
#define FROMLIGHTNING 2
#define FROMNOTHING   3

#define wdtTimeout      60
#define dispStat        0
#define dispHist        1
#define dispInfo        2
#define dispExamples    0
#define dispMinute      3
#define dispHour        4
#define dispDay         5
#define dispMax         5
#define ExampleCounter  24
#define ExampleScale    6
#define ExampleMax      18


#define offsetEEPROM  0x10
#define EEPROM_SIZE   250
#define TIMEZONE euCET

struct StoreStruct {
  byte chkDigit;
  char ESP_SSID[25];
  char ESP_PASS[27];
  char MyCall[10];
  char mqtt_broker[50];
  char mqtt_user[25];
  char mqtt_pass[25];
  char mqtt_subject[25]; 
  int mqtt_port;
  bool use_MQTT;
  byte AS3935_doorMode;
  byte AS3935_distMode;
  byte AS3935_capacitance;
  byte AS3935_divisionRatio;
  byte AS3935_noiseFloorLvl;
  byte AS3935_watchdogThreshold;
  byte AS3935_spikeRejection;
  byte AS3935_lightningThresh;
  byte beeperCnt;
  byte dispScreen;
};

typedef struct {  // WiFi Access
  const char *SSID;
  const char *PASSWORD;
} wlanSSID;

#include "RDK_Settings.h";
// #include "All_Settings.h";

char receivedString[128];
char chkGS[3] = "GS";

byte minutes[60];
byte hours[24];
byte days[30];
unsigned long strikeTimes[11];
byte examples[ExampleCounter];

unsigned long strikeTime;
unsigned long strikeDiff;
unsigned int strikePointer=0;
uint16_t pulses = 0;
uint16_t distPulses = 0;
byte minuteBeeped = 0;
int majorVersion = 4;
int minorVersion = 0;  //Eerste uitlevering 15/11/2017
int updCounter = 0;

struct histData {
  byte dw;
  byte hr;
  byte mn;
  byte sc;
  byte dt;
};

histData lastData[10];
time_t local_time;
time_t boot_time;
byte prevSecond = -1;
byte prevMinute = -1;
byte prevHour = -1;
byte prevDOW = -1;
float loopCheck = millis();

byte fromSource = 0;
byte startPos = 0;
byte height;
byte btnPressed = 0;

WiFiMulti wifiMulti;
SparkFun_AS3935 lightning(AS3935_ADDR);
TFT_eSPI tft = TFT_eSPI();  // Invoke custom library
WiFiClient net;
MQTTClient client;
HTTPClient http;
hw_timer_t *timeTimer = NULL;

void dispData() {
  if (storage.dispScreen == dispStat) printStat(dispExamples);
  if (storage.dispScreen == dispHist) printHist();
  if (storage.dispScreen == dispInfo) printLogo(1);
  if (storage.dispScreen == dispMinute) printStat(dispMinute);
  if (storage.dispScreen == dispHour) printStat(dispHour);
  if (storage.dispScreen == dispDay) printStat(dispDay);
}

void printLogo(bool withInfo) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(0, 0 * lineHeight);
  tft.print(F("PI4RAZ"));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 2 * lineHeight);
  tft.print(F("Lightning"));
  tft.setCursor(0, 3 * lineHeight);
  tft.print(F("Detector v"));
  tft.print(majorVersion);
  tft.print(F("."));
  tft.print(minorVersion);
  if (withInfo){
    printInfo();
  }
}

void printInfo() {
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(0, (4 * lineHeight)+15);
  tft.print(F("Info:"));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  //tft.print(F("  HBC="));
  tft.setCursor(0, (6 * lineHeight)+15);
  tft.print(F("Call:"));
  tft.print(storage.MyCall);
  tft.setCursor(0, (7 * lineHeight)+15);
  tft.print(F("Mode:"));
  if (storage.AS3935_doorMode == 0) tft.print(F("Indoor"));
  else tft.print(F("Outdoor"));
  tft.setCursor(0, (8 * lineHeight)+15);
  tft.print(F("Dist:"));
  if (storage.AS3935_distMode == 1) tft.print(F("Enabled"));
  else tft.print(F("Disabled"));
  tft.setCursor(0, (9 * lineHeight)+15);
  tft.print(F("Boot:"));
  printTime(boot_time,TFT_WHITE,false);
}

void printStat(int dispGraph) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(3);
  tft.setCursor(0, 0 * lineHeight);
  tft.print(F("Statistics:"));
  tft.setTextSize(2);
  tft.setCursor(0, (2 * lineHeight)-8);
  printTime(local_time,TFT_RED,true);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(0, 3 * lineHeight);
  tft.print(F("Minute:"));
  tft.print(minutes[0]);
  tft.setCursor(0, 4 * lineHeight);
  tft.print(F("Hour  :"));
  tft.print(hours[0]);
  tft.setCursor(0, 5 * lineHeight);
  tft.print(F("Day   :"));
  tft.print(days[0]);
  tft.setCursor(0, 6 * lineHeight);
  tft.print(F("Pulses:"));
  tft.print(pulses);
  tft.print(F("/"));
  tft.print(distPulses);
  if (dispGraph == dispExamples) printGraph(examples, ExampleCounter, ExampleScale, TFT_BLUE, "Examples");
  if (dispGraph == dispMinute) printGraph(minutes, 60, 12, TFT_BLUE, "Minutes");
  if (dispGraph == dispHour) printGraph(hours, 24, 12, TFT_PURPLE, "Hours");
  if (dispGraph == dispDay) printGraph(days, 30, 6, TFT_DARKCYAN, "Days");
}

void printGraph(byte graphArray[], int lenArray, int scale, uint32_t lColor, String gHeader){

  tft.fillRect(0,130,320,110,TFT_WHITE);
  tft.drawLine(20,220,20,140,lColor);
  tft.drawLine(20,220,290,220,lColor);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(lColor);
  tft.drawString(gHeader, 160, 150, GFXFF);

  tft.setTextSize(1);
  int hStart = 20;
  for (int i = 0;i<scale+1; i++){
    int xPos = 20 + (float)270/scale*i;
    int xTxt = lenArray - ((lenArray/scale)*i);
    tft.drawString(String(xTxt), xPos, 230, GFXFF);
  }

  int vMax = 1;
  for (int i = 0;i<lenArray; i++){
    if (graphArray[i]>vMax) vMax = graphArray[i];
  }

  for (int i = 0;i<5; i++){
    int yPos = 140 + (i*20);
    float yTxt = (((float)vMax/4)*(4-i));
    char buff[5];
    if (yTxt <10) sprintf(buff,"%.1f",yTxt);
    if (yTxt >9) sprintf(buff,"%.0f",yTxt);
    tft.drawString(buff, 10, yPos, GFXFF);
  }

  float hScale = (float)270/(lenArray-1);
  float vScale = (float)80/vMax;

  int lastX, lastY;
  for (int i=0;i<lenArray;i++){
    int x = 290-(i*hScale);
    int y = 220-(graphArray[i]*vScale);
    //Serial.printf("Pixel %d with value  %d on %d,%d (max = %d)\r\n",i, graphArray[i], x, y, vMax);
    if (i==0) tft.drawPixel(x,y,TFT_RED);
    else tft.drawLine(lastX,lastY,x,y,TFT_RED);
    lastX = x;
    lastY = y;
  }
}

void printTime(time_t thisTime, int dispColor, bool showDay) {
  tft.setTextColor(dispColor);
  if (showDay){
    tft.print(F(dayShortStr(weekday(thisTime))));
    tft.print(" ");
  }
  if (day(thisTime) < 10) tft.print('0');
  tft.print(day(thisTime));
  tft.print("-");
  tft.print(month(thisTime));
  tft.print("-");
  tft.print(year(thisTime));
  tft.print(" ");
  if (hour(thisTime) < 10) tft.print('0');
  tft.print(hour(thisTime), DEC);
  tft.print(':');
  if (minute(thisTime) < 10) tft.print('0');
  tft.print(minute(thisTime), DEC);
}

void printHist() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_BLUE);
  tft.setTextSize(3);
  tft.setCursor(0, 0 * lineHeight);
  tft.print(F("History:"));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  int j = 10;

  for (int i = 0; i < j; i++) {
    dispTime(i + 2, lastData[i].dw, lastData[i].hr, lastData[i].mn, 100);
    tft.setCursor(117, (i + 2) * lineHeight);
    if (lastData[i].dt < 10) tft.print(F(" "));
    tft.print(lastData[i].dt);
    tft.print(F(" KM"));
  }
}

void loop() {
  updCounter = 0;
  if (millis() - loopCheck>5000){
    loopCheck = millis();
    if (check_connection()){
      getNTPData();
      Serial.print('.');
      if (prevMinute != minute(local_time)) {
        prevMinute = minute(local_time);
        fromSource = FROMTIME;
        moveMinutes();
        moveExamples();
        if (hour(local_time) != prevHour) {
          prevHour = hour(local_time);
          moveHours();
          sendToSite(0, 0);
        }
        if (weekday(local_time) != prevDOW) {
          prevDOW = weekday(local_time);
          moveDays();
        }
        dispData();
      }
    }
  }
  
  fromSource = FROMNOTHING;

  uint16_t touchX = 0, touchY = 0;
  bool pressed = tft.getTouch(&touchX, &touchY);
  bool doMenu=false;
  if (pressed){
    Serial.printf("Position x:%d, y:%d\r\n",touchX, touchY);
    
    if (touchX>310 && touchY<10) esp_restart();

    SingleBeep(1);
    doMenu = true;
  } 

  if (doMenu){
    fromSource = FROMMENU;
    handleMenu();
  }

  int intVal = 0;
  if (digitalRead(AS3935_intPin) == HIGH) {
    fromSource = FROMLIGHTNING;
    intVal = lightning.readInterruptReg();
    Serial.printf("Lightning interrupt:%d\r\n",intVal);
    handleLighting(intVal);
  }

  if (fromSource < FROMNOTHING) dispData();
  if (storage.use_MQTT){
    client.loop();
  }
}

void saveConfig() {
  for (unsigned int t = 0; t < sizeof(storage); t++)
    EEPROM.write(offsetEEPROM + t, *((char *)&storage + t));
  EEPROM.commit();
}

void loadConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit)
    for (unsigned int t = 0; t < sizeof(storage); t++)
      *((char *)&storage + t) = EEPROM.read(offsetEEPROM + t);
}

void printConfig() {
  if (EEPROM.read(offsetEEPROM + 0) == storage.chkDigit) {
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

  Serial.print(F("Use MQTT (0 -1) ("));
  Serial.print(storage.use_MQTT);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.use_MQTT = i;
  }
  Serial.println();

  if (storage.use_MQTT){
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

    Serial.print(F("MQTT Subject ("));
    Serial.print(storage.mqtt_subject);
    Serial.print(F("):"));
    if (doAsk == 1) {
      getStringValue(24);
      if (receivedString[0] != 0) {
        storage.mqtt_subject[0] = 0;
        strcat(storage.mqtt_subject, receivedString);
      }
    }
    Serial.println();
  }

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

  Serial.print(F("Division ratio ("));
  Serial.print(storage.AS3935_divisionRatio);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.AS3935_divisionRatio = i;
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

  Serial.print(F("Screen 1 (0 - 5) ("));
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
      } else {
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
      } else {
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
  for (int i = 0; i < 10; i++) {
    while (Serial.available() > 0) {
      Serial.read();
    }
  }
}

void handleMenu() {
  delay(200);
  Serial.print(F("tft:"));
  Serial.println(storage.dispScreen);
  storage.dispScreen++;
  if (storage.dispScreen > dispMax) storage.dispScreen = 0;
}

void IRAM_ATTR updateTime() {
  updCounter++;
  if (updCounter == wdtTimeout) esp_restart();
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
  tft.fillScreen(TFT_BLACK);
  if (int_src == 0) {
    Serial.println(F("Distance estimation has changed"));
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0 * lineHeight);
    tft.print(F("Distance"));
    tft.setCursor(0, 1 * lineHeight);
    tft.print(F(" estimation"));
    tft.setCursor(0, 2 * lineHeight);
    tft.print(F(" has changed"));
  } else if (int_src == LIGHTNING_INT) {
    uint8_t lightning_dist_km = lightning.distanceToStorm();
    Serial.print(F("Lightning detected! Distance to strike: "));
    Serial.print(lightning_dist_km);
    Serial.println(F(" kilometers"));
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0 * lineHeight);
    tft.print(F("Lightning"));
    tft.setCursor(0, 1 * lineHeight);
    tft.print(F("  Dist. "));
    tft.print(lightning_dist_km);
    tft.print(F(" KM"));
    pulses++;
    minutes[0]++;
    hours[0]++;
    days[0]++;
    for (int i = 0; i < 9; i++) {
      lastData[9 - i] = lastData[8 - i];
    }
    lastData[0].dw = weekday(local_time);
    lastData[0].hr = hour(local_time);
    lastData[0].mn = minute(local_time);
    lastData[0].sc = second(local_time);
    lastData[0].dt = lightning_dist_km;

    sendToSite(1, lightning_dist_km);

    for (int i = 0; i < 10; i++) {
      Serial.printf("%d %d %d:%d Dist:%d KM\r\n", lastData[i].dw, lastData[i].hr, lastData[i].mn,lastData[i].sc, lastData[i].dt);
    }
    if (storage.beeperCnt > 0 && minutes[0] >= storage.beeperCnt && minuteBeeped == 0) {
      SingleBeep(5);
      minuteBeeped++;
      char buff[20];
      sprintf(buff, "%2d/%2d/%2d %2d:%2d:%2d", day(local_time), month(local_time), year(local_time), hour(local_time), minute(local_time), second(local_time));
      if (storage.use_MQTT){
        client.publish(String(storage.mqtt_subject) + "/lightning/datetime", buff);
        client.publish(String(storage.mqtt_subject) + "/lightning/distance", (String)lightning_dist_km + "KM");
      }
    }

    if (storage.use_MQTT){ //Frank z'n 10 strikes per second warning
      if (check_connection()){
        strikeTime = millis();
        if (strikeTime < strikeTimes[0]) strikePointer = 0; // more than 50 days up
        if (strikePointer < 9) {
          strikeTimes[strikePointer++] = strikeTime;
        }
        if (strikePointer == 9){
          moveTable();
        }
        strikeTimes[strikePointer] = strikeTime;
        char strdist[10];
        sprintf(strdist, "%u", lightning_dist_km);
        client.publish(String(storage.mqtt_subject) + "strike_distance",strdist);
        Serial.print(F("Strike distance to MQTT: "));
        Serial.println(strdist);
        strikeDiff = (strikeTimes[9] - strikeTimes[0])/1000;
        if ( strikeDiff < 1800) {
          sprintf(strdist, "%u", strikeDiff);
          client.publish(String(storage.mqtt_subject) + "10_strikes_per_sec",strdist);
          Serial.print(F("10 strikes per: "));
          Serial.println(strdist);
        }
      }
    }
  } else if (int_src == DISTURBER_INT) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0 * lineHeight);
    Serial.println(F("Disturber detected"));
    tft.print(F("Disturber detected"));
    distPulses++;
  } else if (int_src == NOISE_INT) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0 * lineHeight);
    Serial.println(F("Noise level too high"));
    tft.print(F("Noise level too high"));
  }
  //lightning.clearStatistics(true);
  delay(500);
}

void moveMinutes() {
  for (int i = 0; i < 59; i++) {
    minutes[59 - i] = minutes[58 - i];
  }
  minutes[0] = 0;
  minuteBeeped = 0;
}

void moveExamples() {
  for (int i = 0; i < ExampleCounter -1 ; i++) {
    examples[(ExampleCounter-1) - i] = examples[(ExampleCounter-2) - i];
  }
  examples[0] = random(0, ExampleMax);
}

void moveHours() {
  for (int i = 0; i < 23; i++) {
    hours[23 - i] = hours[22 - i];
  }
  hours[0] = 0;
}

void moveDays() {
  for (int i = 0; i < 29; i++) {
    days[29 - i] = days[28 - i];
  }
  days[0] = 0;
}

void moveTable() {
  for (strikePointer = 0; strikePointer < 9; strikePointer++) {
    strikeTimes[strikePointer] = strikeTimes[strikePointer+1];
  }
}

void showTime() {
  Serial.print(' ');
  if (hour(local_time) < 10) {
    Serial.print('0');
  }
  Serial.print(hour(local_time), DEC);
  Serial.print(':');
  if (minute(local_time) < 10) {
    Serial.print('0');
  }
  Serial.print(minute(local_time), DEC);
  Serial.print(':');
  if (second(local_time) < 10) {
    Serial.print('0');
  }
  Serial.print(second(local_time), DEC);
  Serial.print(' ');
}

void dispTime(byte line, byte dw, byte hr, byte mn, byte sc) {
  tft.setCursor(0, line * lineHeight);
  tft.print(F(dayShortStr(dw)));

  tft.setCursor(40, line * lineHeight);
  if (hr < 10) {
    tft.print('0');
  }
  tft.print(hr, DEC);
  tft.print(':');
  if (mn < 10) {
    tft.print('0');
  }
  tft.print(mn, DEC);
  if (sc < 99) {
    tft.print(':');
    if (sc < 10) {
      tft.print('0');
    }
    tft.print(sc, DEC);
  }
}

void setup() {
  pinMode(BEEPER, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(AS3935_intPin, INPUT);
  digitalWrite(LED, 0);

  configure_timer();

  digitalWrite(BEEPER, beepOff);
  if (storage.beeperCnt > 0) SingleBeep(2);

  Serial.begin(115200);
  Serial.printf("AS3935 Franklin Lightning Detector v%d.%d\r\n",majorVersion, minorVersion);
  Serial.println(F("beginning boot procedure...."));
  Serial.println(F("Start tft"));
  tft.begin();
  tft.setRotation(screenRotation);
  uint16_t calData[5] = { 304, 3493, 345, 3499, 4 };
  tft.setTouch(calData);

  tft.fillScreen(TFT_BLACK);
  printLogo(0);

  if (!EEPROM.begin(EEPROM_SIZE)) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
    tft.println(F("failed to initialise EEPROM"));
    Serial.println(F("failed to initialise EEPROM"));
    while (1)
      ;
  }
  if (EEPROM.read(offsetEEPROM) != storage.chkDigit) {
    Serial.println(F("Writing defaults...."));
    saveConfig();
  }

  loadConfig();
  printConfig();

  delay(1000);
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(0, 0);
  tft.println(F("Starting"));

  Serial.println(F("Type GS to enter setup:"));
  tft.println(F("Wait for setup"));
  delay(5000);
  if (Serial.available()) {
    Serial.println(F("Check for setup"));
    if (Serial.find(chkGS)) {
      tft.println(F("Setup entered"));
      Serial.println(F("Setup entered..."));
      setSettings(1);
    }
  }

  // setup for the the I2C library: (enable pullups, set speed to 400kHz)
  Serial.println(F("Wire start"));
  Wire.begin();
  delay(2);
  tft.println(F("Starting detector"));
  Serial.println(F("Lightning start"));
  if (!lightning.begin()) {  // Initialize the sensor.
    tft.println(F("Detector not started"));
    Serial.println("Lightning Detector did not start up, freezing!");
    delay(5000);
    esp_restart();
  }

  tft.println(F("Set detector params"));
  bool setAS3935 = check_AS3935(1);
  while (!setAS3935) {
    tft.print(F("."));
    delay(1000);
    lightning.resetSettings();
    lightning.maskDisturber(storage.AS3935_distMode);
    if (storage.AS3935_doorMode == 0) lightning.setIndoorOutdoor(INDOOR);
    else lightning.setIndoorOutdoor(OUTDOOR);
    lightning.setNoiseLevel(storage.AS3935_noiseFloorLvl);
    lightning.watchdogThreshold(storage.AS3935_watchdogThreshold);
    lightning.spikeRejection(storage.AS3935_spikeRejection);
    lightning.lightningThreshold(storage.AS3935_lightningThresh);
    lightning.tuneCap(storage.AS3935_capacitance);
    lightning.changeDivRatio(storage.AS3935_divisionRatio);
    setAS3935 = check_AS3935(1);
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
  for (int i = 0; i < ExampleCounter -1 ; i++) {examples[i] = 0;}
  for (int i = 0; i < 30 ; i++) {days[i] = 0;}
  for (int i = 0; i < 24 ; i++) {hours[i] = 0;}
  for (int i = 0; i < 60 ; i++) {minutes[i] = 0;}
  Serial.println();
  Serial.println(F("Array cleared...."));

  if (storage.use_MQTT){
    tft.println(F("Start MQTT"));
    client.begin(storage.mqtt_broker, storage.mqtt_port, net);
    Serial.println(F("Started MQTT"));
  }

  Serial.println(F("Start WiFi"));
  tft.println(F("Start WiFi"));

  int maxNetworks = (sizeof(wifiNetworks) / sizeof(wlanSSID));
  for (int i = 0; i < maxNetworks; i++ )
    wifiMulti.addAP(wifiNetworks[i].SSID, wifiNetworks[i].PASSWORD);
  wifiMulti.addAP(storage.ESP_SSID,storage.ESP_PASS);

  if (check_connection()){
    getNTPData();
    boot_time = local_time;
    sendToSite(0, 0);
    char buff[20];
    sprintf(buff, "%2d/%2d/%2d %2d:%2d:%2d", day(boot_time), month(boot_time), year(boot_time), hour(boot_time), minute(boot_time), second(boot_time));
    
    if (storage.use_MQTT){
      client.publish(String(storage.mqtt_subject) + "/started", buff);
    }
  }
  tft.fillScreen(TFT_BLACK);
  printInfo();
  for (int i = 0; i<sizeof(examples); i++) examples[i] = random(0, ExampleMax);
}

bool check_AS3935(bool doCheck) {
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
  if (doorMode == INDOOR) {
    Serial.println("Indoor.");
    doorValue = 0;
  } else if (doorMode == OUTDOOR) {
    Serial.println("Outdoor.");
    doorValue = 1;
  } else {
    Serial.println(doorMode, BIN);
    doorValue = -1;
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
  Serial.print("The number of strikes before interrupt is triggered: ");
  Serial.println(lightningThresh);

  byte capacitance = lightning.readTuneCap();
  Serial.print("Internal Capacitor is set to: ");
  Serial.println(capacitance);

  byte divRatio = lightning.readDivRatio();
  Serial.print("Divider Ratio is set to: ");
  Serial.println(divRatio);
  if (doCheck)
    if (distMode == storage.AS3935_distMode)
      if (doorValue == storage.AS3935_doorMode)
        if (capacitance == storage.AS3935_capacitance)
          if (noiseFloorLvl == storage.AS3935_noiseFloorLvl)
            if (watchdogThreshold == storage.AS3935_watchdogThreshold)
              if (spikeRejection == storage.AS3935_spikeRejection)
                if (lightningThresh == storage.AS3935_lightningThresh)
                  result = true;
  if (!doCheck) result = true;
  Serial.println();
  return result;
}

void configure_timer() {
  timeTimer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timeTimer, &updateTime, false);  //attach callback
  timerAlarmWrite(timeTimer, 1000 * 1000, true);        //set time in us
  timerAlarmEnable(timeTimer);                          //enable interrupt
}

boolean check_connection() {
  if (WiFi.status() != WL_CONNECTED) {
    InitWiFiConnection();
  }

  bool mqttConnected = true;
  if (storage.use_MQTT){
    if (WiFi.status() == WL_CONNECTED) {
      if (!client.connected()) {
        InitMQTTConnection();
      }
    }
    mqttConnected = client.connected();
  }

  return (WiFi.status() == WL_CONNECTED) && mqttConnected;
}

void InitWiFiConnection() {
  long startTime = millis();
  while (wifiMulti.run() != WL_CONNECTED && millis()-startTime<30000){
    delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.print("Connected to: ");
    Serial.println(WiFi.SSID());
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
  }
}

void InitMQTTConnection() {
  Serial.println(F("Connecting to MQTT..."));
  while (!client.connect(storage.MyCall, storage.mqtt_user, storage.mqtt_pass)) {
    Serial.print(".");
    delay(100);
  }

  if (client.connected()) {
    Serial.print(F("MQTT connected to: "));
    Serial.println(storage.mqtt_broker);
  }
}

void sendToSite(byte whichInt, byte dist) {
  if (check_connection()) {
    Serial.println("Send to site");
    String getData = String("http://onweer.pi4raz.nl/AddEvent.php?Callsign=") + String(storage.MyCall) + String("&IntType=") + whichInt + String("&Distance=") + dist;

    Serial.println(getData);
    http.begin(getData);                  //Specify request destination
    int httpCode = http.GET();            //Send the request
    if (httpCode > 0) {                   //Check the returning code
      String payload = http.getString();  //Get the request response payload
      Serial.println(payload);  //Print the response payload
    }
    http.end();
  }
}

void getNTPData() {
  if (check_connection()) {
    syncTime();
    local_time = TIMEZONE.toLocal(now(), &tz1_Code);   
  }
}
