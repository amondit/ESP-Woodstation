#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "HT1632.h"

#define ESP8266
#define HT1632_CS   D1
#define HT1632_CLK  D5
#define HT1632_MOSI D7


const char* ssid = "**************";
const char* password = "*************";

// NTP Servers:
static const char ntpServerName[] = "europe.pool.ntp.org";
//static const char ntpServerName[] = "time.nist.gov";
//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-c.timefreq.bldrdoc.gov";

int timeZone = 0;     // Central European Time
//const int timeZone = -5;  // Eastern Standard Time (USA)
//const int timeZone = -4;  // Eastern Daylight Time (USA)
//const int timeZone = -8;  // Pacific Standard Time (USA)
//const int timeZone = -7;  // Pacific Daylight Time (USA)


WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

time_t getNtpTime();
void digitalClockDisplay();
void printDigits(int digits);
void sendNTPpacket(IPAddress &address);

void setup()
{
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  Serial.println("Starting UDP");
  Udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(Udp.localPort());
  Serial.println("waiting for sync");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  Serial.println("Setting up SPI display");
  HT1632.begin(HT1632_CS, HT1632_CLK, HT1632_MOSI);
  HT1632.clear();
  HT1632.setPixel(0,7); // ':' on time display
  HT1632.setPixel(20,7); // ':' on time display
}

time_t prevDisplay = 0; // when the digital clock was displayed

void loop()
{
  if (timeStatus() != timeNotSet) {
    if (now() != prevDisplay) { //update the display only if time has changed
      int newTimeZoneOffset = adjustDstEurope();
      if(newTimeZoneOffset != timeZone)
      {
        Serial.print("Adjusting timezone offset: ");
        Serial.println(newTimeZoneOffset);
        timeZone = newTimeZoneOffset;
        setSyncProvider(getNtpTime);
      }
      prevDisplay = now();
      char buffer[7];
      sprintf(buffer, "%02d%02d%02d", hour(), minute(), second());
      updateTimeDisplay(buffer);
    }
  }
  delay(100);
}

//------------------ Woodstation related data -------------------

static int humidityDisplayBaseAddresses[2][2]=
{
    {0,0},
    {7,0}
};

static int temperatureDisplayBaseAddresses[3][2]=
{
    {0,1},
    {7,1},
    {14,1}
};

static int timeDisplayBaseAddresses[6][2]=
{
    {0,2},  //H
    {7,2}, //H
    {14,2}, //m
    {0,3},  //m
    {7,3}, //s
    {14,3}  //s
};

static int dateDisplayBaseAddresses[8][2]=
{
    {0,4},  //D
    {7,4}, //D
    {14,4}, //M
    {0,5},  //M
    {7,5}, //Y
    {14,5}, //Y
    {1,2},  //Y
    {7,6}  //Y
};

static int sevenSegmentDigits[10][7]=
{
    {1, 1, 1, 1, 1, 1, 0},
    {0, 1, 1, 0, 0, 0, 0},
    {1, 1, 0, 1, 1, 0, 1},
    {1, 1, 1, 1, 0, 0, 1},
    {0, 1, 1, 0, 0, 1, 1},
    {1, 0, 1, 1, 0, 1, 1},
    {1, 0, 1, 1, 1, 1, 1},
    {1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1},
    {1, 1, 1, 1, 0, 1, 1}
};
static int spaceCharacter[7] = {0, 0, 0, 0, 0, 0, 0};
static int minusCharacter[7] = {0, 0, 0, 0, 0, 0, 1};

void writeDisplayCharacterAtIndex(int* sevenSegmentCharBitSequence, int baseAddr, int bitIndex){
  if (!sevenSegmentCharBitSequence)
    return;
  int writeAddr = baseAddr;
  for (int i = 0; i < 7; ++i) {
    if (sevenSegmentCharBitSequence[i]) {
      HT1632.setPixel(writeAddr, bitIndex);
    } else {
      HT1632.clearPixel(writeAddr, bitIndex);
    }
    writeAddr++;
  }
}

void updateTimeDisplay(char* newTime){
  int characterMap[7] = {0, 0, 0, 0, 0, 0, 0};
  int iValue = 0;
  char cValue = '0';
  //HT1632.clear();
  for(int timeDigitIndex = 0; timeDigitIndex < 6; timeDigitIndex++){
    cValue = newTime[timeDigitIndex];
    iValue = atoi(&cValue);
    int baseAddr = timeDisplayBaseAddresses[timeDigitIndex][0];
    int bitIndex = timeDisplayBaseAddresses[timeDigitIndex][1];
    writeDisplayCharacterAtIndex(sevenSegmentDigits[iValue], baseAddr, bitIndex);
  }
  HT1632.render();
}





//-------------------- Time related data ------------------------


int adjustDstEurope()
{
 // last sunday of march
 int beginDSTDate=  (31 - (5* year() /4 + 4) % 7);
 //Serial.println(beginDSTDate);
 int beginDSTMonth=3;
 //last sunday of october
 int endDSTDate= (31 - (5 * year() /4 + 1) % 7);
 //Serial.println(endDSTDate);
 int endDSTMonth=10;
 // DST is valid as:
 if (((month() > beginDSTMonth) && (month() < endDSTMonth))
     || ((month() == beginDSTMonth) && (day() >= beginDSTDate)) 
     || ((month() == endDSTMonth) && (day() <= endDSTDate)))
 return 2;  // DST europe = utc +2 hour
 else return 1; // nonDST europe = utc +1 hour
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
