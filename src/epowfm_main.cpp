/* 
 
*/
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include "TCP2UART.h"
#include "SPI.h"

#include <ArduinoOTA.h>
static int otaPartProcentCount = 0;

#include <ESP8266WebServer.h>

#define DEBUG_UART Serial1

TCP2UART tcp2uart;

extern const char index_html[];
extern const char main_js[];
// QUICKFIX...See https://github.com/esp8266/Arduino/issues/263
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

#define LED_PIN 5                       // 0 = GPIO0, 2=GPIO2
#define LED_COUNT 50

#define WIFI_TIMEOUT 30000              // checks WiFi every ...ms. Reset after this time, if WiFi cannot reconnect.
#define HTTP_PORT 80

#define DOGM_LCD_CS 0
#define DOGM_LCD_RS 4

#define PULSE_INPUT 5

unsigned long auto_last_change = 0;
unsigned long last_wifi_check_time = 0;

ESP8266WebServer server(HTTP_PORT);

uint8_t changed = 0;
uint32_t count = 0;

void printESP_info(void);
void checkForUpdates(void);
void setup_BasicOTA(void);
void sendOneSpiByte(uint8_t data);
void DOGM_LCD_init(void);
void DOGM_LCD_setCursor(uint8_t row, uint8_t col);
void DOGM_LCD_writeStr(const char *p);
void waterMeter_ISR(void);
void DOGM_LCD_write12digitDec(uint32_t value);

void setup() {
    DOGM_LCD_init();
    DOGM_LCD_setCursor(0, 0);
    DOGM_LCD_writeStr("STARTING...");

    DEBUG_UART.begin(115200);
    DEBUG_UART.println(F("\r\n!!!!!Start of MAIN Setup!!!!!\r\n"));
    printESP_info();
    
    WiFiManager wifiManager;
    DEBUG_UART.println(F("trying to connect to saved wifi"));
    DOGM_LCD_setCursor(1, 0);
    DOGM_LCD_writeStr("WIFI CONNECTING.");
    wifiManager.autoConnect(); // using ESP.getChipId() internally
    //checkForUpdates();
    setup_BasicOTA();
    tcp2uart.begin();

    DOGM_LCD_setCursor(0, 0);
    DOGM_LCD_writeStr("RAW:000000000000");
    
    DOGM_LCD_setCursor(1, 0);
    DOGM_LCD_writeStr("LITERS:0000000.0");
    
    DOGM_LCD_setCursor(2, 0);
    DOGM_LCD_writeStr("LITER/MIN:0000.0");

    pinMode(PULSE_INPUT, INPUT);
    //attachInterrupt(digitalPinToInterrupt(PULSE_INPUT), waterMeter_ISR, RISING);

    DOGM_LCD_setCursor(0, 4);
    DOGM_LCD_write12digitDec(123456789);
    
    DEBUG_UART.println(F("\r\n!!!!!End of MAIN Setup!!!!!\r\n"));
}

void waterMeter_ISR(void) {
    changed = 1;
    count++;
}

void DOGM_LCD_writeOneDigit(uint8_t val) {
    sendOneSpiByte(0x30 + val);
    delayMicroseconds(30);
}

void DOGM_LCD_write12digitDec(uint32_t value) {
    uint32_t rest = value;
    DOGM_LCD_writeOneDigit(rest / 100000000000);
    rest = rest % 100000000000;
    DOGM_LCD_writeOneDigit(rest / 10000000000);
    rest = rest % 10000000000;
    DOGM_LCD_writeOneDigit(rest / 1000000000);
    rest = rest % 1000000000;
    DOGM_LCD_writeOneDigit(rest / 100000000);
    rest = rest % 100000000;
    DOGM_LCD_writeOneDigit(rest / 10000000);
    rest = rest % 10000000;
    DOGM_LCD_writeOneDigit(rest / 1000000);
    rest = rest % 1000000;
    DOGM_LCD_writeOneDigit(rest / 100000);
    rest = rest % 100000;
    DOGM_LCD_writeOneDigit(rest / 10000);
    rest = rest % 10000;
    DOGM_LCD_writeOneDigit(rest / 1000);
    rest = rest % 1000;
    DOGM_LCD_writeOneDigit(rest / 100);
    rest = rest % 100;
    DOGM_LCD_writeOneDigit(rest / 10);
    rest = rest % 10;
    DOGM_LCD_writeOneDigit(rest);
}

