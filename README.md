# ระบบตรวจสอบไฟฟ้าดับ ESP32 + WeLPRU + Firebase Dashboard

ระบบตรวจจับไฟฟ้า AC 220V ดับ/กลับมา แล้วส่งแจ้งเตือนผ่าน WeLPRU API อัตโนมัติ
พร้อม Dashboard แบบ real-time ผ่าน Firebase Realtime Database + GitHub Pages

## สถาปัตยกรรมระบบ

```
  ESP32 ──► Firebase Realtime Database ◄── GitHub Pages Dashboard
    │                                          (ผู้ใช้เปิดดูผ่าน Browser)
    │
    └──► WeLPRU Notification API ──► แจ้งเตือนผู้ใช้
```

## วัตถุประสงค์

1. ตรวจจับสถานะไฟฟ้า AC 220V แบบ real-time
2. ส่งการแจ้งเตือนอัตโนมัติเมื่อไฟฟ้าดับหรือกลับมา ผ่าน WeLPRU Notification API
3. บันทึกสถานะและประวัติเหตุการณ์ลง Firebase Realtime Database
4. แสดง Dashboard แบบ real-time ผ่าน GitHub Pages
5. ทำงานได้ต่อเนื่องแม้ไฟฟ้าดับ โดยใช้ UPS สำรองไฟให้ ESP32 และ WiFi Router

## ฮาร์ดแวร์ที่ใช้

| อุปกรณ์ | จำนวน | ราคาประมาณ (บาท) |
|---------|--------|------------------|
| ESP32 DevKit V1 | 1 | 150 |
| AC Voltage Detection Module (PC817 Optocoupler) | 1 | 35 |
| UPS 800VA | 1 | 1,500 |
| สาย Jumper | 3 เส้น | 10 |
| สาย USB Micro-B (สำหรับจ่ายไฟ ESP32) | 1 | 30 |
| **รวม** | | **~1,725** |

## แผนผังการต่อวงจร

```
    AC 220V (ปลั๊กผนังตรง ไม่ผ่าน UPS)
         |
         v
  +------------------+
  | AC Detection     |
  | Module (PC817)   |
  |                  |
  | VCC --- 3.3V ESP |
  | GND --- GND  ESP |
  | OUT --- GPIO34   |
  +------------------+

  +------------------+
  | ESP32 DevKit V1  |
  |                  |
  | GPIO34 = AC IN   |
  | GPIO2  = LED     |
  |                  |
  | จ่ายไฟผ่าน USB   |
  | จาก UPS          |
  +------------------+
```

## แผนผังระบบรวม

```
  ปลั๊กผนัง AC 220V (ไฟจากการไฟฟ้า)
  ├─── AC Detection Module ──── GPIO34 ──┐
  │    (เสียบผนังตรง ไม่ผ่าน UPS)        │
  │                                       │
  └─── UPS 800VA                          │
       ├─── ESP32 DevKit V1 ◄────────────┘
       │    (ตรวจจับสถานะไฟ + ส่ง API)
       │         │
       │         │ WiFi
       │         v
       ├─── WiFi Router ────► Internet
       │                       ├──► WeLPRU API → แจ้งเตือนผู้ใช้
       │                       └──► Firebase RTDB → Dashboard
       │
       └─── (สำรองไฟ ~20-30 นาที)

  เมื่อไฟดับ:
  - AC Detection Module ไม่มีไฟ → GPIO34 = LOW
  - ESP32 + Router ยังทำงานได้จาก UPS
  - ESP32 ส่ง notification + อัปเดต Firebase

  เมื่อไฟกลับมา:
  - AC Detection Module มีไฟ → GPIO34 = HIGH
  - ESP32 ส่ง notification + อัปเดต Firebase
```

## การตั้งค่าก่อน Build

### 1. ตั้งค่า Firebase

