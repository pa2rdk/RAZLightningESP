StoreStruct storage = {
  '?',                  // chkDigit;
  "yourSSID",           // ESP_SSID[16];
  "yourWiFiPass",       // ESP_PASS[27];
  "Pi4RAZ",             // MyCall[10];
  "mqtt.server.nl",     // mqtt_broker[50];
  "MQTTUserName",       // mqtt_user[25];
  "MqttPassword",       // mqtt_pass[25];
  "Lightningdetector",  // mqtt_subject[25];
  1883,                 // mqtt_port;
  1,                    // use_MQTT;
  0,                    // useWapp;
  "+31651847704",       // wappPhone;
  "WhatsAppAPI key",    // wappAPI;
  900,                  // wappInterval;  
  14,                   // AS3935_doorMode, 14 = outdoor
  1,                    // AS3935_distMode, 1 = enabled
  96,                   // AS3935_capacitance;
  16,                   // AS3935_divisionRatio
  2,                    // AS3935_noiseFloorLvl;
  2,                    // AS3935_watchdogThreshold;
  2,                    // AS3935_spikeRejection;
  1,                    // AS3935_lightningThresh;
  2,                    // beeperCnt;
  0,                    // dispScreen;
  1,                    // calibrate;  
  0,                    // isDebug;   
  0,                    // doRotate
  1                     // rotateTouch
};

wlanSSID wifiNetworks[]{
  { "PI4RAZ", "PI4RAZ_Zoetermeer" },
  { "Loretz_Gast", "Lor_Steg_98" }
};