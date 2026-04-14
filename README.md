# NeuNVS

**Ultra-Fast, Secure, and Hardware-Protected NVS Wrapper for ESP32**

NeuNVS is a next-generation storage library for ESP32, designed to replace the standard `Preferences` and `EEPROM` libraries.
It focuses on extreme performance, data integrity, and physical hardware protection.

[![License: MIT](https://shields.io)](https://opensource.org)
[![Framework: Arduino](https://shields.io)](https://arduino.cc)
[![Platform: ESP32](https://shields.io)](https://espressif.com)

---

## Why NeuNVS?

Standard libraries like `Preferences` often carry heavy overhead due to string-based keys and lack of hardware safety.

**NeuNVS** solves this by using an ID-based system with built-in protection layers.

---

### Benchmark Results

Tested on ESP32-D0WDQ6 (Higher is better for speed, lower for latency):

| Feature                   | NeuNVS (Ours)  | Standard Preferences | Improvement          |
| :------------------------ | :------------- | :------------------- | :------------------- |
| **Write Speed (100 ops)** | **24.4 ms**    | 161.0 ms             | **6.5x Faster**      |
| **Dirty Check Skip**      | **127 us**     | 18,518 us            | **145x Faster**      |
| **RAM Usage**             | **Stack Only** | Heap Allocation      | **No Fragmentation** |

---

## Key Features-

**XOR Data Integrity:** Every data block is protected with an ultra-lightweight XOR checksum and size validation. No more corrupt data!

**Hardware Lockdown:** Exclusive "Abuse Protection" that automatically locks Flash writes if a loop-bug or frequent commits are detected.

**Smart Dirty Check:** Internal comparison prevents unnecessary Flash writes, extending your hardware's lifespan significantly.

**Auto-Namespace:** Seamlessly handle multiple instances without manual namespace management.

**Ultra-Lightweight:** Zero dynamic memory allocation (no heap fragmentation).

---

## Quick Start###

Installation

1. Download this repository as a `.zip`.
2. In Arduino IDE, go to **Sketch** -> **Include Library** -> **Add .ZIP Library**.

---

### Basic Usage

```cpp
#include <NeuNVS.h>

void setup() {
    Serial.begin(115200);

    // Initialize with 1s auto-commit interval and 5s lockdown duration
    NVS.begin(1000, 5);

    // Write data (ID-based, no strings needed!)
    uint32_t myData = 1337;
    NVS.put(1, myData);

    // Read data safely
    uint32_t savedData = NVS.get<uint32_t>(1);
    Serial.printf("Saved Data: %u\n", savedData);
}

void loop() {
    // Call update() to process auto-commits safely
    NVS.update();
}
```

---

## Advanced Features## Error Callbacks

Get notified when hardware issues or abuse occur:

```cpp
void onStorageError(uint8_t code, uint8_t id) {
    if (code == NeuNVS::ERR_LOCKDOWN) {
        Serial.println("Hardware Protected: Too many writes!");
    }
}
void setup() {
    NVS.onError(onStorageError);
    NVS.begin();
}
```

---

## Manual Commit & Clean

```cpp
NVS.put(5, 100);
NVS.commit();   // Force immediate write
NVS.clearAll(); // Factory reset storage
```

---

## Technical Specs

- Namespace: Automatic (ns0, ns1, ...)
- ID Range: 0 - 255 (uint8_t)
- Key Format: Internal i[ID] (e.g., i255)
- Overhead: 4-byte header per entry (XOR + Size)

---

## License

Distributed under the MIT License. See LICENSE for more information.

---

### Created by ulywae @Neu

---
