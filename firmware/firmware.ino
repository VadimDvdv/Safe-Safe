/*
 * Smart Lock ESP32 Firmware
 * 
 * Components:
 *   - ESP32 DevKit
 *   - GMEI2864 OLED (SSD1306 128x64, I2C)
 *   - RFID-RC522 (SPI)
 *   - 4x4 Matrix Keypad
 *   - DF9GMS Servo
 *
 * Auth factors:
 *   1. PIN (something you know)
 *   2. RFID (something you have)
 *   3. Dashboard unlock (second device you have + logged in)
 *
 * Libraries: Adafruit SSD1306, Adafruit GFX, MFRC522, Keypad,
 *            ESP32Servo, ArduinoJson, WebSockets (Markus Sattler)
 */

#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <SocketIOclient.h>

// ═══════════════════════════════════════════════════
//  CONFIGURATION — CHANGE THESE
// ═══════════════════════════════════════════════════

const char* WIFI_SSID      = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD  = "YOUR_WIFI_PASSWORD";

const char* SERVER_HOST    = "172.20.10.4";
const int   SERVER_PORT    = 3000;
const bool  USE_SSL        = false;

const char* DEVICE_TOKEN   = "PASTE_YOUR_DEVICE_TOKEN_HERE";

// ═══════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════

#define RFID_SS_PIN    5
#define RFID_RST_PIN   4

#define OLED_WIDTH     128
#define OLED_HEIGHT    64
#define OLED_ADDR      0x3C

#define SERVO_PIN      2

const byte KEYPAD_ROWS = 4;
const byte KEYPAD_COLS = 4;

char keys[KEYPAD_ROWS][KEYPAD_COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

byte rowPins[KEYPAD_ROWS] = {13, 12, 14, 27};
byte colPins[KEYPAD_COLS] = {26, 25, 33, 32};

#define SERVO_LOCKED    180
#define SERVO_UNLOCKED  0
#define AUTO_LOCK_TIMEOUT  5000

// ═══════════════════════════════════════════════════
//  GLOBAL OBJECTS
// ═══════════════════════════════════════════════════

MFRC522 rfid(RFID_SS_PIN, RFID_RST_PIN);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, KEYPAD_ROWS, KEYPAD_COLS);
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
Servo lockServo;
SocketIOclient socketIO;

// ═══════════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════════

enum LockState {
  STATE_IDLE,
  STATE_ENTERING_PIN,
  STATE_CHANGING_PIN,
  STATE_SCANNING_RFID,
  STATE_UNLOCKED
};

LockState currentState = STATE_IDLE;
bool isConnected = false;
bool isLocked = true;

// Credentials
String storedPinHash = "";
std::vector<String> authorizedRFID;

// Auth factors
bool pinVerified  = false;
bool rfidVerified = false;
bool dashVerified = false;

// PIN input
String pinBuffer = "";
String newPinBuffer = "";
String confirmPinBuffer = "";
bool changePinFirstEntry = true;

// Timing
unsigned long unlockTime = 0;

// ═══════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Smart Lock Starting ===");

  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init failed!");
  }
  displayMessage("Smart Lock", "Starting...");

  SPI.begin(18, 19, 23, 5);
  rfid.PCD_Init();
  delay(50);
  Serial.print("RFID: ");
  rfid.PCD_DumpVersionToSerial();

  lockServo.attach(SERVO_PIN);
  lockServo.write(SERVO_LOCKED);
  delay(500);
  lockServo.detach();
  isLocked = true;

  displayMessage("Connecting", "WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setAutoReconnect(true);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());
    displayMessage("WiFi OK", WiFi.localIP().toString().c_str());
    delay(500);
  } else {
    Serial.println("\nWiFi failed — offline mode");
    displayMessage("WiFi Failed", "Offline mode");
    delay(1000);
  }

  connectToServer();
  displayIdle();
}

// ═══════════════════════════════════════════════════
//  MAIN LOOP
// ═══════════════════════════════════════════════════

void loop() {
  socketIO.loop();
  handleKeypad();
  handleRFID();
  handleAutoLock();

  static unsigned long lastReconnect = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastReconnect > 5000) {
    WiFi.reconnect();
    lastReconnect = millis();
  }
}

// ═══════════════════════════════════════════════════
//  SOCKET.IO
// ═══════════════════════════════════════════════════

