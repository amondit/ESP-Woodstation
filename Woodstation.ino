#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "ht1632c.h"

/// framebuffer
static uint8_t** ht1632c_framebuffer = 0;
static uint8_t ht1632c_framebuffer_len = 96;

const char* ssid = "********";
const char* password = "********";

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
  ht1632c_init(HT1632_CMD_8PMOS);
  ht1632c_clear();
  ht1632c_pwm(15);
  ht1632c_sendframe();
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

void digitalClockDisplay()
{
  char buffer[7];
  sprintf(buffer, "%02d%02d%02d", hour(), minute(), second());
  Serial.println(buffer);
}

void printDigits(int digits)
{
  // utility for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}




//------------------ Woodstation related data -------------------

static int timeDisplayBaseAddresses[6][2]=
{
    {0,2},  //H
    {14,2}, //H
    {28,2}, //m
    {0,3},  //m
    {14,3}, //s
    {28,3}  //s
};

static int dateDisplayBaseAddresses[8][2]=
{
    {1,0},  //D
    {15,0}, //D
    {29,0}, //M
    {1,1},  //M
    {15,1}, //Y
    {29,1}, //Y
    {1,2},  //Y
    {29,1}  //Y
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
    ht1632c_update_framebuffer(writeAddr, bitIndex, sevenSegmentCharBitSequence[i]);
    Serial.print(sevenSegmentCharBitSequence[i]);
    writeAddr = writeAddr + 2;
  }
  Serial.println();
}

