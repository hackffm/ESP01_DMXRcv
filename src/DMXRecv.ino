#include <Arduino.h>
#if defined ESP32
#include <WiFi.h>
#include <esp_wifi.h>
#elif defined ESP8266
#include <ESP8266WiFi.h>
#define WIFI_MODE_STA WIFI_STA 
#else
#error "Unsupported platform"
#endif //ESP32

#include <QuickEspNow.h>
#include <elapsedMillis.h>
#include <espDMX.h>

#define ESPNOW_CHANNEL 13
#define ESPNOW_PREFIX "DMXA:"

// Commands: ? will answer via ESPNOW-Broadcast, rest will fill DMX
// DMXA:dddAABBCCDDEE... 
//      ddd=decimal DMX offset, starts with 1
//      AA=hex data

// ADJ UB 12H: RGBWAU (6x6Ch)

#define DEST_ADDR ESPNOW_BROADCAST_ADDRESS 

// Debug prints on Serial activ if defined
#define ESPNOW_TESTMODE 
#define ESPNOW_TESTMODE2

// Send something every 5s
//#define ESPNOW_AUTOSEND

const char espNowPrefix[] = ESPNOW_PREFIX;
int espNowPrefixLen;
volatile bool doAnswer = true;

byte dmx_data[514];
volatile int  new_dmx_start_addr = 0;
volatile int  new_dmx_len = 0;

uint8_t c2i(char b) {
	uint8_t d=16;
  if((b >= '0') && (b <= '9')) d = b - '0';
  else if((b >= 'a') && (b <= 'f')) d = b - 'a' + 10;
  else if((b >= 'A') && (b <= 'F')) d = b - 'A' + 10;
  return(d);
}

void dataReceived (uint8_t* address, uint8_t* data, uint8_t len, signed int rssi, bool broadcast) {
  static uint8_t msgBuf[256];
  memcpy(msgBuf, data, len); msgBuf[len] = 0;
  #ifdef ESPNOW_TESTMODE2
    Serial.print ("Received: ");
    Serial.printf ("%.*s\n", len, data);
    Serial.printf ("RSSI: %d dBm\n", rssi);
    Serial.printf ("From: " MACSTR "\n", MAC2STR (address));
    Serial.printf ("%s\n", broadcast ? "Broadcast" : "Unicast");
    Serial.printf ("Prefixlen %d\n", espNowPrefixLen);
  #endif
  if(len > espNowPrefixLen) {
    if(strncmp((const char *)msgBuf, espNowPrefix, espNowPrefixLen) == 0) {
      //data[len] = 0; // termination of string (not allowed, buffer will overrun!)
      if(msgBuf[espNowPrefixLen] == '?') doAnswer = true;
      // parse start
      uint8_t  b[3];
      uint8_t *p = msgBuf + espNowPrefixLen;
      uint8_t l = len - espNowPrefixLen;
      if(l>=5) {
        int addr = 0;
        b[0] = c2i(*p++);
        b[1] = c2i(*p++);
        b[2] = c2i(*p++);
        l -= 3;
        if((b[0] < 16) && (b[1] < 16) && (b[2] < 16)) {
          int nl = 0;
          int ndsa = -1;
          addr = b[0] * 100 + b[1] * 10 + b[2];
          #ifdef ESPNOW_TESTMODE2
          Serial.printf(" a:%d", addr);
          #endif
          while(l>1) {
            b[0] = c2i(*p++);
            b[1] = c2i(*p++);
            l -= 2; 
            if(nl != 0) nl++; 
            if((b[0] < 16) && (b[1] < 16)) {
              uint8_t d = b[0] * 16 + b[1];
              #ifdef ESPNOW_TESTMODE2
              Serial.printf(" d:%02x", (int)d);
              #endif
              if((addr > 0) && (addr <= 512)) {
                if(ndsa < 0) { ndsa = addr; nl++; } // start from first valid addr+data only
                dmx_data[addr-1] = d;
              }  
            }
            addr++;  
          }
          if((new_dmx_len == 0) && (nl > 0) && (ndsa > 0)) {
            new_dmx_len = nl;
            new_dmx_start_addr = ndsa; 
            #ifdef ESPNOW_TESTMODE2
              Serial.print(" TX");
            #endif
          }  
          #ifdef ESPNOW_TESTMODE2
            Serial.println(".");
          #endif          
        }
      }
    }
  }
}

