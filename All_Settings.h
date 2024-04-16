StoreStruct storage = {
  '#',                  // chkDigit;
  "yourSSID",           // ESP_SSID[16];
  "yourWiFiPass",       // ESP_PASS[27];
  "Pi4RAZ",             // MyCall[10];
  "mqtt.server.nl",     // mqtt_broker[50];
  "MQTTUserName",       // mqtt_user[25];
  "MqttPassword",       // mqtt_pass[25];
  "Lightningdetector",  // mqtt_subject[25];
  1883,                 // mqtt_port;
  1,                    // use_MQTT;
  1,                    // AS3935_doorMode, 0 = indoor
  1,                    // AS3935_distMode, 1 = enabled
  96,                   // AS3935_capacitance;
  16,                   // AS3935_divisionRatio
  2,                    // AS3935_noiseFloorLvl;
  2,                    // AS3935_watchdogThreshold;
  2,                    // AS3935_spikeRejection;
  1,                    // AS3935_lightningThresh;
  2,                    // beeperCnt;
  0                     // dispScreen;
};

wlanSSID wifiNetworks[]{
  { "PI4RAZ", "PI4RAZ_Zoetermeer" },
  { "Loretz_Gast", "Lor_Steg_98" }
};

int screenRotation = 3;  // 0=0, 1=90, 2=180, 3=270
