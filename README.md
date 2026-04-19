# NeuNVS

**High-Performance & Hardware-Protected NVS Library for ESP32**

NeuNVS is a next-generation storage library for the ESP32, designed as a high-performance, intelligent alternative to the standard Preferences and EEPROM libraries.

NeuNVS isn't just a simple wrapper around NVS; it is a Flash Management System built with the instinct to protect your ESP32 hardware from premature death caused by excessive write cycles (flash burnout). It doesn't just store data—it actively guards your hardware using an Intelligent Virtual Heat Meter algorithm. Much like a CPU's thermal protection system, it monitors write pressure and manages your Flash memory's longevity autonomously.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32-blue?logo=espressif&logoColor=white)](https://espressif.com)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-00878F?logo=arduino&logoColor=white)](https://arduino.cc)
[![C++: 11](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://isocpp.org/)

---

## Why Use NeuNVS?

Standard libraries often neglect hardware health and data integrity.

- **Adaptive Wear Leveling (Migration)** The only library that monitors the "temperature" of your data. If one physical slot is overwritten, NeuNVS will automatically move that data to a cooler spare slot without you even realizing it.

- **Smart Thermal Throttling** An exclusive "Heat Meter" feature. The library monitors the intensity of data writes and automatically locks down if it detects activity that could jeopardize Flash lifespan (such as a loop bug)

- **Ultra-Low Latency (Zero-Copy)** Read operations only take about ~600us, because data is directly streamed to your variable without going through unnecessary intermediate buffers.

- **Data Integrity & Thread-Safety** Each data block is protected by CRC16 and a Magic Number. Equipped with a Mutex (Semaphore) for safe use in multi-tasking environments (Dual-Core).

- **System Transparency** Use the dump() function to view the "innards" of your Flash: physical slot positions, heatmaps, and even real-time lockdown status.

---

## Key Features

- **CRC16 Data Integrity:** Every data block (including Strings!) is guarded by a CRC16 checksum and magic-number validation. No more silent data corruption!

- **Hardware Lockdown:** Exclusive "Spam Protection" that automatically locks Flash writes if a loop-bug or excessive commits are detected. It's like thermal throttling for your Flash memory.

- **Active Wear-Leveling:** Intelligent Migration Logic that automatically moves "hot" data to cooler physical slots to ensure even wear across your Flash partition.

- **Auto-Namespace:** Seamlessly handle multiple instances with automatic namespace allocation (`ns0`, `ns1`, ...).

- **Zero-Copy Performance:** High-speed data retrieval with minimal CPU overhead. No unnecessary dynamic memory allocation for POD types.

- **Diagnostic Window:** Built-in dump() feature that gives you a "God View" of your storage health, heatmap, and physical slot mapping.

---

## Installation

### Arduino IDE

1. Download this repository as a `.zip`
2. Go to **Sketch** → **Include Library** → **Add .ZIP Library**
3. Select the downloaded file
4. Or download directly from the library manager on Arduino

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

// 1. Define your data structure (Must be POD/Plain Old Data)
struct UserSettings {
    float targetTemp;
    bool alarmEnabled;
    uint16_t sensorId;
};

void setup() {
    Serial.begin(115200);

    // Initialize with 1s auto-commit interval, 3000ms lockdown duration
    if (!neuNVS.begin(1000, 3000)) {
        Serial.println("Failed to initialize NeuNVS!");
        return;
    }

    // --- Basic Types ---
    uint32_t myData = 1337;
    neuNVS.put(1, myData); // Write ID 1

    uint32_t savedData;
    if (neuNVS.get(1, savedData)) {
        Serial.printf("Saved Data: %u\n", savedData);
    }

    // --- Complex Structs ---
    UserSettings mySettings = {24.5, true, 404};
    neuNVS.put(2, mySettings); // Write ID 2

    UserSettings loadedSettings;
    if (neuNVS.get(2, loadedSettings)) {
        Serial.printf("Temp: %.1f, Alarm: %s\n",
                      loadedSettings.targetTemp,
                      loadedSettings.alarmEnabled ? "ON" : "OFF");
    }

    // --- Strings with XOR protection ---
    neuNVS.putString(10, "Hello NeuNVS!");
    String str = neuNVS.getString(10);
    Serial.println(str);
}

void loop() {
    // MUST be called to process auto-commits and monitor hardware safety
    neuNVS.update();
}
```

---

## Advanced Features

### Error Callbacks

Get notified when hardware issues or data corruption occurs:

```cpp
void handleNeuNVSErrors(NeuNVS_Error e, uint8_t id) {
    Serial.print(F("[NeuNVS Event] "));
    
    switch (e) {
        case NeuNVS_Error::Lock:
            Serial.printf("LOCKDOWN! ID %d is too hot. Write ignored for protection.\n", id);
            break;
        case NeuNVS_Error::InvalidID:
            Serial.printf("ERROR: ID %d is out of range (Check MAX_IDS).\n", id);
            break;
        case NeuNVS_Error::NotFound:
            Serial.printf("INFO: ID %d not found (Normal for first-time access).\n", id);
            break;
        case NeuNVS_Error::ReadFail:
            Serial.printf("CRITICAL: ID %d data is corrupt (CRC Error)!\n", id);
            break;
        case NeuNVS_Error::Migration:
            Serial.printf("SYSTEM: ID %d is migrating to a cooler slot.\n", id);
            break;
        default:
            Serial.printf("Event Code: %d on ID %d\n", (int)e, id);
            break;
    }
}

void setup() {
    neuNVS.begin();
    neuNVS.onError(handleNeuNVSErrors);
}
```

### Manual Commit & Clean

```cpp
neuNVS.put(5, 100);
neuNVS.commit();   // Force immediate write

neuNVS.remove(5);  // Delete specific ID
neuNVS.clear();    // Factory reset entire namespace
```

### Multiple Instances

Each instance is automatically assigned a unique namespace (ns0, ns1, etc.), ensuring no data collisions between different storage objects.

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

> [!IMPORTANT]
>
> ### ID Scope & Range
>
> - **ID Range:** By default, you can use IDs from 0 to 63 (Total 64 IDs).
> - **Flexible Capacity:** Need more? You can easily change MAX_IDS in NeuNVSConfig to suit your needs (up to 254).
> - **Isolated Scope:** This ID range is **local per-instance**.
> - **Zero Conflict:** You can store data under `ID 1` in `configStorage` and different data under `ID 1` in `userStorage`. They won't overwrite each other because they're automatically stored in different namespaces (`ns0`, `ns1`, etc.).

[!TIP]

Why 64 IDs? NeuNVS is designed for high-performance and safety. Each ID consumes a small amount of RAM to track its "Heat" and "Migration" status. 64 IDs is the sweet spot for most IoT projects.

Pro Tip: Use Structs!
Don't waste your IDs by storing single variables (like one ID for age, another for height). Instead, wrap your related data into a struct and store it in one single ID.

```cpp
struct UserSettings {
  int age;
  float height;
  bool isAdmin;
};

UserSettings mySet = {25, 170.5f, true};
neuNVS.put(0, mySet); // Efficient: 3 variables in 1 ID!
```

This approach saves your ID slots and significantly reduces RAM consumption for heat tracking.

### Check Lockdown Status

Writes are temporarily blocked due to excessive write spamming to preserve Flash lifespan.

```cpp
if (neuNVS.isLocked()) {
    Serial.println("System is locked! Waiting for cooldown...");
} else {
    neuNVS.put(99, 123);
}
```

---

### Error Codes

| Code | Name                   | Description                                        |
| :--- | :--------------------- | :------------------------------------------------- |
| 0    | `None	`             | No error. Everything is running normally.                            |
| 1    | `Lock`         | Thermal Protection Active!  |
| 2    | `WriteFail`     | Flash write operation failed at NVS level.         |
| 3    | `ReadFail`      | Failed to read data from flash memory.             |
| 4    | `NotFound`     | Requested ID doesn't exist.                        |
| 5    | `TooLarge`     | Data size exceeds MAX_BLOB (256 bytes).        |
| 6    | `SystemFail`    | Failed NVS initialization or out of RAM |
| 7    | `InvalidID` | ID usage is out of range (Check MAX_IDS or PHYS_SLOTS). |
| 8    | `Migration`     | System Event: NeuNVS is moving your data to a cooler physical slot (Wear Leveling). |

[Pro-Tip]

Use the Migration event to monitor how intelligently this library is protecting your hardware. If you see this event, it means NeuNVS has just automatically extended the lifespan of your ESP32!

---

## API Reference

### Initialization & Core

| Method                       | Description                                                      |
| :--------------------------- | :--------------------------------------------------------------- |
| `begin(interval, lock, max)` | Init NVS with interval (ms), lockdown (ms).    |
| `update()`                   | **Must be called in `loop()`** to process pending auto-commits.  |
| `commit()`                   | Manually trigger a write to Flash (subject to Abuse Protection). |
| `end()`                      | Close NVS handle and cleanup.                                    |

[!CAUTION]

System Maintenance: update()

You must call neuNVS.update() inside your main loop(). This function serves as the system's management engine to:
Process Heat Decay: Gradually reduces "heat" levels to allow future writes.
Lockdown Management: Handles the cooldown period and releases the write lock.
Deferred Commits: Executes pending NVS commits to ensure data persistence and flash efficiency.
Without regular calls to update(), the thermal protection and wear-leveling logic will be disabled.

---

### Data Operations

| Method                         | Description                                                                  |
| :----------------------------- | :--------------------------------------------------------------------------- |
| `put(id, value)`               | Store any **POD** type (int, float, struct, etc.) with automatic XOR header. |
| `get(id, outValue, default)`   | Retrieve data with XOR validation. Returns `true` if successful.             |
| `putString(id, value)`         | Store `String` object with XOR protection.                                   |
| `getString(id, &out, default)` | Retrieve `String` with XOR validation and default value fallback.            |
| `remove(id)`                   | Delete a specific ID and its associated data.                                |
| `clear()`                   | Wipe all data in the current namespace (Factory Reset).                      |
| `commit()`                     | Force immediate write to Flash (subject to Abuse Protection).                |

---

### Status & Debugging

| Method                  | Description                                              |
| :---------------------- | :------------------------------------------------------- |
| `isLocked()`            | Returns `true` if hardware lockdown is currently active. |
| `getHeat(id)`             | Returns the current "temperature" of a specific ID (0.0 to 10.0). Uses soft-saturation logic. |
| `getHeatMax()`        | Returns the highest heat level among all stored IDs. Useful for system health monitoring. |
| `getHeatAvg()` | Returns the average heat level of the entire namespace to monitor overall flash usage.  |
| `dump()`              | The Diagnostic Window. Prints a complete visual map of Logical IDs, Physical Slots, Heatmaps, and Spare Slots to the Serial Monitor.  |

[Pro Tip] for Developers

Integrating getHeatMax() into your dashboard or telemetry is a great way to monitor how "aggressive" your firmware is towards the ESP32 Flash memory in real-world deployments.

---

## Technical Specifications

| Parameter                | Value / Detail                                  |
| :----------------------- | :---------------------------------------------- |
| **Namespace Management** | Automatic Isolation (`ns0` to `ns254`)           |
| **Indexing System**      | O(1) ID-based (`uint8_t`), range `0` - `63` by default |
| **Data Overhead**        | **5 Bytes** per entry (Magic + Size + CRC16) |
| **Default Auto-Commit**  | `200 ms` (Dynamic based on system heat)     |
| **Default Lockdown**     | `3000 ms` (Configurable)   |
| **Storage Engine**       | Native ESP-IDF NVS with Custom Mapping Layer      |

---

## Requirements

· ESP32 (all variants: D0WD, D0WDQ6, S3, C3, etc.)

· Arduino ESP32 core (tested with v2.0.0+)

· C++11 or later

---

## Limitations

· POD Types Only: Data must be Trivially Copyable. Do not store objects with virtual methods, pointers, or dynamic containers (like std::vector) directly inside put().

· String Size: While NVS supports larger blobs, NeuNVS is optimized for efficiency with a default MAX_BLOB of 256 bytes (Configurable).

· Thread Safety: Fully Thread-Safe. Built-in Mutex (Semaphore) protection ensures safe access from multiple FreeRTOS tasks or different CPU cores.

· NVS Partition: Since NeuNVS uses spare slots for wear-leveling, ensure your NVS partition has enough space for PHYS_SLOTS entries.

---

## Migration from Preferences

| Preferences                        | NeuNVS                     |
| :--------------------------------- | :------------------------- |
| preferences.putInt("key", 10)      | neuNVS.put(1, 10)          |
| preferences.getInt("key", 0)       | neuNVS.get(1, 0)           |
| preferences.putString("str", "hi") | neuNVS.putString(10, "hi") |
| preferences.getString("str", "")   | neuNVS.getString(10, "")   |
| preferences.clear()                | neuNVS.clear()          |

---

## License

Distributed under the MIT License. See LICENSE for more information.

---

## Author

Created by Ulywae @ Neu

---

## Contributing

Issues and pull requests are welcome! For major changes, please open an issue first to discuss.

---

### Support the Project 

If this project helps you, please consider giving it a ⭐! 
It helps others find the repository and keeps me motivated to add more features.

---