void connectToServer() {
  if (USE_SSL) {
    socketIO.beginSSL(SERVER_HOST, 443, "/socket.io/?EIO=3");
  } else {
    socketIO.begin(SERVER_HOST, SERVER_PORT, "/socket.io/?EIO=3");
  }
  socketIO.onEvent(socketIOEvent);
}

void socketIOEvent(socketIOmessageType_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case sIOtype_CONNECT:
      Serial.println("[IO] Connected");
      isConnected = true;
      sendEvent("esp_auth", "{\"device_token\":\"" + String(DEVICE_TOKEN) + "\"}");
      displayMessage("Connected", "Authenticating...");
      break;

    case sIOtype_DISCONNECT:
      Serial.println("[IO] Disconnected");
      isConnected = false;
      displayIdle();
      break;

    case sIOtype_EVENT:
      handleServerEvent((char*)payload);
      break;

    case sIOtype_ERROR:
      Serial.printf("[IO] Error: %s\n", payload);
      break;

    default:
      break;
  }
}

void handleServerEvent(const char* payload) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return;
  }

  const char* event = doc[0];
  JsonObject data = doc[1];
  Serial.printf("[IO] Event: %s\n", event);

  if (strcmp(event, "auth_ok") == 0) {
    const char* label = data["label"] | "Lock";
    Serial.printf("Authenticated as: %s\n", label);
    displayMessage("Authenticated", label);
    delay(1000);
    displayIdle();
  }
  else if (strcmp(event, "credentials_update") == 0) {
    authorizedRFID.clear();
    JsonArray rfidArr = data["rfid_tags"];
    for (const char* uid : rfidArr) {
      authorizedRFID.push_back(String(uid));
      Serial.printf("  RFID: %s\n", uid);
    }

    bool hasPin = data["has_pin"] | false;
    if (hasPin) storedPinHash = "set";
    Serial.printf("Loaded: %d RFID, PIN: %s\n",
      authorizedRFID.size(), hasPin ? "yes" : "no");
  }
  else if (strcmp(event, "pin_hash_update") == 0) {
    storedPinHash = data["pin_hash"].as<String>();
    Serial.println("PIN hash updated");
  }
  else if (strcmp(event, "command") == 0) {
    const char* action = data["action"];

    if (strcmp(action, "change_pin") == 0) {
      currentState = STATE_CHANGING_PIN;
      newPinBuffer = "";
      confirmPinBuffer = "";
      changePinFirstEntry = true;
      displayMessage("Change PIN", "Enter new PIN:");
      displayPinDots("");
    }
    else if (strcmp(action, "scan_rfid") == 0) {
      currentState = STATE_SCANNING_RFID;
      displayMessage("Scan RFID", "Tap your card...");
    }
    else if (strcmp(action, "dash_unlock") == 0) {
      dashVerified = true;
      Serial.println("Dashboard unlock received!");
      displayMessage("Phone OK", "Dashboard verified");

      sendEvent("access_attempt",
        "{\"method\":\"DASH\",\"success\":true,\"detail\":\"Dashboard unlock\"}");

      delay(500);
      checkAllFactors();
    }
  }
  else if (strcmp(event, "error") == 0) {
    const char* msg = data["message"] | "Unknown error";
    displayMessage("Error", msg);
    delay(2000);
    displayIdle();
  }
  else if (strcmp(event, "pin_verified") == 0) {
    bool success = data["success"] | false;
    if (success) {
      pinVerified = true;
      displayMessage("PIN OK", "");
      sendEvent("access_attempt",
        "{\"method\":\"PIN\",\"success\":true,\"detail\":\"PIN verified\"}");
      delay(500);
      checkAllFactors();
    } else {
      displayMessage("Wrong PIN", "");
      sendEvent("access_attempt",
        "{\"method\":\"PIN\",\"success\":false,\"detail\":\"Wrong PIN\"}");
      delay(1500);
      displayIdle();
    }
  }
}

void sendEvent(const String& event, const String& jsonData) {
  String message = "[\"" + event + "\"," + jsonData + "]";
  socketIO.sendEVENT(message);
  Serial.printf("[IO] Sent: %s\n", message.c_str());
}

// ═══════════════════════════════════════════════════
//  KEYPAD
// ═══════════════════════════════════════════════════

