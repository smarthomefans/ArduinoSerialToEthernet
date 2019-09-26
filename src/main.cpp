#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>

//#define DEBUG

#ifdef DEBUG
#define DEBUG_PORT Serial1
#endif
#define SERIAL_PORT Serial

// EEPROM size
#define EEPROMMAXSIZE 512
// Port to send commands
#define CMD_PORT 23
// Port for configuration
#define CONTROL_PORT 24
// label buffer size
#define LBLSIZE 64

const char *host = "espSer2net";

struct ComSettings
{
  char label[LBLSIZE];
  long baudrate;
  char parity;
  long wordlength;
  long stopbits;
};

ComSettings settings;
// default serial port configuration
ComSettings defaults = {"Undefined", 9600, 'N', 8, 1};

void eepromRead(int pos, uint8_t *ptr, int size)
{
  for (int i = 0; i < size; i++)
  {
    ptr[i] = EEPROM.read(pos + i);
  }
}

void eepromSave(int pos, uint8_t *ptr, int size)
{
  for (int i = 0; i < size; i++)
  {
    EEPROM.write(pos + i, ptr[i]);
  }
  EEPROM.commit();
}

enum SerialConfig serialSettings(struct ComSettings s)
{
  // this function returns serial configuration for Serial1 library
  long conf = 0;
  long wl = 3;
  if (s.wordlength >= 5 && s.wordlength <= 8)
  {
    wl = s.wordlength - 5;
  }
  long stp = 0;
  if (s.stopbits == 1 || s.stopbits == 2)
  {
    if (s.stopbits == 1)
    {
      stp = 1;
    }
    else if (s.stopbits == 2)
    {
      stp = 3;
    }
  }
  long p = 0;
  if (s.parity == 'E')
  {
    p = 2;
  }
  if (s.parity == 'O')
  {
    p = 3;
  }
  conf = (p) | (stp << 4) | (wl << 2);
  return (enum SerialConfig)conf;
}

#ifdef DEBUG
void printConfig()
{
  DEBUG_PORT.print("IP-address: ");
  DEBUG_PORT.println(WiFi.localIP());
  if (!SERIAL_PORT)
  {
    DEBUG_PORT.println("Serial port is closed");
  }
  else
  {
    DEBUG_PORT.println("Serial port is opened");
  }
  DEBUG_PORT.println("Serial configuration:");
  DEBUG_PORT.print("Label: ");
  DEBUG_PORT.println(settings.label);
  DEBUG_PORT.print("Baudrate: ");
  DEBUG_PORT.println(settings.baudrate);
  DEBUG_PORT.print("Parity: ");
  DEBUG_PORT.println(settings.parity);
  DEBUG_PORT.print("Wordlength: ");
  DEBUG_PORT.println(settings.wordlength);
  DEBUG_PORT.print("Stopbits: ");
  DEBUG_PORT.println(settings.stopbits);
}
#endif

bool alreadyConnected = false;
bool controlAlreadyConnected = false;

WiFiServer cmdServer(CMD_PORT);
WiFiClient cmdClient;
WiFiServer controlServer(CONTROL_PORT);
WiFiClient controlClient;

String cmd = "";

void reopenSerial()
{
  SERIAL_PORT.begin(settings.baudrate, serialSettings(settings));
#ifdef DEBUG
  printConfig();
#endif
  controlServer.println("Settings changed:");
  controlServer.print(settings.label);
  controlServer.print(",");
  controlServer.print(settings.baudrate);
  controlServer.print(",");
  controlServer.print(settings.parity);
  controlServer.print(",");
  controlServer.print(settings.wordlength);
  controlServer.print(",");
  controlServer.println(settings.stopbits);
}

