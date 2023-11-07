StoreStruct storage = {
		'@',                  // chkDigit;
		"MARODEKWiFi",        // ESP_SSID[16];
		"MAROWiFi19052004!",  // ESP_PASS[27];
		"PA2RDK",             // MyCall[10];
		"mqtt.rjdekok.nl",    // mqtt_broker[50];
		"Robert",             // mqtt_user[25];
		"Mosq5495!",          // mqtt_pass[25];
		1883,                 // mqtt_port;
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

wlanSSID wifiNetworks[] {
    {"PI4RAZ","PI4RAZ_Zoetermeer"},
    {"Loretz_Gast", "Lor_Steg_98"}
};

int screenRotation          = 3; // 0=0, 1=90, 2=180, 3=270
