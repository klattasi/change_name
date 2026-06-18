#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ModbusMaster.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── WiFi Access Point ─────────────────────────────────────────────────────────
#define WIFI_SSID   "WIFI_BATT_02"   // ไม่มีรหัสผ่าน (open AP)

// ── BLE ───────────────────────────────────────────────────────────────────────
#define BLE_NAME    "BATT_02"
#define SVC_UUID    "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define VOLT_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CURR_UUID   "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// ── RS485 / PZEM-017 (UART1) ─────────────────────────────────────────────────
// ตาม HardwareESP32Config.md: TX1=GPIO17, RX1=GPIO16, MAX13487 DE=GPIO4
#define RS485_TX    17
#define RS485_RX    16
#define RS485_DE    4    // DE/RE direction control (HIGH=transmit, LOW=receive)
#define PZEM_ADDR   0x01 // Modbus address ค่าเริ่มต้นของ PZEM-017

// ── Timing ────────────────────────────────────────────────────────────────────
#define READ_INTERVAL_MS 5000

// ── Globals ───────────────────────────────────────────────────────────────────
static float         g_voltage  = 0.0f;
static float         g_current  = 0.0f;
static unsigned long g_lastRead = 0;

static HardwareSerial  rs485Serial(1);   // UART1
static ModbusMaster    modbus;
static AsyncWebServer  httpServer(80);
static AsyncWebSocket  wsServer("/ws");

static BLECharacteristic* bleVolt = nullptr;
static BLECharacteristic* bleCurr = nullptr;

// ── RS485 direction callbacks for ModbusMaster ───────────────────────────────
void rs485PreTx()  { digitalWrite(RS485_DE, HIGH); }
void rs485PostTx() { digitalWrite(RS485_DE, LOW);  }

// ── BLE server callbacks — restart advertising after client disconnect ────────
class BLEEvents : public BLEServerCallbacks {
  void onConnect(BLEServer*)    override { Serial.println("[BLE] client connected");    }
  void onDisconnect(BLEServer*) override {
    Serial.println("[BLE] client disconnected — restarting advertising");
    BLEDevice::startAdvertising();
  }
};

// ── Read PZEM-017 Input Registers 0x0000–0x0001 (voltage, current) ───────────
static void readPZEM() {
  uint8_t rc = modbus.readInputRegisters(0x0000, 2);
  if (rc == ModbusMaster::ku8MBSuccess) {
    g_voltage = modbus.getResponseBuffer(0) / 100.0f;  // 0.01 V resolution
    g_current = modbus.getResponseBuffer(1) / 100.0f;  // 0.01 A resolution
    Serial.printf("[PZEM] V=%.2f V  I=%.2f A\n", g_voltage, g_current);
  } else {
    Serial.printf("[PZEM] Modbus error: 0x%02X\n", rc);
  }
}

// ── Push data to all WebSocket clients and BLE subscribers ───────────────────
static void broadcastData() {
  // JSON: {"v":12.34,"i":5.67}
  String json = "{\"v\":" + String(g_voltage, 2) +
                ",\"i\":" + String(g_current, 2) + "}";
  wsServer.textAll(json);

  if (bleVolt && bleCurr) {
    String vStr = String(g_voltage, 2);
    String iStr = String(g_current, 2);
    bleVolt->setValue(vStr.c_str());
    bleVolt->notify();
    bleCurr->setValue(iStr.c_str());
    bleCurr->notify();
  }
}

// ── WebSocket: send current values immediately when client connects ───────────
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                      AwsEventType type, void*, uint8_t*, size_t) {
  if (type == WS_EVT_CONNECT) {
    String json = "{\"v\":" + String(g_voltage, 2) +
                  ",\"i\":" + String(g_current, 2) + "}";
    client->text(json);
  }
}

// ── setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== BATT_02 starting ===");

  // RS485 + ModbusMaster
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  rs485Serial.begin(9600, SERIAL_8N2, RS485_RX, RS485_TX);
  modbus.begin(PZEM_ADDR, rs485Serial);
  modbus.preTransmission(rs485PreTx);
  modbus.postTransmission(rs485PostTx);
  Serial.println("[RS485] Modbus ready  TX=GPIO17 RX=GPIO16 DE=GPIO4");

  // WiFi Access Point (no password, no internet required)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID);
  Serial.printf("[WiFi] AP \"%s\"  IP: %s\n",
                WIFI_SSID, WiFi.softAPIP().toString().c_str());

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[SPIFFS] Mount failed — check data/ upload");
  } else {
    Serial.println("[SPIFFS] Mounted OK");
  }

  // HTTP + WebSocket
  wsServer.onEvent(onWsEvent);
  httpServer.addHandler(&wsServer);
  httpServer.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  httpServer.begin();
  Serial.println("[HTTP] Server started on port 80");

  // BLE
  BLEDevice::init(BLE_NAME);
  BLEServer*  bleServer  = BLEDevice::createServer();
  bleServer->setCallbacks(new BLEEvents());
  BLEService* bleService = bleServer->createService(SVC_UUID);

  bleVolt = bleService->createCharacteristic(
    VOLT_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  bleVolt->addDescriptor(new BLE2902());

  bleCurr = bleService->createCharacteristic(
    CURR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  bleCurr->addDescriptor(new BLE2902());

  bleService->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  Serial.printf("[BLE] Advertising as \"%s\"\n", BLE_NAME);

  Serial.println("=== Ready ===");
}

// ── loop ──────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();
  if (now - g_lastRead >= READ_INTERVAL_MS) {
    g_lastRead = now;
    readPZEM();
    broadcastData();
  }
  wsServer.cleanupClients();
}