void loop() {
    tcp2uart.BridgeMainTask();
    ArduinoOTA.handle();

    if (changed == 1) {
        changed = 0;
        DOGM_LCD_setCursor(0, 4);
        DOGM_LCD_write12digitDec(count);
        //DOGM_LCD_writeStr("000000000000");
    }

/*
    digitalWrite(DOGM_LCD_CS, LOW); // enable Slave Select
    digitalWrite(DOGM_LCD_RS, LOW);
    SPI.transfer(0xAA);

    digitalWrite(DOGM_LCD_RS, HIGH);
    SPI.transfer(0x55);
    digitalWrite(DOGM_LCD_CS, HIGH); // disable Slave Select
    */
}

void DOGM_LCD_writeStr(const char *p) {
    while (*p != 0) {
        sendOneSpiByte(*p);
        delayMicroseconds(30);
        p++;
    }
}

void DOGM_LCD_setCursor(uint8_t row, uint8_t col) {
    if (row > 2) row = 2;
    if (col > 0x1F) col = 0x1F;

    digitalWrite(4, LOW); // Instruction
    delayMicroseconds(1);
    sendOneSpiByte(0x80 + row*0x10 + col); // second row
    delayMicroseconds(30);
    digitalWrite(4, HIGH); // data (default)
    delayMicroseconds(1);
}

void DOGM_LCD_init(void) {
// DOGM LCD SPI CS
    digitalWrite(DOGM_LCD_CS, HIGH);
    pinMode(DOGM_LCD_CS, OUTPUT);
    // DOGM LCD RS
    pinMode(DOGM_LCD_RS, OUTPUT);
    digitalWrite(DOGM_LCD_RS, LOW);
    pinMode(DOGM_LCD_RS, OUTPUT);

    SPI.begin();
    SPI.setClockDivider(SPI_CLOCK_DIV8);
    SPI.setDataMode(SPI_MODE3);
    SPI.setHwCs(false);
    SPI.setBitOrder(MSBFIRST);

    delay(40);

    sendOneSpiByte(0x39); // 8 bit data length, 2 lines, instruction table 1
    delayMicroseconds(30);
    
    sendOneSpiByte(0x39);
    delayMicroseconds(30);

    sendOneSpiByte(0x15); // BS: 1/5, 3 line LCD
    delayMicroseconds(30);

    sendOneSpiByte(0x56); // booster on, contrast C5, set C4
    delayMicroseconds(30);

    sendOneSpiByte(0x6E); // set voltage follower and gain
    delay(300); // wait for power to stabilize

    sendOneSpiByte(0x70); // set contrast C3, C2, C1
    delayMicroseconds(30);

    sendOneSpiByte(0x38); // switch back to instruction table 0
    delayMicroseconds(30);

    //sendOneSpiByte(0x0F); // display on, cursor on, cursor blink
    //delayMicroseconds(30);
    sendOneSpiByte(0x0C); // display on, cursor off, cursor not blink
    delayMicroseconds(30);

    sendOneSpiByte(0x01); // clear display, cursor at home
    delayMicroseconds(30);

    sendOneSpiByte(0x06); // cursor auto-increment
    delayMicroseconds(30);
}

void sendOneSpiByte(uint8_t data) {
    digitalWrite(DOGM_LCD_CS, LOW); // enable chip Select
    SPI.transfer(data);
    digitalWrite(DOGM_LCD_CS, HIGH); // disable chip Select
}

