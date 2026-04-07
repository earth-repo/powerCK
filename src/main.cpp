#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// =====================================================
// ข้อมูลลับ — แก้ไขใน secrets.h (ไม่ push ขึ้น GitHub)
// คัดลอก secrets.h.example → secrets.h แล้วกรอกค่าจริง
// =====================================================
#include "secrets.h"

// =====================================================
// ตั้งค่าระบบทั่วไป (ไม่มีข้อมูลลับ — push ขึ้น GitHub ได้)
// =====================================================
#define DEVICE_NAME   "อาคารศูนย์คอมพิวเตอร์"
#define AC_SENSE_PIN  34       // GPIO34 รับสัญญาณจาก AC Detection Module
#define LED_STATUS    2        // GPIO2 LED บนบอร์ด ESP32
#define DEBOUNCE_MS   3000     // ต้องเปลี่ยนสถานะค้างนาน 3 วินาที ถึงนับว่าเปลี่ยนจริง
#define INVERT_LOGIC  false    // true = สลับ logic เผื่อโมดูลทำงานกลับด้าน
#define HEARTBEAT_INTERVAL 60000  // ส่ง heartbeat ทุก 60 วินาที

// =====================================================
// รายชื่อผู้รับแจ้งเตือน — เพิ่ม/ลบได้ตรงนี้
// =====================================================
const char* TARGET_IDS[] = {"30066","50194"};
const int   TARGET_IDS_COUNT = sizeof(TARGET_IDS) / sizeof(TARGET_IDS[0]);

// =====================================================
// ตั้งค่า NTP สำหรับเวลาจริง (เขตเวลาไทย UTC+7)
// =====================================================
#define NTP_SERVER    "pool.ntp.org"
#define GMT_OFFSET    25200    // UTC+7 = 7*3600
#define DST_OFFSET    0

// =====================================================
// ตัวแปรสถานะระบบ
// =====================================================
bool powerStatus       = false;   // สถานะไฟปัจจุบัน (true = มีไฟ)
bool lastRawReading    = false;   // ค่าที่อ่านได้ล่าสุดจาก GPIO
unsigned long debounceStart = 0;  // เวลาเริ่มต้นนับ debounce
bool debouncing        = false;   // กำลังอยู่ในช่วง debounce หรือไม่

unsigned long lastBlinkTime     = 0;  // เวลาที่กระพริบ LED ครั้งล่าสุด
bool ledState                   = false;
unsigned long lastHeartbeatTime = 0;  // เวลาที่ส่ง heartbeat ครั้งล่าสุด
bool ntpSynced                  = false; // NTP sync สำเร็จหรือยัง

// =====================================================
// Function Prototypes
// =====================================================
String firebaseUrl(const char* path);
void connectWiFi();
bool readACPower();
bool sendNotification(bool isPowerOn);
void blinkStatusLED();
void syncNTP();
time_t getTimestamp();
bool updateFirebaseStatus(bool isPowerOn);
bool logFirebaseEvent(bool isPowerOn);
void sendHeartbeat();

// =====================================================
// firebaseUrl() — สร้าง URL สำหรับ Firebase REST API
// ถ้ามี FIREBASE_AUTH จะใส่ ?auth= ให้อัตโนมัติ
// =====================================================
String firebaseUrl(const char* path) {
    String url = String("https://") + FIREBASE_HOST + "/" + path + ".json";
    if (strlen(FIREBASE_AUTH) > 0) {
        url += "?auth=" + String(FIREBASE_AUTH);
    }
    return url;
}

// =====================================================
// connectWiFi() — เชื่อมต่อ WiFi พร้อม retry สูงสุด 40 ครั้ง
// =====================================================
void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.printf("[%lu] กำลังเชื่อมต่อ WiFi: %s\n", millis(), WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 40) {
        delay(500);
        Serial.print(".");
        retryCount++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[%lu] เชื่อมต่อ WiFi สำเร็จ IP: %s\n", millis(), WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[%lu] เชื่อมต่อ WiFi ไม่สำเร็จ หลังจากลอง %d ครั้ง\n", millis(), retryCount);
    }
}

