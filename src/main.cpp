#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#define DEBUG

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
// UDP port
#define UDP_PORT 8788
// label buffer size
#define LBLSIZE 64

const char* host = "espSer2net";
const char* ssid = "SchumyOpenWrt";
const char* password = "63483550";

struct ComSettings {
  char label[LBLSIZE];
  long baudrate;
  char parity;
  long wordlength;
  long stopbits;
};

ComSettings settings;
// default serial port configuration
ComSettings defaults = {"Undefined", 9600, 'N', 8, 1};

void eepromRead(int pos, uint8_t *ptr, int size) {
  for (int i = 0; i < size; i++) {
    ptr[i] = EEPROM.read(pos + i);
  }
}

void eepromSave(int pos, uint8_t *ptr, int size) {
  for (int i = 0; i < size; i++) {
    EEPROM.write(pos + i, ptr[i]);
  }
  EEPROM.commit();
}

enum SerialConfig serialSettings(struct ComSettings s){
  // this function returns serial configuration for Serial1 library
  long conf = 0;
  long wl = 3;
  if(s.wordlength >= 5 && s.wordlength <= 8){
    wl = s.wordlength-5;
  }
  long stp = 0;
  if(s.stopbits==1 || s.stopbits==2){
    if (s.stopbits==1){
      stp = 1;
    } else if (s.stopbits==2){
      stp = 3;  
    }
  }
  long p = 0;
  if(s.parity=='E'){
    p=2;
  }
  if(s.parity=='O'){
    p=3;
  }
  conf = (p) | (stp << 4) | (wl << 2);
  return (enum SerialConfig)conf;
}

#ifdef DEBUG
void printConfig(){
    DEBUG_PORT.print("IP-address: ");
    DEBUG_PORT.println(WiFi.localIP());
    if(!SERIAL_PORT){
      DEBUG_PORT.println("Serial port is closed");
    }else{
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

ESP8266WebServer server(80);
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";


bool alreadyConnected = false;
bool controlAlreadyConnected = false;

WiFiServer cmdServer(CMD_PORT);
WiFiClient cmdClient;
WiFiServer controlServer(CONTROL_PORT);
WiFiClient controlClient;
WiFiUDP Udp;

String cmd = "";

void reopenSerial(){
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

void parseCmd(String s, WiFiClient client) {
  #ifdef DEBUG
    DEBUG_PORT.println("Recieved control command: ");
    DEBUG_PORT.println(s);
  #endif
  if(s=="help"){
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

  if(s=="save"){
    eepromSave(0, (uint8_t *)&settings, sizeof(settings));
    client.println("Saved!");
  }

  if(s=="load"){
    eepromRead(0, (uint8_t *)&settings, sizeof(settings));
    client.println("Loaded!");
    changed = true;
  }

  if(s=="?"){
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
  if(s.startsWith("label")){
    if(l>6){
      s.substring(6).toCharArray(settings.label, LBLSIZE);
      changed = true;
    }
    client.println(settings.label);
  }
  if(s.startsWith("baudrate")){
    if(l>9){
      settings.baudrate=s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.baudrate);
  }
  if(s.startsWith("parity")){
    if(l>7){
      settings.parity=s.charAt(7);
      changed = true;
    }
    client.println(settings.parity);
  }
  if(s.startsWith("wordlength")){
    if(l>11){
      settings.wordlength=s.substring(11).toInt();
      changed = true;
    }
    client.println(settings.wordlength);
  }
  if(s.startsWith("stopbits")){
    if(l>9){
      settings.stopbits=s.substring(9).toInt();
      changed = true;
    }
    client.println(settings.stopbits);
  }
  if(changed){
    reopenSerial();
  }
}


void checkControl(){
  WiFiClient client = controlServer.available();

  if (client) {
    if (!controlAlreadyConnected) {
      // clean out the input buffer:
      client.flush();
      controlAlreadyConnected = true;
    }

    if (client.available() > 0){
      char c = client.read();
      if(c=='\n'){
        parseCmd(cmd, client);
        cmd = "";
      }else{
        if(c!='\r'){ // ignoring \r
          cmd += c;
        }
      }
    }
  }
}

// buffers for receiving and sending data
char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,
char  ReplyBuffer[] = "Serial gate v1.1";       // a string to send back

void checkUDP(){
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    #ifdef DEBUG
      DEBUG_PORT.print("Received packet of size ");
      DEBUG_PORT.println(packetSize);
      DEBUG_PORT.print("From ");
      IPAddress remote = Udp.remoteIP();
      for (int i = 0; i < 4; i++)
      {
        DEBUG_PORT.print(remote[i], DEC);
        if (i < 3)
        {
          DEBUG_PORT.print(".");
        }
      }
      DEBUG_PORT.print(", port ");
      DEBUG_PORT.println(Udp.remotePort());
    #endif

    // read the packet into packetBufffer
    Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);
    packetBuffer[packetSize] = '\0';
    #ifdef DEBUG
      DEBUG_PORT.println("Contents:");
      DEBUG_PORT.println(packetBuffer);
    #endif

    if(strcmp(packetBuffer,"?") == 0){
      // send a reply, to the IP address and port that sent us the packet we received
      Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
      Udp.write(ReplyBuffer);
      Udp.endPacket();
    }
  }
}

void setup() {
  EEPROM.begin(EEPROMMAXSIZE);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  eepromRead(0, (uint8_t *)&settings, sizeof(settings));
  // very stupid check
  if(settings.baudrate<300){
    eepromSave(0, (uint8_t *)&defaults, sizeof(defaults));
    eepromRead(0, (uint8_t *)&settings, sizeof(settings));
  }
  SERIAL_PORT.begin(settings.baudrate, serialSettings(settings));
#ifdef DEBUG
  DEBUG_PORT.begin(9600);
  printConfig();
#endif
  cmdServer.begin();
  controlServer.begin();
  Udp.begin(UDP_PORT);

  if (WiFi.waitForConnectResult() == WL_CONNECTED) {
    MDNS.begin(host);
    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", serverIndex);
    });
    server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
      ESP.restart();
    }, []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        WiFiUDP::stopAll();
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if (!Update.begin(maxSketchSpace)) { //start with max available size
          //Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
          //Update.printError(Serial);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) { //true to set the size to the current progress
        } else {
          //Update.printError(Serial);
        }
      }
      yield();
    });
    server.begin();
    MDNS.addService("http", "tcp", 80);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  for (int i=0; i<3; i++){
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
  }
}

void loop() {
  // wait for a new client:
  if (cmdServer.hasClient()) {
    cmdClient = cmdServer.available();
    DEBUG_PORT.println("got client");
  }

  // to disable telnet control just comment the line below
  //checkControl();
  checkUDP();

  while (cmdClient.available() && SERIAL_PORT.availableForWrite() > 0) {
    char c = cmdClient.read();
    SERIAL_PORT.write(c);
#ifdef DEBUG
    DEBUG_PORT.write(c);
#endif
  }

  while (SERIAL_PORT.available() && cmdClient.availableForWrite() > 0) {
    char c = SERIAL_PORT.read();
    cmdServer.write(c);
#ifdef DEBUG
    DEBUG_PORT.write(c);
#endif
  }
#ifdef DEBUG
  if (DEBUG_PORT.available() > 0) {
    DEBUG_PORT.read();
    printConfig();
  }
#endif

  server.handleClient();
  MDNS.update();
}