// *************************************************************************************
//  V6.0  WhatsApp implemented
//  V5.3  Noisefloor levels aangepast
//  V5.2  AutoTune - Finetuning
//  V5.0  AutoTune - Thanks to PA3CNO
//  V4.7  Fixed IN/OUTDOOR
//  V4.6  OTA
//  V4.3  Auto backlight
//  V4.2  IP info on Info screen
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
#include "driver/pcnt.h"
#include <UrlEncode.h>

#include "Free_Fonts.h"  // Include the header file attached to this sketch
#include <TFT_eSPI.h>    // https://github.com/Bodmer/TFT_eSPI
#include "NTP_Time.h"
#include <RDKOTA.h>

#define LED 14
#define lineHeight 18
#define AS3935_intPin 26

#define AS3935_ADDR 0x03
#define INDOOR 0x12
#define OUTDOOR 0xE
#define LIGHTNING_INT 0x08
#define DISTURBER_INT 0x04
#define NOISE_INT 0x01
#define ANTFREQ 0x03

#define BEEPER 32
#define beepOn 1
#define beepOff 0

#define FROMTIME 0
#define FROMMENU 1
#define FROMLIGHTNING 2
#define FROMNOTHING 3

#define wdtTimeout 60
#define dispStat 0
#define dispHist 1
#define dispInfo 2
#define dispExamples 0
#define dispMinute 3
#define dispHour 4
#define dispDay 5
#define dispMax 5
#define ExampleCounter 24
#define ExampleScale 6
#define ExampleMax 18

#define offsetEEPROM 0x10
#define EEPROM_SIZE 400
#define TIMEZONE euCET

#define OTAHOST "https://www.rjdekok.nl/Updates/RAZLightningESP"
#define VERSION "v6.0"

#define PCNT_TEST_UNIT PCNT_UNIT_0  // Use Pulse Count Unit 0
#define PCNT_H_LIM_VAL 32767        // Upper limit 32767
#define PCNT_L_LIM_VAL -32767       // Lower limit -32767
#define PCNT_INPUT_SIG_IO 26        // Pulse Input GPIO 26; is AS3935 interrupt pin

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
  bool useWapp;
  char wappPhone[15];
  char wappAPI[35];
  int wappInterval;
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
  bool calibrate;
  bool isDebug;  
  bool doRotate;
  bool rotateTouch;
};

typedef struct {  // WiFi Access
  const char *SSID;
  const char *PASSWORD;
} wlanSSID;

//#include "RDK_Settings.h"
#include "All_Settings.h"

char receivedString[128];
char chkGS[3] = "GS";

byte minutes[60];
byte hours[24];
byte days[30];
unsigned long strikeTimes[11];
byte examples[ExampleCounter];

unsigned long strikeTime;
unsigned long strikeDiff;
unsigned int strikePointer = 0;
uint16_t pulses = 0;
uint16_t distPulses = 0;
byte minuteBeeped = 0;
int updCounter = 0;
long DisplayOnTime = millis();
long lastWappMessage = millis();

int16_t counter = 0;  // Pulse counter - max value 32767
//float frequency = 0;
//float previousfreq = 0;
//float percentage;
// String units = "Hz";                // Frequency units. Not used in this application
// int previouscap = 0;                // To store previous tuning attempt
unsigned long timebase = 1000000;  // Timebase value in us
unsigned int prescaler = 80;       // Timer prescaler = 80 MHz / 80 = 1 MHz
int scale = 4;                     // Measuring scale
bool controle = false;             // Set scale or not
bool overflow = false;             // Indicates Pulse Counter overflow

pcnt_isr_handle_t user_isr_handle = NULL;  // Interrupt routine handler

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
RDKOTA rdkOTA(OTAHOST);
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
  tft.print(F("Detector "));
  tft.print(VERSION);
  if (withInfo) {
    printInfo();
  }
}