void checkForUpdates(void)
{
    //EEPROM.put(SPI_FLASH_SEC_SIZE, "hello");

    DEBUG_UART.println(F("checking for updates"));
    String updateUrl = "http://espOtaServer/esp_ota/" + String(ESP.getChipId(), HEX) + ".bin";
    t_httpUpdate_return ret = ESPhttpUpdate.update(updateUrl.c_str());
    DEBUG_UART.print(F("HTTP_UPDATE_"));
    switch (ret) {
      case HTTP_UPDATE_FAILED:
        DEBUG_UART.println(F("FAIL Error "));
        DEBUG_UART.printf("(%d): %s", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        DEBUG_UART.println(F("NO_UPDATES"));
        break;

      case HTTP_UPDATE_OK:
        DEBUG_UART.println(F("OK"));
        break;
    }
}

// called from setup() function
void printESP_info(void) { 
    uint32_t realSize = ESP.getFlashChipRealSize();
    uint32_t ideSize = ESP.getFlashChipSize();
    FlashMode_t ideMode = ESP.getFlashChipMode();
  
    DEBUG_UART.print(F("Flash real id:   ")); DEBUG_UART.printf("%08X\r\n", ESP.getFlashChipId());
    DEBUG_UART.print(F("Flash real size: ")); DEBUG_UART.printf("%u 0\r\n\r\n", realSize);
  
    DEBUG_UART.print(F("Flash ide  size: ")); DEBUG_UART.printf("%u\r\n", ideSize);
    DEBUG_UART.print(F("Flash ide speed: ")); DEBUG_UART.printf("%u\r\n", ESP.getFlashChipSpeed());
    DEBUG_UART.print(F("Flash ide mode:  ")); DEBUG_UART.printf("%s\r\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
  
    if(ideSize != realSize)
    {
        DEBUG_UART.println(F("Flash Chip configuration wrong!\r\n"));
    }
    else
    {
        DEBUG_UART.println(F("Flash Chip configuration ok.\r\n"));
    }
    DEBUG_UART.printf(" ESP8266 Chip id = %08X\n", ESP.getChipId());
}

void setup_BasicOTA()
{
    ArduinoOTA.onStart([]() {
        otaPartProcentCount = 0;
        String type;

        if (ArduinoOTA.getCommand() == U_FLASH) {

          type = "sketch";

        } else { // U_SPIFFS

          type = "filesystem";

        }



        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

        DEBUG_UART.println(F("OTA Start\rOTA Progress: "));

      });

      ArduinoOTA.onEnd([]() {

        DEBUG_UART.println("\n100%\nOTA End");

      });

      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

        //DEBUG_UART.printf("Progress: %u%%\r", (progress / (total / 100)));
        //Serial1.printf("%u%%\r", (progress / (total / 100)));
        DEBUG_UART.print("-");
        if (otaPartProcentCount < 10)
          otaPartProcentCount++;
        else
        {
          otaPartProcentCount = 0;
          DEBUG_UART.printf(" %u%%\r", (progress / (total / 100)));
        }
      });

      ArduinoOTA.onError([](ota_error_t error) {
        DEBUG_UART.printf("OTA Error");
        DEBUG_UART.printf("[%u]: ", error);
        if (error == OTA_AUTH_ERROR) DEBUG_UART.println(F("Auth Failed"));
        else if (error == OTA_BEGIN_ERROR) DEBUG_UART.println(F("Begin Failed"));
        else if (error == OTA_CONNECT_ERROR) DEBUG_UART.println(F("Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR) DEBUG_UART.println(F("Receive Failed"));
        else if (error == OTA_END_ERROR) DEBUG_UART.println(F("End Failed"));
      });

      ArduinoOTA.begin();

      DEBUG_UART.println("Ready");

      DEBUG_UART.print("IP address: ");

      DEBUG_UART.println(WiFi.localIP());
}