// =====================================================
// syncNTP() — ซิงค์เวลาจาก NTP Server
// =====================================================
void syncNTP() {
    Serial.printf("[%lu] กำลังซิงค์เวลาจาก NTP...\n", millis());
    configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);

    // รอจน NTP sync สำเร็จ (สูงสุด 10 วินาที)
    int retry = 0;
    while (time(nullptr) < 1000000000 && retry < 20) {
        delay(500);
        retry++;
    }

    if (time(nullptr) > 1000000000) {
        ntpSynced = true;
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        Serial.printf("[%lu] NTP sync สำเร็จ: %04d-%02d-%02d %02d:%02d:%02d\n",
            millis(),
            timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
            timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.printf("[%lu] NTP sync ไม่สำเร็จ — จะใช้ millis() แทน\n", millis());
    }
}

// =====================================================
// getTimestamp() — คืนค่า Unix timestamp (วินาที)
// =====================================================
time_t getTimestamp() {
    return time(nullptr);
}

// =====================================================
// readACPower() — อ่านสถานะไฟ AC จาก GPIO34
// คืนค่า true = มีไฟ AC, false = ไฟดับ
// =====================================================
bool readACPower() {
    bool raw = digitalRead(AC_SENSE_PIN);
    if (INVERT_LOGIC) {
        raw = !raw;
    }
    return raw;
}

// =====================================================
// updateFirebaseStatus() — อัปเดตสถานะปัจจุบันใน Firebase RTDB
// ใช้ PATCH เพื่ออัปเดตเฉพาะ field ที่ส่งไป
// =====================================================
bool updateFirebaseStatus(bool isPowerOn) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // สร้าง URL สำหรับ PATCH ไปที่ /power_monitor/current.json
    String url = firebaseUrl("power_monitor/current");

    // สร้าง JSON payload
    JsonDocument doc;
    doc["status"]    = isPowerOn ? "power_on" : "power_off";
    doc["device"]    = DEVICE_NAME;
    doc["timestamp"] = getTimestamp();
    doc["uptime_ms"] = millis();
    doc["wifi_rssi"] = WiFi.RSSI();

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    // ใช้ PATCH เพื่ออัปเดตเฉพาะ field
    int httpCode = http.PATCH(jsonPayload);
    bool success = (httpCode == 200);

    if (success) {
        Serial.printf("[%lu] Firebase: อัปเดตสถานะสำเร็จ\n", millis());
    } else {
        Serial.printf("[%lu] Firebase: อัปเดตสถานะล้มเหลว HTTP %d\n", millis(), httpCode);
    }

    http.end();
    return success;
}

// =====================================================
// logFirebaseEvent() — บันทึก event ลง Firebase RTDB
// ใช้ POST เพื่อเพิ่ม record ใหม่ (auto-generate key)
// =====================================================
bool logFirebaseEvent(bool isPowerOn) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    // POST ไปที่ /power_monitor/events.json เพื่อสร้าง record ใหม่
    String url = firebaseUrl("power_monitor/events");

    JsonDocument doc;
    doc["status"]    = isPowerOn ? "power_on" : "power_off";
    doc["device"]    = DEVICE_NAME;
    doc["timestamp"] = getTimestamp();
    doc["message"]   = isPowerOn ? "ไฟฟ้ากลับมาเป็นปกติ" : "ตรวจพบไฟฟ้าดับ";

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(10000);

    int httpCode = http.POST(jsonPayload);
    bool success = (httpCode == 200);

    if (success) {
        String response = http.getString();
        Serial.printf("[%lu] Firebase: บันทึก event สำเร็จ — %s\n", millis(), response.c_str());
    } else {
        Serial.printf("[%lu] Firebase: บันทึก event ล้มเหลว HTTP %d\n", millis(), httpCode);
    }

    http.end();
    return success;
}

// =====================================================
// sendHeartbeat() — ส่ง heartbeat ไป Firebase ทุก 60 วินาที
// เพื่อให้ Dashboard รู้ว่า ESP32 ยังออนไลน์อยู่
// =====================================================
void sendHeartbeat() {
    unsigned long now = millis();
    if (now - lastHeartbeatTime < HEARTBEAT_INTERVAL) {
        return;
    }
    lastHeartbeatTime = now;

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    String url = firebaseUrl("power_monitor/current");

    JsonDocument doc;
    doc["timestamp"] = getTimestamp();
    doc["uptime_ms"] = millis();
    doc["wifi_rssi"] = WiFi.RSSI();

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);

    int httpCode = http.PATCH(jsonPayload);
    http.end();

    if (httpCode == 200) {
        Serial.printf("[%lu] Firebase: heartbeat สำเร็จ\n", millis());
    } else {
        Serial.printf("[%lu] Firebase: heartbeat ล้มเหลว HTTP %d\n", millis(), httpCode);
    }
}

