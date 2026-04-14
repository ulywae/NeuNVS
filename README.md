# NeuNVS

**Ultra-Fast, Secure, and Hardware-Protected NVS Wrapper for ESP32**

NeuNVS is a next-generation storage library for ESP32, designed to replace the standard `Preferences` and `EEPROM` libraries.
It focuses on extreme performance, data integrity, and physical hardware protection with **XOR checksum** and **auto-lockdown mechanism**.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue?logo=espressif&logoColor=white)](https://espressif.com)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-00878F?logo=arduino&logoColor=white)](https://arduino.cc)
[![C++: 11](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://isocpp.org/)

---

## Why NeuNVS?

Standard libraries like `Preferences` often carry heavy overhead due to string-based keys, lack of hardware safety, and **no corruption detection**.

**NeuNVS** solves this by using:
- **ID-based system** (0-255) for ultra-fast access
- **XOR checksum** on every data block
- **Auto-lockdown** to prevent flash wear from buggy loops
- **Zero heap allocation** - no fragmentation!

---

## Benchmark Results

Tested on ESP32-D0WDQ6 (Higher is better for speed, lower for latency):

| Feature                   | NeuNVS (Ours)  | Standard Preferences | Improvement          |
| :------------------------ | :------------- | :------------------- | :------------------- |
| **Write Speed (100 ops)** | **24.4 ms**    | 161.0 ms             | **6.5x Faster**      |
| **Dirty Check Skip**      | **127 us**     | 18,518 us            | **145x Faster**      |
| **RAM Usage**             | **Stack Only** | Heap Allocation      | **No Fragmentation** |

---

## Key Features

- **XOR Data Integrity:** Every data block (including Strings!) is protected with ultra-lightweight XOR checksum and size validation. No more corrupt data!

- **Hardware Lockdown:** Exclusive "Abuse Protection" that automatically locks Flash writes if a loop-bug or frequent commits are detected (configurable threshold).

- **Smart Dirty Check:** Internal comparison prevents unnecessary Flash writes, extending your hardware's lifespan significantly.

- **Auto-Namespace:** Seamlessly handle multiple instances with automatic namespace allocation (`ns0`, `ns1`, ...).

- **Ultra-Lightweight:** Minimal stack usage, no dynamic memory allocation for POD types.

- **Full String Support:** String data is also protected with XOR checksum (unlike many alternatives).

---

## Installation

### Arduino IDE
1. Download this repository as a `.zip`
2. Go to **Sketch** → **Include Library** → **Add .ZIP Library**
3. Select the downloaded file

### PlatformIO
```ini
lib_deps =
    https://github.com/ulywae/NeuNVS.git
```

---

## Quick Start

### Basic Usage

```cpp
#include <NeuNVS.h>

void setup() {
    Serial.begin(115200);

    // Initialize with 1s auto-commit interval, 5s lockdown duration, max 5 commits before lockdown
    if (!NeuNVS.begin(1000, 5, 5)) {
        Serial.println("Failed to initialize NeuNVS!");
        return;
    }

    // Write data (ID-based, no strings needed!)
    uint32_t myData = 1337;
    NeuNVS.put(1, myData);

    // Read data safely with XOR validation
    uint32_t savedData = NeuNVS.get<uint32_t>(1);
    Serial.printf("Saved Data: %u\n", savedData);
    
    // String with XOR protection
    NeuNVS.putString(10, "Hello NeuNVS!");
    String str = NeuNVS.getString(10);
    Serial.println(str);
}

void loop() {
    // Call update() to process auto-commits safely
    NeuNVS.update();
}
```

---

## Advanced Features

### Error Callbacks

Get notified when hardware issues or data corruption occurs:

```cpp
void onStorageError(uint8_t code, uint8_t id) {
    switch(code) {
        case NeuNVS::ERR_LOCKDOWN:
            Serial.println("Hardware Protected: Too many writes!");
            break;
        case NeuNVS::ERR_DATA_CORRUPT:
            Serial.printf("Data corrupted at ID: %d\n", id);
            break;
        case NeuNVS::ERR_SIZE_MISMATCH:
            Serial.printf("Size mismatch for ID: %d\n", id);
            break;
        case NeuNVS::ERR_WRITE_FAILED:
            Serial.println("Flash write failed!");
            break;
    }
}

void setup() {
    NeuNVS.onError(onStorageError);
    NeuNVS.begin();
}
```

### Manual Commit & Clean

```cpp
NeuNVS.put(5, 100);
NeuNVS.commit();   // Force immediate write

NeuNVS.remove(5);  // Delete specific ID
NeuNVS.clearAll(); // Factory reset entire namespace
```

### Multiple Instances

```cpp
NeuNVS configStorage;
NeuNVS userStorage;

void setup() {
    configStorage.begin(1000, 5, 3);  // ns0
    userStorage.begin(2000, 10, 5);   // ns1
    
    configStorage.put(1, 9600);       // Baud rate config
    userStorage.putString(1, "Alice"); // Username
}
```

### Check Lockdown Status

```cpp
if (NeuNVS.isLocked()) {
    Serial.println("System is locked! Waiting for cooldown...");
} else {
    NeuNVS.put(99, 123);
}
```

---

### Error Codes

Code Name Description
0 ERR_NONE No error
1 ERR_LOCKDOWN Hardware protection active (too many writes)
2 ERR_WRITE_FAILED Flash write operation failed
3 ERR_ID_NOT_FOUND ID doesn't exist in storage
4 ERR_DATA_CORRUPT XOR checksum mismatch (data corrupted!)
5 ERR_SIZE_MISMATCH Data type size doesn't match stored data
6 ERR_INSTANCE_INVALID Too many instances created (max 254)

---

## API Reference

### Initialization

Method Description
begin(intervalMs, lockSec, maxCommits) Initialize NVS with auto-commit interval (ms), lockdown duration (seconds), and max commits before lockdown
end() Close NVS handle and cleanup
update() Call in loop() to process auto-commits

### Data Operations

Method Description
put<T>(id, value) Store any POD type (int, float, struct, etc.)
get<T>(id, defaultValue) Retrieve data with XOR validation
putString(id, value) Store String with XOR protection
getString(id, defaultValue) Retrieve String with XOR validation
exists(id) Check if ID exists
remove(id) Delete specific ID
clearAll() Delete all data in namespace
commit() Force immediate write

### Status & Info

Method Description
isLocked() Check if hardware lockdown is active
isValid() Check if instance was created successfully
getNamespace() Get current namespace name
getTotalFreeEntries() Get free NVS entries (global)
dump(id) Print hex dump for debugging

---

## Technical Specifications

Parameter Value
Namespace Auto (ns0 - ns253)
ID Range 0 - 255 (uint8_t)
Key Format i[ID] (e.g., i255)
Overhead 4 bytes per entry (XOR + Size)
Max Instances 254
Default Auto-Commit 1000 ms
Default Lockdown Duration 5 seconds
Default Max Commits 5 before lockdown

---

## Requirements

· ESP32 (all variants: D0WD, D0WDQ6, S3, C3, etc.)
· Arduino ESP32 core (tested with v2.0.0+)
· C++11 or later

---

## Limitations

· Data types must be trivially copyable (POD types). For complex objects, use putString() or serialize manually.
· Not thread-safe for interrupt context (use with care in RTOS tasks).
· Max 254 simultaneous instances.
· String maximum size limited by available NVS blob space (~4000 bytes).

---

## Migration from Preferences

Preferences NeuNVS
preferences.putInt("key", 10) NeuNVS.put(1, 10)
preferences.getInt("key", 0) NeuNVS.get<int>(1, 0)
preferences.putString("str", "hi") NeuNVS.putString(1, "hi")
preferences.clear() NeuNVS.clearAll()

---

## License

Distributed under the MIT License. See LICENSE file for more information.

---

## Author

### ulywae @ Neu

---

## Contributing

Issues and pull requests are welcome! For major changes, please open an issue first to discuss.

---

Star History

If you find this library useful, please give it a on GitHub!

```

---