void printInfo() {
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(0, (4 * lineHeight) + 15);
  tft.print(F("Info:"));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  //tft.print(F("  HBC="));
  tft.setCursor(0, (6 * lineHeight) + 15);
  tft.print(F("Call:"));
  tft.print(storage.MyCall);
  tft.setCursor(0, (7 * lineHeight) + 15);
  tft.print(F("Mode:"));
  if (storage.AS3935_doorMode == INDOOR) tft.print(F("Indoor"));
  else tft.print(F("Outdoor"));
  tft.setCursor(0, (8 * lineHeight) + 15);
  tft.print(F("Dist:"));
  if (storage.AS3935_distMode == 1) tft.print(F("Enabled"));
  else tft.print(F("Disabled"));
  tft.setCursor(0, (9 * lineHeight) + 15);
  tft.print(F("Boot:"));
  printTime(boot_time, TFT_WHITE, false);
  tft.setCursor(0, (10 * lineHeight) + 15);
  tft.print(F("WiFi:"));
  tft.print(WiFi.SSID());
  tft.setCursor(0, (11 * lineHeight) + 15);
  tft.print(F("MyIP:"));
  tft.print(WiFi.localIP());
}

void printStat(int dispGraph) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(3);
  tft.setCursor(0, 0 * lineHeight);
  tft.print(F("Statistics:"));
  tft.setTextSize(2);
  tft.setCursor(0, (2 * lineHeight) - 8);
  printTime(local_time, TFT_RED, true);
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
  if (dispGraph == dispExamples) printGraph(examples, ExampleCounter, ExampleScale, TFT_GREEN, "Examples");
  if (dispGraph == dispMinute) printGraph(minutes, 60, 12, TFT_YELLOW, "Minutes");
  if (dispGraph == dispHour) printGraph(hours, 24, 12, TFT_ORANGE, "Hours");
  if (dispGraph == dispDay) printGraph(days, 30, 6, TFT_DARKCYAN, "Days");
}

void printGraph(byte graphArray[], int lenArray, int scale, uint32_t lColor, String gHeader) {

  tft.fillRect(0, 130, 320, 110, TFT_BLACK);
  tft.drawLine(20, 220, 20, 140, lColor);
  tft.drawLine(20, 220, 290, 220, lColor);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(lColor);
  tft.drawString(gHeader, 160, 150, GFXFF);

  tft.setTextSize(1);
  int hStart = 20;
  for (int i = 0; i < scale + 1; i++) {
    int xPos = 20 + (float)270 / scale * i;
    int xTxt = lenArray - ((lenArray / scale) * i);
    tft.drawString(String(xTxt), xPos, 230, GFXFF);
  }

  int vMax = 1;
  for (int i = 0; i < lenArray; i++) {
    if (graphArray[i] > vMax) vMax = graphArray[i];
  }

  for (int i = 0; i < 5; i++) {
    int yPos = 140 + (i * 20);
    float yTxt = (((float)vMax / 4) * (4 - i));
    char buff[5];
    if (yTxt < 10) sprintf(buff, "%.1f", yTxt);
    if (yTxt > 9) sprintf(buff, "%.0f", yTxt);
    tft.drawString(buff, 10, yPos, GFXFF);
  }

  float hScale = (float)270 / (lenArray - 1);
  float vScale = (float)80 / vMax;

  int lastX, lastY;
  for (int i = 0; i < lenArray; i++) {
    int x = 290 - (i * hScale);
    int y = 220 - (graphArray[i] * vScale);
    //Serial.printf("Pixel %d with value  %d on %d,%d (max = %d)\r\n",i, graphArray[i], x, y, vMax);
    if (i == 0) tft.drawPixel(x, y, TFT_RED);
    else tft.drawLine(lastX, lastY, x, y, TFT_RED);
    lastX = x;
    lastY = y;
  }
}