void parseCmd(String s, WiFiClient client)
{
#ifdef DEBUG
  DEBUG_PORT.println("Recieved control command: ");
  DEBUG_PORT.println(s);
#endif
  if (s == "help")
  {
    client.println("Available commands:");
    client.println("? - get <label>,<baudrate>,<parity>,<wordlength>,<stopbits>");
    client.println("label [string] - get or set custom label for this box (up to 32 characters)");
    client.print("baudrate [value] - get or set baudrate ");
    client.println("(300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 115200)");
    client.println("parity [value] - get or set parity (N, E, O)");
    client.println("wordlength [value] - get or set wordlength (5, 6, 7, 8)");
    client.println("stopbits [value] - get or set stopbits (1, 2)");
    client.println("save - saves current settings to EEPROM memory");
    client.println("load - loads settings from EEPROM memory");
  }

  bool changed = false;

  if (s == "save")
  {
    eepromSave(0, (uint8_t *)&settings, sizeof(settings));
    client.println("Saved!");
  }

  if (s == "load")
  {
    eepromRead(0, (uint8_t *)&settings, sizeof(settings));
    client.println("Loaded!");
    changed = true;
  }

  if (s == "?")
  {
    client.print(settings.label);
    client.print(",");
    client.print(settings.baudrate);
    client.print(",");
    client.print(settings.parity);
    client.print(",");
    client.print(settings.wordlength);
    client.print(",");
    client.println(settings.stopbits);
  }

  int l = s.length();
  if (s.startsWith("label"))
  {
    if (l > 6)
    {
      s.substring(6).toCharArray(settings.label, LBLSIZE);
      changed = true;
    }
    client.println(settings.label);
  }
  if (s.startsWith("baudrate"))
  {
    if (l > 9)
    {
      settings.baudrate = s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.baudrate);
  }
  if (s.startsWith("parity"))
  {
    if (l > 7)
    {
      settings.parity = s.charAt(7);
      changed = true;
    }
    client.println(settings.parity);
  }
  if (s.startsWith("wordlength"))
  {
    if (l > 11)
    {
      settings.wordlength = s.substring(11).toInt();
      changed = true;
    }
    client.println(settings.wordlength);
  }
  if (s.startsWith("stopbits"))
  {
    if (l > 9)
    {
      settings.stopbits = s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.stopbits);
  }
  if (changed)
  {
    reopenSerial();
  }
}

void checkControl()
{
  WiFiClient client = controlServer.available();

  if (client)
  {
    if (!controlAlreadyConnected)
    {
      // clean out the input buffer:
      client.flush();
      controlAlreadyConnected = true;
    }

    if (client.available() > 0)
    {
      char c = client.read();
      if (c == '\n')
      {
        parseCmd(cmd, client);
        cmd = "";
      }
      else
      {
        if (c != '\r')
        { // ignoring \r
          cmd += c;
        }
      }
    }
  }
}

void setup()
{
  WiFiManager wifiManager;

  EEPROM.begin(EEPROMMAXSIZE);
  eepromRead(0, (uint8_t *)&settings, sizeof(settings));
  // very stupid check
  if (settings.baudrate < 300)
  {
    eepromSave(0, (uint8_t *)&defaults, sizeof(defaults));
    eepromRead(0, (uint8_t *)&settings, sizeof(settings));
  }
  SERIAL_PORT.begin(settings.baudrate, serialSettings(settings));
#ifdef DEBUG
  DEBUG_PORT.begin(9600);
  printConfig();
#endif

#ifndef DEBUG
  wifiManager.setDebugOutput(false);
#endif
  wifiManager.autoConnect(host);

  cmdServer.begin();
  controlServer.begin();

  if (WiFi.waitForConnectResult() == WL_CONNECTED)
  {
    MDNS.begin(host);
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setHostname(host);
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
      {
        type = "sketch";
      }
      else
      { // U_FS
        type = "filesystem";
      }

#ifdef DEBUG
      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
      DEBUG_PORT.println("Start updating " + type);
#endif
    });
#ifdef DEBUG
    ArduinoOTA.onEnd([]() {
      DEBUG_PORT.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      DEBUG_PORT.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      DEBUG_PORT.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR)
      {
        DEBUG_PORT.println("Auth Failed");
      }
      else if (error == OTA_BEGIN_ERROR)
      {
        DEBUG_PORT.println("Begin Failed");
      }
      else if (error == OTA_CONNECT_ERROR)
      {
        DEBUG_PORT.println("Connect Failed");
      }
      else if (error == OTA_RECEIVE_ERROR)
      {
        DEBUG_PORT.println("Receive Failed");
      }
      else if (error == OTA_END_ERROR)
      {
        DEBUG_PORT.println("End Failed");
      }
    });
#endif
    ArduinoOTA.begin();
  }

  pinMode(2, OUTPUT);
  for (int i = 0; i < 3; i++)
  {
    digitalWrite(2, LOW);
    delay(200);
    digitalWrite(2, HIGH);
    delay(200);
  }
}

void loop()
{
  // wait for a new client:
  if (cmdServer.hasClient())
  {
    cmdClient = cmdServer.available();
#ifdef DEBUG
    DEBUG_PORT.println("got client");
#endif
  }

  // to disable telnet control just comment the line below
  //checkControl();

  while (cmdClient.available() && SERIAL_PORT.availableForWrite() > 0)
  {
    char c = cmdClient.read();
    SERIAL_PORT.write(c);
#ifdef DEBUG
    DEBUG_PORT.write(c);
#endif
  }

  while (SERIAL_PORT.available() && cmdClient.availableForWrite() > 0)
  {
    char c = SERIAL_PORT.read();
    cmdServer.write(c);
#ifdef DEBUG
    DEBUG_PORT.write(c);
#endif
  }
#ifdef DEBUG
  if (DEBUG_PORT.available() > 0)
  {
    DEBUG_PORT.read();
    printConfig();
  }
#endif

  ArduinoOTA.handle();
  MDNS.update();
}