void setup() {
  espNowPrefixLen = strlen(espNowPrefix);
  Serial.begin(74880);   //  GPIO1 (TX) and GPIO3 (RX)
  Serial.setDebugOutput(true);
  
  dmxB.begin();

  dmx_data[0] = 0;   // R
  dmx_data[1] = 0;   // G
  dmx_data[2] = 0;   // B
  dmx_data[3] = 0;   // W
  dmx_data[4] = 255; // A
  dmx_data[5] = 0;   // UV

  dmx_data[6] = 0;
  dmx_data[7] = 0;
  dmx_data[8] = 0;
  dmx_data[9] = 0;
  dmx_data[10] = 255;
  dmx_data[11] = 0;
  
  // Map 32 values to dmx channels 1 .. on dmxB
  dmxB.setChans(dmx_data, 256, 1);

  //WiFi.disconnect();
	//ESP.eraseConfig();
	//delay(3000);
  WiFi.mode(WIFI_MODE_STA);
  //wifi_promiscuous_enable(true); // this kills broadcast receive capability!
  //wifi_set_channel(ESPNOW_CHANNEL);
  //wifi_promiscuous_enable(false); 

  #if defined ESP32
    WiFi.disconnect (false, true);
  #elif defined ESP8266
    WiFi.disconnect (false);
  #endif //ESP32

  quickEspNow.begin(ESPNOW_CHANNEL); // 
  #ifdef ESPNOW_TESTMODE
    Serial.printf ("Connected to %s in channel %d\n", WiFi.SSID ().c_str (), WiFi.channel ());
    Serial.printf ("IP address: %s\n", WiFi.localIP ().toString ().c_str ());
    Serial.printf ("MAC address: %s\n", WiFi.macAddress ().c_str ());
    Serial.println(system_get_sdk_version());
  #endif
  quickEspNow.onDataRcvd(dataReceived);
  #ifdef ESP32
    quickEspNow.setWiFiBandwidth (WIFI_IF_STA, WIFI_BW_HT20); // Only needed for ESP32 in case you need coexistence with ESP8266 in the same network
  #endif //ESP32
}

char msg[256];

#define SERRECVLINESIZE 1024
char serRecvLine[SERRECVLINESIZE];
int  serRecvIdx = 0;

void loop() {
  static elapsedMillis bcTx = 0;
  static int cnt = 0;

  // Testbroadcast every 5s
  if(bcTx > 5000) {
    bcTx = 0;
    sprintf(msg,"DongDong:%d", cnt++);

    #ifdef ESPNOW_AUTOSEND
    if (!quickEspNow.send (DEST_ADDR, (uint8_t*)msg, strlen(msg))) {
        Serial.println (">>>>>>>>>> Message sent");
    } else {
        Serial.println (">>>>>>>>>> Message not sent");
    }
    #endif
  }

  if(doAnswer) {
    doAnswer = false;
    sprintf(msg,"DMXA V1.0, Uptime: %lu", millis());

    if (!quickEspNow.send (DEST_ADDR, (uint8_t*)msg, strlen(msg))) {
        Serial.println (">>>>>>>>>> Answer Message sent");
    } else {
        Serial.println (">>>>>>>>>> Answer Message not sent");
    }
  }

  if(new_dmx_len > 0) {
    //dmxB.setChans(&dmx_data[new_dmx_start_addr], new_dmx_len, new_dmx_start_addr);
    Serial.printf("New data addr:%d, len:%d\r\n", new_dmx_start_addr, new_dmx_len);
    dmxB.setChans(dmx_data, 256, 1);
    noInterrupts();
    new_dmx_len = 0;
    interrupts();
  }

  // put your main code here, to run repeatedly:
  if(Serial.available()) {
    char b = Serial.read();

    if(b >= ' ') {
      if(serRecvIdx < (SERRECVLINESIZE - 2)) {
        serRecvLine[serRecvIdx++] = b;
        Serial.write(b);
      }
    } else if(b == 8) {
      if(serRecvIdx > 0) {
        serRecvIdx--;
        Serial.write(b);
      }
    } else if((b == 0x0d) || (b == 0x0a)) {
      Serial.println("");
      serRecvLine[serRecvIdx] = 0;
      if(serRecvIdx > 0) {
        if (!quickEspNow.send (DEST_ADDR, (uint8_t*)serRecvLine, strlen(serRecvLine))) {
            Serial.println (">>>>>>>>>> Message sent");
        } else {
            Serial.println (">>>>>>>>>> Message not sent");
        }
      }
      serRecvIdx = 0;
    }
  }  
  delay(1);
}