// =====================================================
// sendNotification() — ส่ง HTTP POST แจ้งเตือนผ่าน WeLPRU API
// + อัปเดต Firebase พร้อมกัน
// isPowerOn: true = ไฟกลับมา, false = ไฟดับ
// retry สูงสุด 3 ครั้ง ห่างกัน 2 วินาที
// =====================================================
bool sendNotification(bool isPowerOn) {
    // ตรวจสอบ WiFi ก่อนส่ง — ถ้าหลุดให้ reconnect
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[%lu] WiFi หลุด — กำลัง reconnect ก่อนส่ง notification\n", millis());
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[%lu] ไม่สามารถ reconnect WiFi ได้ — ยกเลิกการส่ง\n", millis());
            return false;
        }
    }

    // ===== ส่วนที่ 1: อัปเดต Firebase =====
    updateFirebaseStatus(isPowerOn);
    logFirebaseEvent(isPowerOn);

    // ===== ส่วนที่ 2: ส่ง WeLPRU Notification =====
    const char* title;
    const char* body;
    const char* status;

    if (isPowerOn) {
        title  = "⚡ ไฟฟ้ากลับมาเป็นปกติ";
        body   = "ตรวจพบไฟฟ้ากลับมาเป็นปกติแล้ว";
        status = "power_on";
    } else {
        title  = "🔌 ตรวจพบไฟฟ้าดับ";
        body   = "ตรวจพบไฟฟ้าดับ กรุณาตรวจสอบ";
        status = "power_off";
    }

    // สร้าง JSON payload สำหรับ WeLPRU API
    JsonDocument doc;
    doc["topic"]  = "personnel";
    doc["title"]  = title;
    doc["body"]   = body;
    doc["target_group"] = "personnel";

    // รายชื่อผู้รับแจ้งเตือน (อ่านจาก TARGET_IDS ด้านบน)
    JsonArray targetIds = doc["target_ids"].to<JsonArray>();
    for (int i = 0; i < TARGET_IDS_COUNT; i++) {
        targetIds.add(TARGET_IDS[i]);
    }

    JsonObject data = doc["data"].to<JsonObject>();
    data["device"] = DEVICE_NAME;
    data["status"] = status;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    Serial.printf("[%lu] กำลังส่ง notification: %s\n", millis(), isPowerOn ? "ไฟกลับมา" : "ไฟดับ");
    Serial.printf("[%lu] Payload: %s\n", millis(), jsonPayload.c_str());

    // ลองส่ง HTTP POST สูงสุด 3 ครั้ง
    for (int attempt = 1; attempt <= 3; attempt++) {
        HTTPClient http;
        http.begin(API_URL);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("X-API-Key", API_KEY);
        http.setTimeout(10000);

        int httpCode = http.POST(jsonPayload);

        if (httpCode == 200) {
            String response = http.getString();
            Serial.printf("[%lu] ส่ง notification สำเร็จ (ครั้งที่ %d) HTTP %d\n", millis(), attempt, httpCode);
            Serial.printf("[%lu] Response: %s\n", millis(), response.c_str());
            http.end();
            return true;
        } else {
            Serial.printf("[%lu] ส่ง notification ล้มเหลว ครั้งที่ %d/%d HTTP %d\n", millis(), attempt, 3, httpCode);
            if (httpCode > 0) {
                String response = http.getString();
                Serial.printf("[%lu] Response: %s\n", millis(), response.c_str());
            }
            http.end();

            if (attempt < 3) {
                Serial.printf("[%lu] รอ 2 วินาทีก่อน retry...\n", millis());
                delay(2000);
            }
        }
    }

    Serial.printf("[%lu] ส่ง notification ไม่สำเร็จหลังจาก retry 3 ครั้ง\n", millis());
    return false;
}