1. ไปที่ [Firebase Console](https://console.firebase.google.com/) สร้างโปรเจกต์ใหม่
2. เปิด **Realtime Database** → Create Database → เลือก region → Start in **test mode**
3. คัดลอก **Project ID** (เช่น `my-power-monitor`)
4. ไปที่ Project Settings → General → คัดลอก **Web API Key**
5. ไปที่ Realtime Database → คัดลอก **Database Secret** จาก Project Settings → Service accounts → Database secrets

### 2. ตั้งค่า Firebase Security Rules (แนะนำ)

เปิด Realtime Database → Rules แล้วตั้งค่า:

```json
{
  "rules": {
    "power_monitor": {
      "current": {
        ".read": true,
        ".write": true
      },
      "events": {
        ".read": true,
        ".write": true,
        ".indexOn": ["timestamp"]
      }
    }
  }
}
```

### 3. แก้ไข src/main.cpp

```cpp
// WiFi
#define WIFI_SSID     "ชื่อ WiFi ของคุณ"
#define WIFI_PASSWORD "รหัสผ่าน WiFi"

// WeLPRU API
#define API_KEY       "API Key ที่ได้รับจากระบบ WeLPRU"
#define DEVICE_NAME   "ชื่อสถานที่ติดตั้ง"

// Firebase
#define FIREBASE_HOST "your-project-id-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "your_database_secret_here"
```

ตั้งค่าเพิ่มเติม (ถ้าจำเป็น):

```cpp
#define DEBOUNCE_MS        3000    // ระยะเวลา debounce (มิลลิวินาที)
#define INVERT_LOGIC       false   // เปลี่ยนเป็น true ถ้าโมดูลให้ logic กลับด้าน
#define HEARTBEAT_INTERVAL 60000   // ส่ง heartbeat ไป Firebase ทุกกี่ ms
```

## วิธี Build และ Upload

### ผ่าน PlatformIO CLI

```bash
# Build
pio run

# Upload ไปยัง ESP32
pio run -t upload

# เปิด Serial Monitor ดู log
pio device monitor
```

### ผ่าน VS Code + PlatformIO Extension

1. เปิดโฟลเดอร์โปรเจกต์ใน VS Code
2. กดปุ่ม Build (เครื่องหมายถูก) ที่แถบด้านล่าง
3. กดปุ่ม Upload (ลูกศรขวา) ที่แถบด้านล่าง
4. กดปุ่ม Serial Monitor (ปลั๊กไฟ) เพื่อดู log

## ตั้งค่า GitHub Pages Dashboard

### 1. Push โปรเจกต์ขึ้น GitHub

```bash
git init
git add .
git commit -m "Initial commit"
git remote add origin https://github.com/<username>/<repo>.git
git push -u origin main
```

### 2. เปิด GitHub Pages

1. ไปที่ Settings → Pages ของ repository
2. Source: **Deploy from a branch**
3. Branch: `main` → Folder: `/docs`
4. กด Save

### 3. เปิด Dashboard

1. เปิด URL: `https://<username>.github.io/<repo>/`
2. กรอก **Firebase API Key** และ **Project ID**
3. กด **เชื่อมต่อ**
4. Dashboard จะแสดงสถานะแบบ real-time

### โครงสร้างข้อมูลใน Firebase

```json
{
  "power_monitor": {
    "current": {
      "status": "power_on",
      "device": "อาคารวิทยาศาสตร์ ชั้น 3",
      "timestamp": 1712500000,
      "uptime_ms": 3600000,
      "wifi_rssi": -45
    },
    "events": {
      "-auto-id-1": {
        "status": "power_off",
        "device": "อาคารวิทยาศาสตร์ ชั้น 3",
        "timestamp": 1712499000,
        "message": "ตรวจพบไฟฟ้าดับ"
      },
      "-auto-id-2": {
        "status": "power_on",
        "device": "อาคารวิทยาศาสตร์ ชั้น 3",
        "timestamp": 1712500000,
        "message": "ไฟฟ้ากลับมาเป็นปกติ"
      }
    }
  }
}
```

## วิธีทดสอบ

1. Upload โค้ดลง ESP32 แล้วเปิด Serial Monitor
2. ตรวจสอบว่า ESP32 เชื่อมต่อ WiFi และ NTP sync สำเร็จ
3. เปิด Firebase Console → Realtime Database ดูว่ามีข้อมูลใน `power_monitor/current`
4. เปิด Dashboard บน GitHub Pages ตรวจสอบว่าแสดงสถานะ "ออนไลน์"
5. **ทดสอบไฟดับ:** ถอดปลั๊ก AC Detection Module ออกจากผนัง
   - Serial Monitor จะแสดง "ตรวจพบไฟฟ้าดับ" หลังจาก 3 วินาที
   - Firebase จะอัปเดตสถานะเป็น `power_off`
   - Dashboard จะเปลี่ยนเป็นสีแดงแบบ real-time
   - ระบบจะส่ง notification แจ้งไฟดับผ่าน WeLPRU
6. **ทดสอบไฟกลับมา:** เสียบปลั๊ก AC Detection Module กลับคืน
   - Serial Monitor จะแสดง "ไฟฟ้ากลับมาเป็นปกติ" หลังจาก 3 วินาที
   - Firebase จะอัปเดตสถานะเป็น `power_on`
   - Dashboard จะเปลี่ยนเป็นสีเขียวแบบ real-time
7. LED บน ESP32 (GPIO2) จะกระพริบตลอดเวลา แสดงว่าระบบทำงานปกติ

## ข้อจำกัดและแนวทางพัฒนาต่อ

### ข้อจำกัดปัจจุบัน

- ระบบทำงานได้นานเท่าที่ UPS สำรองไฟได้ (~20-30 นาที สำหรับ UPS 800VA)
- ต้องมี WiFi ในการส่งแจ้งเตือน ถ้า Router ไม่ได้ต่อ UPS จะส่งแจ้งเตือนไม่ได้
- Firebase Realtime Database มี free tier จำกัดที่ 1GB storage / 10GB transfer ต่อเดือน
- Dashboard ต้องกรอก Firebase config ครั้งแรก (เก็บใน localStorage)

### แนวทางพัฒนาต่อ

- เพิ่ม OLED Display แสดงสถานะบนตัวเครื่อง
- เพิ่ม OTA Update เพื่ออัปเดต firmware ผ่าน WiFi
- เพิ่มกราฟแสดงสถิติไฟดับรายวัน/รายเดือนบน Dashboard
- รองรับหลายอุปกรณ์บน Dashboard เดียวกัน
- เพิ่มการวัดแรงดันไฟฟ้าจริง (ใช้ ZMPT101B voltage sensor)
- เพิ่ม Firebase Authentication สำหรับความปลอดภัย

## License

MIT License

Copyright (c) 2026

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