void printTime(time_t thisTime, int dispColor, bool showDay) {
  tft.setTextColor(dispColor);
  if (showDay) {
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
  if (millis() - loopCheck > 5000) {
    loopCheck = millis();
    if (check_connection()) {
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

  if (millis() - DisplayOnTime > 30000) {
    turnOnOffLed("Off");
    DisplayOnTime = millis();
  }

  fromSource = FROMNOTHING;

  uint16_t touchX = 0, touchY = 0;
  bool pressed = tft.getTouch(&touchX, &touchY);
  bool doMenu = false;
  if (pressed) {
    turnOnOffLed("On");
    DisplayOnTime = millis();

    Serial.printf("Position x:%d, y:%d\r\n", touchX, touchY);

    if (touchY<50 && touchX<50) ESP.restart();

    SingleBeep(1);
    doMenu = true;
  }

  if (doMenu) {
    fromSource = FROMMENU;
    handleMenu();
  }

  int intVal = 0;
  if (digitalRead(AS3935_intPin) == HIGH) {
    turnOnOffLed("On");
    DisplayOnTime = millis();

    fromSource = FROMLIGHTNING;
    intVal = lightning.readInterruptReg();
    Serial.printf("Lightning interrupt:%d\r\n", intVal);
    handleLighting(intVal);
  }

  if (fromSource < FROMNOTHING) dispData();
  if (storage.use_MQTT) {
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

void turnOnOffLed(String ledStatus) {
  Serial.printf("Backlight turned %s.\r\n", ledStatus);
  digitalWrite(LED, ledStatus == "On" ? 0 : 1);
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

  if (storage.use_MQTT) {
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

  Serial.print(F("Use WhatsApp (0 -1) ("));
  Serial.print(storage.useWapp);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.useWapp = i;
  }
  Serial.println();

  if (storage.useWapp){
    Serial.print(F("WhatsApp phone ("));
    Serial.print(storage.wappPhone);
    Serial.print(F("):"));
    if (doAsk == 1) {
      getStringValue(49);
      if (receivedString[0] != 0) {
        storage.wappPhone[0] = 0;
        strcat(storage.wappPhone, receivedString);
      }
    }
    Serial.println();

    Serial.print(F("WhatsApp API Key ("));
    Serial.print(storage.wappAPI);
    Serial.print(F("):"));
    if (doAsk == 1) {
      getStringValue(49);
      if (receivedString[0] != 0) {
        storage.wappAPI[0] = 0;
        strcat(storage.wappAPI, receivedString);
      }
    }
    Serial.println();

    Serial.print(F("WhatsApp interval ("));
    Serial.print(storage.wappInterval);
    Serial.print(F("):"));
    if (doAsk == 1) {
      i = getNumericValue();
      if (receivedString[0] != 0) storage.wappInterval = i;
    }
    Serial.println();
  }

  Serial.print(F("Indoors=0 or Outdoors=1 ("));
  if (storage.AS3935_doorMode == INDOOR) {
    Serial.print(F("Indoors"));
  } else {
    Serial.print(F("Outdoors"));
  }
  Serial.print(" - ");
  Serial.print(storage.AS3935_doorMode); 
  Serial.print(F("): "));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) {
      if (i == 1) storage.AS3935_doorMode = OUTDOOR;
      else storage.AS3935_doorMode = INDOOR;
    }
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

  Serial.print(F("Capacity (0, 8, 16, 24, 32...120)("));
  Serial.print(storage.AS3935_capacitance);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.AS3935_capacitance = i;
  }
  Serial.println();

  Serial.print(F("Division ratio (16, 32, 64, or 128)("));
  Serial.print(storage.AS3935_divisionRatio);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.AS3935_divisionRatio = i;
  }
  Serial.println();

  Serial.print(F("Noiselevel (0 - 7) ("));
  Serial.print(storage.AS3935_noiseFloorLvl);
  Serial.print(F("):"));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0 && i>=0 && i <8) storage.AS3935_noiseFloorLvl = i;
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

  Serial.print(F("Calibrate detector (0/1) ("));
  if (storage.calibrate == 0) {
    Serial.print(F("Disabled"));
  } else {
    Serial.print(F("Enabled"));
  }
  Serial.print(F("): "));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.calibrate = i;
  }
  Serial.println();

  Serial.print(F("Debugmode (0/1) ("));
  if (storage.isDebug == 0) {
    Serial.print(F("Disabled"));
  } else {
    Serial.print(F("Enabled"));
  }
  Serial.print(F("): "));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.isDebug = i;
  }
  Serial.println();

  Serial.print(F("Rotate screen (0/1) ("));
  if (storage.doRotate == 0) {
    Serial.print(F("Disabled"));
  } else {
    Serial.print(F("Enabled"));
  }
  Serial.print(F("): "));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.doRotate = i;
  }
  Serial.println();

  Serial.print(F("Rotate touch (0/1) ("));
  if (storage.rotateTouch == 0) {
    Serial.print(F("Disabled"));
  } else {
    Serial.print(F("Enabled"));
  }
  Serial.print(F("): "));
  if (doAsk == 1) {
    i = getNumericValue();
    if (receivedString[0] != 0) storage.rotateTouch = i;
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

void IRAM_ATTR pcnt_example_intr_handler(void *arg)  // Pulse Counter overflow interrupt routine
{
  overflow = true;                         // Indicates counter overflow
  PCNT.int_clr.val = BIT(PCNT_TEST_UNIT);  // Point to overflow flag
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
      Serial.printf("%d %d %d:%d Dist:%d KM\r\n", lastData[i].dw, lastData[i].hr, lastData[i].mn, lastData[i].sc, lastData[i].dt);
    }
    if (storage.beeperCnt > 0 && minutes[0] >= storage.beeperCnt && minuteBeeped == 0) {
      SingleBeep(5);
      minuteBeeped++;
      char buff[20];
      sprintf(buff, "%02d/%02d/%02d %02d:%02d:%02d", day(local_time), month(local_time), year(local_time), hour(local_time), minute(local_time), second(local_time));
      if (storage.use_MQTT) {
        client.publish(String(storage.mqtt_subject) + "/lightning/datetime", buff);
        client.publish(String(storage.mqtt_subject) + "/lightning/distance", (String)lightning_dist_km + "KM");
      }
      if (storage.useWapp){
        if (millis()-lastWappMessage>storage.wappInterval*1000){
          sendWapp("Lightning at " + (String)lightning_dist_km + "KM (" + String(buff)+")");
          lastWappMessage = millis();
        }
      }
    }

    if (storage.use_MQTT) {  //Frank z'n 10 strikes per second warning
      if (check_connection()) {
        strikeTime = millis();
        if (strikeTime < strikeTimes[0]) strikePointer = 0;  // more than 50 days up
        if (strikePointer < 9) {
          strikeTimes[strikePointer++] = strikeTime;
        }
        if (strikePointer == 9) {
          moveTable();
        }
        strikeTimes[strikePointer] = strikeTime;
        char strdist[10];
        sprintf(strdist, "%u", lightning_dist_km);
        client.publish(String(storage.mqtt_subject) + "strike_distance", strdist);
        Serial.print(F("Strike distance to MQTT: "));
        Serial.println(strdist);
        strikeDiff = (strikeTimes[9] - strikeTimes[0]) / 1000;
        if (strikeDiff < 1800) {
          sprintf(strdist, "%u", strikeDiff);
          client.publish(String(storage.mqtt_subject) + "10_strikes_per_sec", strdist);
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

void sendWapp(String text){
  // Data to send with HTTP POST
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + String(storage.wappPhone) + "&apikey=" + String(storage.wappAPI) + "&text=" + urlEncode(text);
  Serial.println(url);
  HTTPClient http;
  http.begin(url);

  // Specify content-type header
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Send HTTP POST request
  int httpResponseCode = http.POST(url);
  if (httpResponseCode == 200) {
    Serial.println("WHATSAPP Message sent successfully");
  }
  else {
    Serial.println("Error sending the WHATSAPP message");
    Serial.print("HTTP response code: ");
    Serial.println(httpResponseCode);
  }
  // Free resources
  http.end();
}

void moveMinutes() {
  for (int i = 0; i < 59; i++) {
    minutes[59 - i] = minutes[58 - i];
  }
  minutes[0] = 0;
  minuteBeeped = 0;
}

void moveExamples() {
  for (int i = 0; i < ExampleCounter - 1; i++) {
    examples[(ExampleCounter - 1) - i] = examples[(ExampleCounter - 2) - i];
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
    strikeTimes[strikePointer] = strikeTimes[strikePointer + 1];
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
  turnOnOffLed("On");

  digitalWrite(BEEPER, beepOff);
  if (storage.beeperCnt > 0) SingleBeep(2);

  Serial.begin(115200);
  Serial.printf("AS3935 Franklin Lightning Detector %s\r\n", VERSION);
  Serial.println(F("beginning boot procedure...."));
  Serial.println(F("Start tft"));

  if (!EEPROM.begin(EEPROM_SIZE)) {
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

  tft.begin();
  tft.init();
  tft.setRotation(storage.doRotate?1:3);
  uint16_t calData[5] = { 304, 3493, 345, 3499, storage.rotateTouch?7:1 };
  tft.setTouch(calData);

  tft.fillScreen(TFT_BLACK);
  printLogo(0);

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

  lightning.resetSettings();
  tft.print(F("."));
  delay(1000);

  tft.println(F("Set detector params"));
  bool setAS3935 = check_AS3935(1);
  while (!setAS3935) {
    delay(1000);
    lightning.maskDisturber(storage.AS3935_distMode);
    lightning.setIndoorOutdoor(storage.AS3935_doorMode == INDOOR ? INDOOR : OUTDOOR);
    lightning.setNoiseLevel(storage.AS3935_noiseFloorLvl);
    lightning.watchdogThreshold(storage.AS3935_watchdogThreshold);
    lightning.spikeRejection(storage.AS3935_spikeRejection);
    lightning.lightningThreshold(storage.AS3935_lightningThresh);
    lightning.tuneCap(storage.AS3935_capacitance);
    lightning.changeDivRatio(storage.AS3935_divisionRatio);
    setAS3935 = check_AS3935(1);
  }

  uint16_t touchX = 0, touchY = 0;
  bool pressed = tft.getTouch(&touchX, &touchY);
  if (storage.calibrate || pressed) {
    Serial.println(F("Start calibrating"));
    tft.println(F("Start calibrating"));
    pcnt_init();       // Initialize interrupt
    calibrateTimer();  // Interrupt for freq. counter
    storage.AS3935_capacitance = calcCapacity();
    tft.print(F("Found capacity:"));
    tft.print(storage.AS3935_capacitance);
    tft.println(F(" pF"));
    timerEnd(timeTimer);
    storage.calibrate = 0;
    saveConfig();
    printConfig();
  }

  if(lightning.calibrateOsc()) {
    Serial.println("Successfully Calibrated!");
  } else {
    Serial.println("Not Successfully Calibrated!");
  }
  runTimer(); // Interrupt for lightning interrupts

  Serial.print(F("Clear array...."));
  for (int i = 0; i < 10; i++) {
    lastData[i].dw = 0;
    lastData[i].hr = 0;
    lastData[i].mn = 0;
    lastData[i].sc = 0;
    lastData[i].dt = 0;
    Serial.print(F("."));
  }
  for (int i = 0; i < ExampleCounter - 1; i++) { examples[i] = 0; }
  for (int i = 0; i < 30; i++) { days[i] = 0; }
  for (int i = 0; i < 24; i++) { hours[i] = 0; }
  for (int i = 0; i < 60; i++) { minutes[i] = 0; }
  Serial.println();
  Serial.println(F("Array cleared...."));

  if (storage.use_MQTT) {
    tft.println(F("Start MQTT"));
    client.begin(storage.mqtt_broker, storage.mqtt_port, net);
    Serial.println(F("Started MQTT"));
  }

  Serial.println(F("Start WiFi"));
  tft.println(F("Start WiFi"));

  int maxNetworks = (sizeof(wifiNetworks) / sizeof(wlanSSID));
  for (int i = 0; i < maxNetworks; i++)
    wifiMulti.addAP(wifiNetworks[i].SSID, wifiNetworks[i].PASSWORD);
  wifiMulti.addAP(storage.ESP_SSID, storage.ESP_PASS);

  if (check_connection()) {
    if (rdkOTA.checkForUpdate(VERSION)) {
      if (questionBox("Installeer update", TFT_WHITE, TFT_NAVY, 5, 100, 310, 48)) {
        messageBox("Installing update", TFT_YELLOW, TFT_NAVY, 5, 100, 310, 48);
        rdkOTA.installUpdate();
      }
    }

    getNTPData();
    boot_time = local_time;
    sendToSite(0, 0);
    char buff[20];
    sprintf(buff, "%02d/%02d/%02d %02d:%02d:%02d", day(boot_time), month(boot_time), year(boot_time), hour(boot_time), minute(boot_time), second(boot_time));

    if (storage.use_MQTT) {
      client.publish(String(storage.mqtt_subject) + "/started", buff);
    }

    if (storage.useWapp){
      sendWapp("Lightningdetector booted at " + String(buff));
    }
  }
  tft.fillScreen(TFT_BLACK);
  printInfo();
  for (int i = 0; i < sizeof(examples); i++) examples[i] = random(0, ExampleMax);
  DisplayOnTime = millis();
}

bool check_AS3935(bool doCheck) {
  bool result = false;

  Serial.printf("Disturbers being masked:%s\r\n", lightning.readMaskDisturber() ? "YES" : "NO");
  Serial.printf("Are we set for indoor or outdoor:%s.\r\n", lightning.readIndoorOutdoor() == INDOOR ? "Indoor" : lightning.readIndoorOutdoor() == OUTDOOR ? "Outdoor"
                                                                                                                                                          : "Undefined");
  Serial.printf("Internal Capacitor is set to:%d\r\n", lightning.readTuneCap());
  Serial.printf("Noise Level is set at:%d\r\n", lightning.readNoiseLevel());
  Serial.printf("Watchdog Threshold is set to:%d\r\n", lightning.readWatchdogThreshold());
  Serial.printf("Spike Rejection is set to:%d\r\n", lightning.readSpikeRejection());
  Serial.printf("The number of strikes before interrupt is triggered:%d\r\n", lightning.readLightningThreshold());
  Serial.printf("Divider Ratio is set to:%d\r\n", lightning.readDivRatio());

  Serial.printf("distMode %d,%d\r\n", lightning.readMaskDisturber(), storage.AS3935_distMode);
  Serial.printf("In/Outdoor %d,%d\r\n", lightning.readIndoorOutdoor(), storage.AS3935_doorMode);
  Serial.printf("capacitance %d,%d\r\n", lightning.readTuneCap(), storage.AS3935_capacitance);
  Serial.printf("noiseFloorLvl %d,%d\r\n", lightning.readNoiseLevel(), storage.AS3935_noiseFloorLvl);
  Serial.printf("watchdogThreshold %d,%d\r\n", lightning.readWatchdogThreshold(), storage.AS3935_watchdogThreshold);
  Serial.printf("spikeRejection %d,%d\r\n", lightning.readSpikeRejection(), storage.AS3935_spikeRejection);
  Serial.printf("lightningThresh %d,%d\r\n", lightning.readLightningThreshold(), storage.AS3935_lightningThresh);
  Serial.printf("Divider Ratio %d,%d\r\n", lightning.readDivRatio(), storage.AS3935_divisionRatio);

  if (doCheck)
    if (lightning.readMaskDisturber() == storage.AS3935_distMode)
      if (lightning.readIndoorOutdoor() == storage.AS3935_doorMode)
        if (lightning.readTuneCap() == storage.AS3935_capacitance)
          if (lightning.readNoiseLevel() == storage.AS3935_noiseFloorLvl)
            if (lightning.readWatchdogThreshold() == storage.AS3935_watchdogThreshold)
              if (lightning.readSpikeRejection() == storage.AS3935_spikeRejection)
                if (lightning.readLightningThreshold() == storage.AS3935_lightningThresh)
                  if (lightning.readDivRatio() == storage.AS3935_divisionRatio)
                    result = true;

  if (!doCheck) result = true;
  Serial.println();
  return result;
}

void runTimer() {
  timeTimer = timerBegin(0, prescaler, true);           //timer 0, div 80
  timerAttachInterrupt(timeTimer, &updateTime, false);  //attach callback
  timerAlarmWrite(timeTimer, 1000 * 1000, true);        //set time in us
  timerAlarmEnable(timeTimer);                          //enable interrupt
}

void calibrateTimer()  // Time Base timer initialization routine
{
  timeTimer = timerBegin(0, prescaler, true);        // use timer 0, 80 MHz / prescaler, true = count up
  timerAttachInterrupt(timeTimer, &timeBase, true);  // address of the function to be called by the timer / true generates an interrupt
  timerAlarmWrite(timeTimer, timebase, true);        // Sets the count time 64 bits/true to repeat the alarm
  timerAlarmEnable(timeTimer);                       // Enables timer 0
}

boolean check_connection() {
  if (WiFi.status() != WL_CONNECTED) {
    InitWiFiConnection();
  }

  bool mqttConnected = true;
  if (storage.use_MQTT) {
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
  while (wifiMulti.run() != WL_CONNECTED && millis() - startTime < 30000) {
    delay(1000);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
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
      Serial.println(payload);            //Print the response payload
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

/***************************************************************************************
**                          Draw messagebox with message
***************************************************************************************/
void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor) {
  messageBox(msg, fgcolor, bgcolor, 5, 240, 230, 24);
}

void messageBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w / 2, y + (h / 2));
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();
}

bool questionBox(const char *msg, uint16_t fgcolor, uint16_t bgcolor, int x, int y, int w, int h) {
  uint16_t current_textcolor = tft.textcolor;
  uint16_t current_textbgcolor = tft.textbgcolor;

  //tft.loadFont(AA_FONT_SMALL);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(fgcolor, bgcolor);
  tft.fillRoundRect(x, y, w, h, 5, fgcolor);
  tft.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, bgcolor);
  tft.setTextPadding(tft.textWidth(msg));
  tft.drawString(msg, w / 2, y + (h / 4));

  tft.fillRoundRect(x + 4, y + (h / 2) - 2, (w - 12) / 2, (h - 4) / 2, 5, TFT_GREEN);
  tft.setTextColor(fgcolor, TFT_GREEN);
  tft.setTextPadding(tft.textWidth("Yes"));
  tft.drawString("Yes", x + 4 + ((w - 12) / 4), y + (h / 2) - 2 + (h / 4));
  tft.fillRoundRect(x + (w / 2) + 2, y + (h / 2) - 2, (w - 12) / 2, (h - 4) / 2, 5, TFT_RED);
  tft.setTextColor(fgcolor, TFT_RED);
  tft.setTextPadding(tft.textWidth("No"));
  tft.drawString("No", x + (w / 2) + 2 + ((w - 12) / 4), y + (h / 2) - 2 + (h / 4));
  Serial.printf("Yes = x:%d,y:%d,w:%d,h:%d\r\n", x + 4, y + (h / 2) - 2, (w - 12) / 2, (h - 4) / 2);
  Serial.printf("No  = x:%d,y:%d,w:%d,h:%d\r\n", x + (w / 2) + 2, y + (h / 2) - 2, (w - 12) / 2, (h - 4) / 2);
  tft.setTextColor(current_textcolor, current_textbgcolor);
  tft.unloadFont();

  uint16_t touchX = 0, touchY = 0;

  long startWhile = millis();
  while (millis() - startWhile < 30000) {
    bool pressed = tft.getTouch(&touchX, &touchY);
    if (pressed) {
      Serial.printf("Pressed = x:%d,y:%d\r\n", touchX, touchY);
      if (touchY >= y + (h / 2) - 2 && touchY <= y + (h / 2) - 2 + ((h - 4) / 2)) {
        if (touchX >= x + 4 && touchX <= x + 4 + ((w - 12) / 2)) return true;
        if (touchX >= x + (w / 2) + 2 && touchX <= x + (w / 2) + 2 + ((w - 12) / 2)) return false;
      }
    }
  }
  return false;
}

int calcCapacity() {
  lightning.displayOscillator(true, ANTFREQ);
  float frequency = 0;
  float previousfreq = 0;
  float percentage;
  int capacitor = 120;
  long tuneFreq = 500000;
  String units = "Hz"; 
  int previouscap = 0;  // To store previous tuning attempt
  //scale = 4;
  while (capacitor >= 0) {
    tft.print(".");
    lightning.tuneCap(capacitor);
    delay(1000);
    if (counter > 0) {
      // setScale ();
      frequency = counter * storage.AS3935_divisionRatio;  // Calculate frequency
      Serial.printf("      Scale %d : %0.1f %s  Capacitance : %d\r\n", scale, frequency, units, capacitor);
    }
    if (frequency < tuneFreq) {
      previouscap = capacitor;
      previousfreq = frequency;
      capacitor -= 8;
    } else {
      Serial.printf("Frequency = %0.1f %s, Capacitance = %d\r\n", frequency, units, capacitor);
      Serial.printf("Previous Frequency = %0.1f %s, Previous Capacitance = %d\r\n", previousfreq, units, previouscap);
      if ((frequency - tuneFreq) > (tuneFreq - previousfreq)) {  // Is current freq closer to 500kHz or previous freq?
        capacitor = previouscap;
        frequency = previousfreq;
      }
      break;
    }  // Give timer time to fill counter
  }
  if (capacitor < 0) capacitor = 0;
  lightning.tuneCap(capacitor);

  percentage = 100 * abs(tuneFreq - frequency) / tuneFreq;
  Serial.println("Antenna tuned to best possible values:");
  Serial.printf("Frequency = %0.1f %s which deviates %0.1f %s from the center frequency. This is %0.2f%%\r\n", frequency, units, abs(tuneFreq - frequency), units, percentage);

  if (percentage > 3.5)
    Serial.println("This is more than the allowed 3.5%. This sensor is useless.");
  else
    Serial.println("This is within the allowed 3.5%. Using these values.");

  tft.println(".");
  lightning.displayOscillator(false, ANTFREQ);
  return (capacitor);
}

void pcnt_init()  // Pulse counter initialization
{
  pcnt_config_t pcnt_config = {};                  // Pulse instance configuration
  pcnt_config.pulse_gpio_num = PCNT_INPUT_SIG_IO;  // Pulse input = GPIO 26
  pcnt_config.pos_mode = PCNT_COUNT_INC;           // Pulse increment counter
  pcnt_config.lctrl_mode = PCNT_MODE_REVERSE;      // Reverses the count when the control pin is LOW
  pcnt_config.hctrl_mode = PCNT_MODE_KEEP;         // If control pin is HIGH, increment cpunt
  pcnt_config.counter_h_lim = PCNT_H_LIM_VAL;      // Max counter limit
  pcnt_config.counter_l_lim = PCNT_L_LIM_VAL;      // Min counter limit
  pcnt_unit_config(&pcnt_config);                  // Initialize PCNT

  pcnt_event_enable(PCNT_TEST_UNIT, PCNT_EVT_H_LIM);  // Enable PCNT event of PCNT unit - Enables maximum limit event = 1
  pcnt_event_enable(PCNT_TEST_UNIT, PCNT_EVT_L_LIM);  // Enable PCNT event of PCNT unit - Enables minimum limit event = 0
  pcnt_set_filter_value(PCNT_TEST_UNIT, 40);          // Filter config - Any pulses lasting shorter than this will be ignored
  pcnt_filter_enable(PCNT_TEST_UNIT);                 // Enable filter
  pcnt_counter_pause(PCNT_TEST_UNIT);                 // Pause PCNT counter of PCNT unit
  pcnt_counter_clear(PCNT_TEST_UNIT);                 // Clear and reset PCNT counter value to zero

  pcnt_isr_register(pcnt_example_intr_handler, NULL, 0, &user_isr_handle);  // ISR registering and PCNT interrupt handling
  pcnt_intr_enable(PCNT_TEST_UNIT);                                         // Enable PCNT interrupt for PCNT unit
  pcnt_counter_resume(PCNT_TEST_UNIT);                                      // Resume counting for PCNT counter
}

void timeBase()  // Pulse counter reading routine (Time base)
{
  pcnt_get_counter_value(PCNT_TEST_UNIT, &counter);  // get the pulse counter value - max value 32767
  pcnt_counter_clear(PCNT_TEST_UNIT);                // reset pulse counter
}

// void setScale (){
//   if (overflow == true)                                   // If PCNT overflow interrupt occurred
//   {
//     overflow = false;                                     // Disable new occurrence
//     controle = true;                                      // Enables scale change
//     Serial.println(" Overflow: ");                        // Print

//     scale = scale - 1;                                    // Decrement scale
//     if (scale < 1 )                                       // If you reached the minimum
//       scale = 0;                                          // minimum scale
//     selectScale();                                        // Calls scaling routine
//   }

//   if (counter > 32767)                                    // If frequency is greater than 32767
//   {
//     controle = true;                                      // Enables scale change
//     scale = scale - 1;                                    // Decrement scale
//     if (scale < 1)                                        // If you reached the minimum
//       scale = 4;                                          // Go to the maximum
//     selectScale();                                        // Calls scaling routine
//   }
//   Serial.print(" Counter = "); Serial.print(counter);     // Print
// }

// void selectScale()                                        // Scale change routine
// {
//   if (controle == true)                                   // If scale switching is enabled
//   {
//     controle = false;                                     // Disable on exchange
//     switch (scale)                                        // Select scale
//     {
//       case 1:                                             // Scalt 1
//         timebase = 1000;                                  // Timebase 1
//         scalingFactor = 1000;                             // Scaling factor
//         prescaler = 80;                                   // Precaler 1
//         units = "MHz       ";                             // MHz units
//         break;                                            // Ready and return

//       case 2:                                             // Scale 2 = from 327.670 Hz to 3.276.700 Hz
//         timebase = 10000;                                 // Timebase 2
//         scalingFactor = 0.1;                              // Scaling factor 100
//         prescaler = 80;                                   // Precaler 2
//         units = "kHz       ";                             // kHz units
//         break;                                            // Ready and return

//       case 3:                                             // Scale 3 = from 32767 Hz to 327670 Hz
//         timebase = 100000;                                // Timebase 3
//         prescaler = 80;                                   // Precaler 3
//         scalingFactor = 10;                               // Scaling factor 10
//         units = "Hz       ";                              // kHz units
//         break;                                            // Ready and return

//       case 4:                                             // Scale 4 = from 1 Hz to 32767 Hz
//         timebase = 1000000;                               // Timebase 4
//         prescaler = 80;                                   // Prescaler 4
//         scalingFactor = 1;                                // Scaling factor 1
//         units = "Hz       ";                              // Hz units
//         break;                                            // Ready and return
//     }
//   }
//   startTimer();                                           // Reset the timer
// }