// =====================================================
// blinkStatusLED() — กระพริบ LED แบบ non-blocking ทุก 500ms
// =====================================================
void blinkStatusLED() {
    unsigned long now = millis();
    if (now - lastBlinkTime >= 500) {
        lastBlinkTime = now;
        ledState = !ledState;
        digitalWrite(LED_STATUS, ledState ? HIGH : LOW);
    }
}

// =====================================================
// setup() — เริ่มต้นระบบ
// =====================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("==========================================");
    Serial.println("  ระบบตรวจสอบไฟฟ้าดับ ESP32");
    Serial.println("  WeLPRU + Firebase Dashboard");
    Serial.println("==========================================");

    // ตั้งค่า GPIO
    pinMode(AC_SENSE_PIN, INPUT);
    pinMode(LED_STATUS, OUTPUT);
    digitalWrite(LED_STATUS, LOW);

    // เชื่อมต่อ WiFi
    connectWiFi();

    // ซิงค์เวลาจาก NTP — ต้องทำหลังเชื่อม WiFi
    syncNTP();

    // อ่านสถานะไฟเริ่มต้น — ไม่ส่ง notification ตอนเริ่มเครื่อง
    powerStatus = readACPower();
    lastRawReading = powerStatus;
    debouncing = false;

    // ส่งสถานะเริ่มต้นไป Firebase (ไม่ส่ง notification / ไม่ log event)
    updateFirebaseStatus(powerStatus);

    Serial.printf("[%lu] สถานะไฟเริ่มต้น: %s\n", millis(), powerStatus ? "มีไฟ (AC ON)" : "ไม่มีไฟ (AC OFF)");
    Serial.printf("[%lu] อุปกรณ์: %s\n", millis(), DEVICE_NAME);
    Serial.printf("[%lu] Debounce: %d ms\n", millis(), DEBOUNCE_MS);
    Serial.printf("[%lu] Invert Logic: %s\n", millis(), INVERT_LOGIC ? "YES" : "NO");
    Serial.printf("[%lu] Firebase Host: %s\n", millis(), FIREBASE_HOST);
    Serial.printf("[%lu] Heartbeat Interval: %d ms\n", millis(), HEARTBEAT_INTERVAL);
    Serial.println("==========================================");
    Serial.printf("[%lu] ระบบพร้อมทำงาน\n", millis());
}

// =====================================================
// loop() — วนลูปตรวจสอบสถานะไฟ
// =====================================================
void loop() {
    // ตรวจสอบ WiFi — ถ้าหลุดให้ reconnect อัตโนมัติ
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[%lu] WiFi หลุด — กำลัง reconnect...\n", millis());
        connectWiFi();
    }

    // อ่านสถานะไฟ AC จาก GPIO34
    bool currentReading = readACPower();

    // ระบบ Debounce — ป้องกันการสั่นของสัญญาณ
    if (currentReading != powerStatus) {
        if (!debouncing) {
            debouncing = true;
            debounceStart = millis();
            lastRawReading = currentReading;
            Serial.printf("[%lu] ตรวจพบสถานะเปลี่ยน — เริ่ม debounce %d ms\n", millis(), DEBOUNCE_MS);
        } else if (currentReading != lastRawReading) {
            debouncing = false;
            Serial.printf("[%lu] สถานะกลับเหมือนเดิมระหว่าง debounce — ยกเลิก\n", millis());
        } else if (millis() - debounceStart >= DEBOUNCE_MS) {
            debouncing = false;
            powerStatus = currentReading;

            if (powerStatus) {
                Serial.printf("[%lu] ===== ไฟฟ้ากลับมาเป็นปกติ =====\n", millis());
                sendNotification(true);
            } else {
                Serial.printf("[%lu] ===== ตรวจพบไฟฟ้าดับ =====\n", millis());
                sendNotification(false);
            }
        }
    } else {
        if (debouncing) {
            debouncing = false;
            Serial.printf("[%lu] สถานะกลับเป็นปกติ — ยกเลิก debounce\n", millis());
        }
    }

    // ส่ง heartbeat ไป Firebase ทุก 60 วินาที
    sendHeartbeat();

    // กระพริบ LED บอกว่าระบบยังทำงาน
    blinkStatusLED();

    // หน่วงเวลา 100ms ก่อนวนรอบใหม่
    delay(100);
}
