#line 1 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\makeLibESP.ino"
// #define NO_GLOBAL_EEPROM

// #include <Arduino.h>
// #include "SecureEEPROM.h"

// SecureEEPROM storage;

// // Struktur data contoh untuk disimpan
// struct MyConfig
// {
//     float temperature;
//     int deviceId;
//     bool status;
// };

// void eepromErrorHandler(uint8_t code, uint8_t id)
// {
//     Serial.println("\n[ CALLBACK DETECTED ]");

//     switch (code)
//     {
//     case SecureEEPROM::ERR_LOCKDOWN:
//         Serial.println(">>> ALERT: Sistem Lockdown! Berhenti menulis sementara.");
//         // Bisa tambahkan: digitalWrite(LED_PIN, HIGH);
//         break;

//     case SecureEEPROM::ERR_WRITE_FAILED:
//         Serial.println(">>> ALERT: Gagal menulis ke Hardware Flash!");
//         break;

//     case SecureEEPROM::ERR_DATA_CORRUPT:
//         Serial.printf(">>> ALERT: Data ID %u Korup (XOR Mismatch)!\n", id);
//         break;

//     case SecureEEPROM::ERR_ID_NOT_FOUND:
//         Serial.printf(">>> ALERT: Ukuran data ID %u tidak cocok/Tipe salah!\n", id);
//         break;
//     }
// }

// void setup()
// {
//     Serial.begin(115200);
//     delay(1000);
//     Serial.println("\n--- Testing SecureEEPROM ---");

//     // Inisialisasi: Interval commit 2 detik, Lockdown 10 detik
//     if (storage.begin(2000, 10))
//         Serial.println("NVS Initialized!");
//     else
//         Serial.println("NVS Failed!");

//     storage.onError(eepromErrorHandler);

//     // 1. Simpan data tipe dasar (ID 1)
//     uint32_t myNumber = 123456;
//     storage.put(1, myNumber);
//     storage.commit(); // Paksa tulis pertama kali

//     // 2. Simpan data tipe Struct (ID 2)
//     MyConfig cfg = {25.5, 99, true};
//     storage.put(2, cfg);
//     storage.commit();

//     Serial.println("Data Initialized.");
// }

// void loop()
// {
//     // --- MENGUJI DIRTY CHECK & UPDATE ---
//     // Jika nilai tetap 123456, storage.update() TIDAK akan menulis ke Flash
//     uint32_t val = 123456;
//     storage.put(1, val);
//     storage.update();

//     // --- MENGUJI PEMBACAAN ---
//     static uint32_t lastPrint = 0;
//     if (millis() - lastPrint > 5000)
//     { // Cetak setiap 5 detik
//         lastPrint = millis();

//         uint32_t readVal = storage.get<uint32_t>(1);
//         MyConfig readCfg = storage.get<MyConfig>(2);

//         Serial.println("\n--- Current Data ---");
//         Serial.printf("ID 1 (uint32): %u\n", readVal);
//         Serial.printf("ID 2 (Struct): Temp: %.2f, ID: %d, Active: %s\n",
//                       readCfg.temperature, readCfg.deviceId, readCfg.status ? "Yes" : "No");

//         if (storage.isLocked())
//             Serial.println("STATUS: [LOCKED] Storage sedang diproteksi!");
//         else
//             Serial.println("STATUS: [READY] Storage normal.");
//     }

//     // --- MENGUJI LOCKDOWN (Hanya untuk testing) ---
//     // Ketik 'L' di Serial Monitor untuk memicu penulisan paksa berulang-ulang
//     if (Serial.available() > 0)
//     {
//         char cmd = Serial.read();
//         if (cmd == 'L')
//         {
//             Serial.println("\nMemicu penulisan paksa beruntun (Abuse Test)...");
//             for (int i = 0; i < 10; i++)
//             {
//                 uint32_t randomVal = random(100, 999);
//                 storage.put(1, randomVal);
//                 if (storage.commit())
//                     Serial.printf("Commit %d sukses\n", i + 1);
//                 else
//                     Serial.printf("Commit %d GAGAL (Terdeteksi Abuse/Locked)\n", i + 1);
//             }
//         }
//     }
// }

#define NO_GLOBAL_EEPROM

#include <Arduino.h>
#include <Preferences.h>
#include "SecureEEPROM.h"

SecureEEPROM secureStore;
Preferences prefs;

#line 126 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\makeLibESP.ino"
void setup();
#line 136 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\makeLibESP.ino"
void runBenchmark();
#line 201 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\makeLibESP.ino"
void loop();
#line 126 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\makeLibESP.ino"
void setup()
{
    Serial.begin(115200);
    secureStore.begin(1000, 5); // Interval 1 detik, Lockdown 5 detik
    prefs.begin("bench", false);

    delay(2000);
    runBenchmark();
}

void runBenchmark()
{
    uint32_t start, end;
    const int iterations = 100;
    int testData = 12345;

    Serial.println("\n=== START BENCHMARK ===");

    // 1. TEST: KECEPATAN TULIS (WRITE PERFORMANCE)
    // Menulis data baru yang berbeda untuk memaksa penulisan fisik
    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        secureStore.commit(); // Paksa tulis
    }
    end = micros();
    Serial.printf("SecureEEPROM Write (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i1", (int)random(0, 10000));
    }
    end = micros();
    Serial.printf("Preferences Write  (%d ops): %lu us\n", iterations, end - start);

    // 2. TEST: EFISIENSI DIRTY CHECK
    // Menulis data yang SAMA berulang kali
    int sameData = 999;
    secureStore.put(2, sameData);
    secureStore.commit();
    prefs.putInt("i2", sameData);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(2, sameData); // Seharusnya dilewati oleh Dirty Check
    }
    end = micros();
    Serial.printf("\nSecureEEPROM Dirty Check Skip (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i2", sameData); // Preferences tetap memproses internal key-value
    }
    end = micros();
    Serial.printf("Preferences (No internal Dirty Check) (%d ops): %lu us\n", iterations, end - start);

    delay(6000); // Menunggu masa lockdown dari benchmark sebelumnya selesai

    // 3. TEST: PROTEKSI ABUSE (LOCKDOWN)
    Serial.println("\nTesting Abuse Protection (With Dirty Data)...");
    for (int i = 0; i < 10; i++)
    {
        // KUNCI: Kita harus kasih data baru supaya _isDirty jadi true
        // Tanpa ini, fungsi commit() akan langsung return true tanpa cek abuse
        secureStore.put(1, (int)random(0, 10000));

        bool success = secureStore.commit();
        Serial.printf("SecureEEPROM Commit %d: %s\n", i + 1, success ? "SUCCESS" : "LOCKED/FAILED");
    }
}

void loop() {}

