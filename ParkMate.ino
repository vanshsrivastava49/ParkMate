#include <WiFi.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include "ThingSpeak.h"

const char* ssid = "xyz"; //Replace with your wifi name
const char* password = "password"; //replace with your wifi password

const unsigned long channelID = YourThingspeakChannelID;
const char* writeAPIKey = "ThingsSpeakWriteAPIKey";
WiFiClient client;

// RFID
#define RST_PIN 2    // GPIO 2
#define SS_PIN 21    // GPIO 21
MFRC522 rfid(SS_PIN, RST_PIN);

// Servo
Servo gateServo;
#define SERVO_PIN 14  // GPIO 14

// Ultrasonic Sensor
#define TRIG_PIN 13   // GPIO 13
#define ECHO_PIN 12   // GPIO 12

// IR Sensors
#define IR1 4         // GPIO 4 (Slot 1)
#define IR2 5         // GPIO 5 (Slot 2)
#define IR3 15        // GPIO 18 (Slot 3)

// Allowed RFID UIDs
String allowedUIDs[] = {"E3DD7228", "F6B97D43", "96046F43", "13FF2E14"};

// Detection threshold (in cm)
#define ULTRASONIC_THRESHOLD 10

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== ESP32 Smart Parking System ===");

  // Initialize IR sensors
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);
  pinMode(IR3, INPUT);
  
  // Initialize ultrasonic sensor
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Initialize RFID
  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  if (version == 0x00 || version == 0xFF) {
    Serial.println("RFID reader not detected. Check connections!");
  } else {
    Serial.println("RFID reader detected.");
  }

  // Initialize servo (reversed logic as in your original code)
  gateServo.attach(SERVO_PIN, 500, 2400);
  gateServo.write(120); // Initial closed position (reversed)
  Serial.println("Gate servo initialized to closed position (120 degrees)");

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    ThingSpeak.begin(client);
  } else {
    Serial.println("\nWiFi connection failed.");
  }
}

String getUID(MFRC522::Uid uid) {
  String uidStr = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

long readUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // Timeout at 30ms
  long distance = duration * 0.034 / 2;
  return distance;
}

void openGate() {
  // Move the servo gradually (reversed direction as in your code)
  for (int pos = 120; pos >= 0; pos -= 10) {
    gateServo.write(pos);
    delay(15);
  }
  gateServo.write(0);  // Final open position (0 degrees)
  Serial.println("Gate opened to 0 degrees.");
}

void closeGate() {
  delay(2000);
  // Move the servo gradually (reversed direction)
  for (int pos = 0; pos <= 120; pos += 10) {
    gateServo.write(pos);
    delay(15);
  }
  gateServo.write(120);   // Final closed position (120 degrees)
  Serial.println("Gate closed to 120 degrees.");
}

void loop() {
  String scannedUID = "";
  static bool gateIsOpen = false;

  // Read distance from ultrasonic sensor
  long distance = readUltrasonicDistance();
  Serial.print("Ultrasonic Distance: ");
  Serial.println(distance);
  
  bool vehicleDetected = (distance > 0 && distance <= ULTRASONIC_THRESHOLD);

  // ========== 1. RFID-Based Gate Control (Entry) ==========
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    scannedUID = getUID(rfid.uid);
    Serial.print("RFID UID Scanned: ");
    Serial.println(scannedUID);

    bool accessGranted = false;
    for (String uid : allowedUIDs) {
      if (scannedUID.equals(uid)) {
        accessGranted = true;
        break;
      }
    }

    if (accessGranted) {
      Serial.println("Access granted. Opening gate.");
      openGate();
      gateIsOpen = true;
    } else {
      Serial.println("Access denied.");
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  // ========== 2. Ultrasonic-Based Gate Control ==========
  if (vehicleDetected && !gateIsOpen) {
    Serial.println("Vehicle detected by sensor. Opening gate.");
    openGate();
    gateIsOpen = true;
  }
  
  if (gateIsOpen && !vehicleDetected) {
    Serial.println("Vehicle no longer detected. Closing gate.");
    closeGate();
    gateIsOpen = false;
  }

  // ========== 3. ThingSpeak Update ==========
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 15000 && WiFi.status() == WL_CONNECTED) {
    lastUpdate = millis();

    // Get all slot statuses
    bool slot1 = !digitalRead(IR1); // Inverted logic (LOW = occupied)
    bool slot2 = !digitalRead(IR2);
    bool slot3 = !digitalRead(IR3);

    ThingSpeak.setField(1, vehicleDetected ? 1 : 0);  // Vehicle near gate
    ThingSpeak.setField(2, slot1);                   // Slot 1 status
    ThingSpeak.setField(3, slot2);                   // Slot 2 status
    ThingSpeak.setField(4, slot3);                   // Slot 3 status
    ThingSpeak.setField(5, scannedUID);              // Last scanned UID

    int response = ThingSpeak.writeFields(channelID, writeAPIKey);
    if (response == 200) {
      Serial.println("ThingSpeak update successful.");
    }
  }

  delay(100);
}
