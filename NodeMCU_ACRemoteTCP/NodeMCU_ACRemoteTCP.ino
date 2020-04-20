#include <OneWire.h>
#include <IRremoteESP8266.h>
#include <IRremoteInt.h>

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

// TCP server settings
WiFiServer server(80);
bool irCommandPresent = false;
char packetBuffer[39];

// MAXIMUM SIZE OF THE PAYLOAD
#define MAX_PAYLOAD 20

#define PAYLOAD_SIZE 19

#define INTRO_SIZE 8

// payload description -- IR commands timings and info
const int freq = 38; // 38 KHz
const int irOne = 1300;
const int irZero = 400;
const int irSpace = 400;
const int irSeparator = 9800;
const int irLock1 = 3500;
const int irLock2 = 1700;
const byte irIntro[INTRO_SIZE] = {
  0x40, 0x04, 0x07, 0x20,
  0x00, 0x00, 0x00, 0x60
}; // hexadecimal code for opening sequence

byte payload[PAYLOAD_SIZE];

boolean expectData = false;

byte command = 0;
boolean commandProcessed = false;
long timeout;
unsigned char message[20];

long usbTimeoutMs = 1000;


const int checkLEDPin = D4;
const int irLedPin = D2;
int inputIndex = 0;

// IR sender (from IRremote.h) - sets the pin and PWM properly
IRsend irsend(irLedPin);

void okResponse(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println();
  client.println("{\"message\":\"Ok\"}");
}

void badRequestResponse(WiFiClient client) {
  client.println("HTTP/1.1 400 Bad Request");
  client.println("Content-Type: application/json");
  client.println();
  client.println("{\"message\":\"Bad request\"}");
}

void flashLED() {
  flashLED(1);
}

void flashLED(int times) {
  for (int i = 0 ; i < times ; i++) {
    digitalWrite(checkLEDPin, HIGH);
    delay (30);
    digitalWrite(checkLEDPin, LOW);
    if (i < times - 1) {
      delay(30);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(10);
  WiFi.hostname("AC Remote");
  WiFi.begin("The-LAN-Before-Time", "freewifi");

  Serial.println("");
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.println(WiFi.localIP());

  pinMode(irLedPin, OUTPUT);
  pinMode(checkLEDPin, OUTPUT);
  flashLED(5);
}

void loop() {
  processPrevRequest();
  processNextRequest();
}

void emitIRCommand(byte* payload, int payloadSize) {
  Serial.println("Emitting");
  int l = 0;
  // set the PWM for the LED to 38KHz
  irsend.enableIROut(38);
  // send the Lock sequence
  irsend.mark(irLock1);
  irsend.space(irLock2);

  // send the intro part
  for (int s = 0 ; s < INTRO_SIZE ; s++ ) {
    for (int i = 7 ; i >= 0 ; i--) { // Most Significant Bit comes first
      irsend.mark(irSpace);
      irsend.space((irIntro[s] >> i) &  1 == 1 ? irOne : irZero);
    }
  }
  // send the separators and Lock sequence
  irsend.mark(irSpace);
  irsend.space(irSeparator);
  irsend.mark(irLock1);
  irsend.space(irLock2);

  // send the payload itself
  for (int s = 0 ; s < payloadSize ; s++ ) {
    for (int i = 7 ; i >= 0 ; i--) { // Most Significant Bit comes first
      irsend.mark(irSpace);
      irsend.space((payload[s] >> i) & 1 == 1 ? irOne : irZero);
    }
  }
  // end communication
  irsend.mark(irSpace);
  irsend.space(0);
}

void processBuffer() {
  byte cmdIndex = 0;
  char hexConvert[5] = "0x";

  // ASCII is received
  for (int i = 0 ; i < 19 ; i++) {
    hexConvert[2] = packetBuffer[cmdIndex++];
    hexConvert[3] = packetBuffer[cmdIndex++];
    hexConvert[4] = '\0';
    // convert the received chars to a numeric value in payload[i]
    payload[i] = (int)strtol(hexConvert, NULL, 0);
  }
}

void processPrevRequest() {
  Serial.println("Looking for prepared request");
  if (irCommandPresent) {
    Serial.println("Processing prepared request");
    processBuffer();
    emitIRCommand(payload, PAYLOAD_SIZE);
    flashLED(2);
    memset(packetBuffer, 0, sizeof packetBuffer);

    irCommandPresent = false;
  }
}

void processNextRequest() {
  Serial.println("Processing next request");
  // Check if a client has connected
  WiFiClient client = server.available();

  if (!client) {
    return;
  }

  // Wait until the client sends some data
  Serial.println("New client!");
  int timeoutTicker = 0;
  while (!client.available()) {
    timeoutTicker++;
    Serial.print(".");
    delay(2);

    if (timeoutTicker >= 100) {
      client.stop();
      return;
    }
  }
  Serial.println();

  // Read the first line of the request
  String req = client.readStringUntil('\r');
  Serial.println(req);

  client.flush();

  int foundCmd = req.indexOf("/cmd/");
  if (foundCmd != -1) {
    String cmd = req.substring(foundCmd + 5); // 5 = length of "/cmd/"

    cmd.toCharArray(packetBuffer, sizeof(packetBuffer));

    irCommandPresent = true;

    Serial.print("CMD: ");
    Serial.println(packetBuffer);

    okResponse(client);
    client.stop();
    return;
  }
  else {
    Serial.println("Invalid request");
    badRequestResponse(client);
    delay(5);
    client.stop();
    return;
  }
}