void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  Serial.printf("Key: %c | State: %d\n", key, currentState);

  switch (currentState) {

    case STATE_IDLE:
      if (key == '#') {
        currentState = STATE_ENTERING_PIN;
        pinBuffer = "";
        displayMessage("Enter PIN", "");
        displayPinDots("");
      }
      break;

    case STATE_ENTERING_PIN:
      if (key == '#') {
        verifyPin(pinBuffer);
        pinBuffer = "";
        currentState = STATE_IDLE;
      } else if (key == '*') {
        pinBuffer = "";
        currentState = STATE_IDLE;
        displayIdle();
      } else if (key >= '0' && key <= '9') {
        pinBuffer += key;
        displayMessage("Enter PIN", "");
        displayPinDots(pinBuffer);
        if (pinBuffer.length() >= 8) {
          verifyPin(pinBuffer);
          pinBuffer = "";
          currentState = STATE_IDLE;
        }
      }
      break;

    case STATE_CHANGING_PIN:
      if (key == '*') {
        newPinBuffer = "";
        confirmPinBuffer = "";
        currentState = STATE_IDLE;
        displayIdle();
        return;
      }
      if (key == '#') {
        String& activeBuffer = changePinFirstEntry ? newPinBuffer : confirmPinBuffer;

        if (activeBuffer.length() < 4) {
          displayMessage("Too Short", "Min 4 digits");
          delay(1000);
          activeBuffer = "";
          displayMessage(changePinFirstEntry ? "Change PIN" : "Confirm PIN",
                         changePinFirstEntry ? "Enter new PIN:" : "Re-enter PIN:");
          displayPinDots("");
          return;
        }

        if (changePinFirstEntry) {
          changePinFirstEntry = false;
          displayMessage("Confirm PIN", "Re-enter PIN:");
          displayPinDots("");
        } else {
          if (confirmPinBuffer == newPinBuffer) {
            sendEvent("pin_changed", "{\"new_pin\":\"" + confirmPinBuffer + "\"}");
            displayMessage("PIN Changed!", "");
            delay(1500);
          } else {
            displayMessage("Mismatch!", "Try again");
            delay(1500);
          }
          newPinBuffer = "";
          confirmPinBuffer = "";
          currentState = STATE_IDLE;
          displayIdle();
        }
      } else if (key >= '0' && key <= '9') {
        String& activeBuffer = changePinFirstEntry ? newPinBuffer : confirmPinBuffer;
        activeBuffer += key;
        displayMessage(changePinFirstEntry ? "Change PIN" : "Confirm PIN",
                       changePinFirstEntry ? "Enter new PIN:" : "Re-enter PIN:");
        displayPinDots(activeBuffer);
      }
      break;

    case STATE_UNLOCKED:
      if (key == '*') {
        lockDoor();
      }
      break;

    default:
      break;
  }
}

// ═══════════════════════════════════════════════════
//  PIN VERIFICATION
// ═══════════════════════════════════════════════════

void verifyPin(const String& pin) {
  if (pin.length() == 0) {
    displayIdle();
    return;
  }

  if (isConnected) {
    sendEvent("verify_pin", "{\"pin\":\"" + pin + "\"}");
    displayMessage("Checking PIN", "...");
  } else {
    displayMessage("No Server", "Can't verify PIN");
    delay(1500);
    displayIdle();
  }
}

// ═══════════════════════════════════════════════════
//  RFID
// ═══════════════════════════════════════════════════

void handleRFID() {
  rfid.PCD_Init();

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) uid += ":";
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  Serial.printf("RFID: %s\n", uid.c_str());

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (currentState == STATE_SCANNING_RFID) {
    sendEvent("rfid_scanned", "{\"tag_uid\":\"" + uid + "\",\"label\":\"My Card\"}");
    displayMessage("Card Scanned!", uid.substring(0, 14).c_str());
    delay(1500);
    currentState = STATE_IDLE;
    displayIdle();
    return;
  }

  bool authorized = false;
  for (const String& authUid : authorizedRFID) {
    if (authUid.equalsIgnoreCase(uid)) {
      authorized = true;
      break;
    }
  }

  if (authorized) {
    rfidVerified = true;
    displayMessage("RFID OK", uid.substring(0, 14).c_str());

    if (isConnected) {
      sendEvent("access_attempt",
        "{\"method\":\"RFID\",\"success\":true,\"detail\":\"" + uid + "\"}");
    }
    delay(500);
    checkAllFactors();
  } else {
    displayMessage("RFID Denied", uid.substring(0, 14).c_str());

    if (isConnected) {
      sendEvent("access_attempt",
        "{\"method\":\"RFID\",\"success\":false,\"detail\":\"" + uid + "\"}");
    }
    delay(1500);
    displayIdle();
  }
}