void updateTimeDisplay(char* newTime){
  int characterMap[7] = {0, 0, 0, 0, 0, 0, 0};
  int iValue = 0;
  char cValue = '0';
  for(int timeDigitIndex = 0; timeDigitIndex < 6; timeDigitIndex++){
    cValue = newTime[timeDigitIndex];
    iValue = atoi(&cValue);
    Serial.println(iValue);
    int baseAddr = timeDisplayBaseAddresses[timeDigitIndex][0];
    int bitIndex = timeDisplayBaseAddresses[timeDigitIndex][1];
    writeDisplayCharacterAtIndex(sevenSegmentDigits[iValue], baseAddr, bitIndex);
  }
  Serial.println();
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


//------------------- HT1632 Related data ------------

void ht1632c_chipselect(const int value)
{
  digitalWrite(SS, toBit(value));
}

void ht1632c_sendcmd(const uint8_t cmd) {
  uint16_t data = HT1632_ID_CMD;
  data <<= HT1632_CMD_LEN;
  data |= cmd;
  data <<= 5;
  //reverse_endian(&data, sizeof(data));
  
  ht1632c_chipselect(1);
  SPI.write16(data);
  ht1632c_chipselect(0);

}

void ht1632c_update_framebuffer(const int addr, const uint8_t bitIndex, const uint8_t bitValue)
{
  if(addr <0 || addr > (ht1632c_framebuffer_len-1) || bitIndex > 3)
    return;
  ht1632c_framebuffer[addr][bitIndex] = toBit(bitValue);
}

uint8_t ht1632c_get_framebuffer(const int addr, const uint8_t bitIndex)
{
  if(addr <0 || addr > (ht1632c_framebuffer_len-1) || bitIndex > 3)
      return 0;
  return ht1632c_framebuffer[addr][bitIndex];
}

//
// public functions
//

int ht1632c_init(const uint8_t commonsMode)
{
  // init SPI
  pinMode(SS, OUTPUT);
  SPI.begin(); 
  SPI.setBitOrder(MSBFIRST);
  SPI.setClockDivider(SPI_CLOCK_DIV64);
  SPI.setDataMode(SPI_MODE0);


  switch (commonsMode) {
    case HT1632_CMD_8NMOS:
    case HT1632_CMD_8PMOS:
      ht1632c_framebuffer_len = 64;
      break;
    case HT1632_CMD_16NMOS:
    case HT1632_CMD_16PMOS:
      ht1632c_framebuffer_len = 96;
      break;
    default:
      break;
  }


  ht1632c_framebuffer = (uint8_t**) malloc(ht1632c_framebuffer_len*sizeof(uint8_t*));
  if (!ht1632c_framebuffer) {
    Serial.println( "Framebuffer allocation failed.");
    return 3;
  }

  for (int i = 0; i < ht1632c_framebuffer_len; ++i) {
    ht1632c_framebuffer[i] = (uint8_t*) malloc(4*sizeof(uint8_t));
    if (!ht1632c_framebuffer[i]) {
        Serial.println( "Framebuffer allocation failed.");
        return 3;
      }
    ht1632c_framebuffer[i][0] = ht1632c_framebuffer[i][1] = ht1632c_framebuffer[i][1] = ht1632c_framebuffer[i][3] = 0;
  }

  // init display
  ht1632c_sendcmd(HT1632_CMD_SYSDIS);
  ht1632c_sendcmd(commonsMode);
  ht1632c_sendcmd(HT1632_CMD_MSTMD);
  ht1632c_sendcmd(HT1632_CMD_RCCLK);
  ht1632c_sendcmd(HT1632_CMD_SYSON);
  ht1632c_sendcmd(HT1632_CMD_LEDON);
  ht1632c_sendcmd(HT1632_CMD_BLOFF);
  ht1632c_sendcmd(HT1632_CMD_PWM);

  ht1632c_clear();
  ht1632c_sendframe();

  return 0;
}

int ht1632c_close()
{
  SPI.end();
  if (ht1632c_framebuffer) {
    for (int i = 0; i < ht1632c_framebuffer_len; ++i) {
      if (ht1632c_framebuffer[i]) {
        free(ht1632c_framebuffer[i]);
        ht1632c_framebuffer[i] = 0;
      }
    }
    free(ht1632c_framebuffer);
    ht1632c_framebuffer = 0;
  }
}

void ht1632c_pwm(const uint8_t value)
{
  ht1632c_sendcmd(HT1632_CMD_PWM | (value & 0x0f));
}

void ht1632c_sendframe()
{
  uint16_t data = HT1632_ID_WR;
  data <<= HT1632_ADDR_LEN;
  data <<= 6;

  //We append the first 1.5 addresses values, to fill the 16bit buffer
  uint8_t bitValues = 0;
  for (int j = 0; j < 4; ++j) {
    bitValues <<= 1;
    bitValues |= toBit(ht1632c_framebuffer[0][j]);
  }
  for (int j = 0; j < 2; ++j) {
    bitValues <<= 1;
    bitValues |= toBit(ht1632c_framebuffer[1][j]);
  }
  data |= bitValues;
  //reverse_endian(&data, sizeof(data));

  ht1632c_chipselect(1);
  SPI.write16(data);


  //Loop to write the frame buffer
  int i = 1;
  while (i < ht1632c_framebuffer_len) {
    bitValues = 0;
    //we copy the last 2 bits of the previous address,
    //as they are truncated due to message size of 16b
    //
    //(format of bit values : AABB BBCC)
    for (int j = 2; j < 4; ++j) {
      bitValues <<= 1;
      bitValues |= toBit(ht1632c_framebuffer[i][j]);
    }
    i++;
    if (i == ht1632c_framebuffer_len) {
      bitValues <<=6;
    }else {
      for (int j = 0; j < 4; ++j) {
        bitValues <<= 1;
        bitValues |= toBit(ht1632c_framebuffer[i][j]);
      }
      i++;
      if (i == ht1632c_framebuffer_len) {
        bitValues <<= 2;
      }else {
        for (int j = 0; j < 2; ++j) {
          bitValues <<= 1;
          bitValues |= toBit(ht1632c_framebuffer[i][j]);
        }
      }

    }
    SPI.write(bitValues);
  }

  ht1632c_chipselect(0);
}

void ht1632c_clear()
{
  // clear buffer
  if (ht1632c_framebuffer) {
    for (int i = 0; i < ht1632c_framebuffer_len; ++i) {
      if (ht1632c_framebuffer[i]) {
        ht1632c_framebuffer[i][0] = ht1632c_framebuffer[i][1] = ht1632c_framebuffer[i][1] = ht1632c_framebuffer[i][3] = 0;
      }
    }
  }

}