// ═══════════════════════════════════════════════════
//  MULTI-FACTOR CHECK & LOCK CONTROL
// ═══════════════════════════════════════════════════

void checkAllFactors() {
  int required = 0;
  int passed = 0;

  if (storedPinHash.length() > 0) {
    required++;
    if (pinVerified) passed++;
  }

  if (!authorizedRFID.empty()) {
    required++;
    if (rfidVerified) passed++;
  }

  if (isConnected) {
    required++;
    if (dashVerified) passed++;
  }

  Serial.printf("Factors: %d/%d\n", passed, required);

  if (passed > 0 && passed < required) {
    String progress = String(passed) + "/" + String(required) + " factors";
    displayMessage("Verifying...", progress.c_str());
  }

  if (required == 0) {
    displayMessage("No Auth Set", "Use dashboard");
    delay(2000);
    displayIdle();
    return;
  }

  if (passed >= required) {
    unlockDoor();
  }
}

void unlockDoor() {
  isLocked = false;
  currentState = STATE_UNLOCKED;
  unlockTime = millis();

  lockServo.attach(SERVO_PIN);
  lockServo.write(SERVO_UNLOCKED);
  delay(500);
  lockServo.detach();

  displayMessage("UNLOCKED", "* to re-lock");

  if (isConnected) {
    sendEvent("access_attempt",
      "{\"method\":\"ALL\",\"success\":true,\"detail\":\"All factors passed - UNLOCKED\"}");
  }

  Serial.println(">>> DOOR UNLOCKED <<<");

  pinVerified = false;
  rfidVerified = false;
  dashVerified = false;
}

void lockDoor() {
  isLocked = true;
  currentState = STATE_IDLE;

  lockServo.attach(SERVO_PIN);
  lockServo.write(SERVO_LOCKED);
  delay(500);
  lockServo.detach();

  if (isConnected) {
    sendEvent("access_attempt",
      "{\"method\":\"LOCK\",\"success\":true,\"detail\":\"Door locked\"}");
  }

  Serial.println(">>> DOOR LOCKED <<<");
  displayMessage("LOCKED", "");
  delay(1000);
  displayIdle();
}

void handleAutoLock() {
  if (currentState == STATE_UNLOCKED && millis() - unlockTime > AUTO_LOCK_TIMEOUT) {
    Serial.println("Auto-locking...");
    lockDoor();
  }
}

// ═══════════════════════════════════════════════════
//  OLED DISPLAY
// ═══════════════════════════════════════════════════

void displayMessage(const char* line1, const char* line2) {
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);

  display.setTextSize(1);
  display.setCursor(0, 30);
  display.println(line2);

  display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.setTextSize(1);
  display.print(WiFi.status() == WL_CONNECTED ? "W" : "~");
  display.print(" ");
  display.print(isConnected ? "S" : "-");
  display.print(" ");
  display.print(isLocked ? "LOCKED" : "OPEN");
  display.print(" ");
  if (pinVerified) display.print("P");
  if (rfidVerified) display.print("R");
  if (dashVerified) display.print("D");

  display.display();
}

void displayPinDots(const String& pin) {
  display.fillRect(0, 16, 128, 14, SSD1306_BLACK);
  display.setTextSize(2);
  display.setCursor(0, 16);
  for (unsigned int i = 0; i < pin.length(); i++) {
    display.print("*");
  }
  display.display();
}

void displayIdle() {
  int n = 0;
  if (storedPinHash.length() > 0) n++;
  if (!authorizedRFID.empty()) n++;
  if (isConnected) n++;

  String info = String(n) + " factors | # = PIN";

  if (isLocked) {
    displayMessage("LOCKED", info.c_str());
  } else {
    displayMessage("UNLOCKED", "* to lock");
  }
}
