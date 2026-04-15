#define NO_GLOBAL_EEPROM

#include <Arduino.h>
#include <Preferences.h>
#include <NeuNVS.h>

NeuNVS secureStore;
Preferences prefs;

struct DeviceConfig
{
    char ssid[32];
    char password[64];
    uint32_t ip;
    float threshold;
    bool autoUpdate;
};

void setup()
{
    Serial.begin(115200);
    delay(2000);

    runBenchmark();
}

void runBenchmark()
{
    uint32_t start, end;
    const int iterations = 100;

    Serial.println("\n=== START BENCHMARK ===");

    // =========================================================
    // INIT (NO LOCKDOWN FOR PURE PERFORMANCE TEST)
    // =========================================================
    secureStore.begin(0, 0, 255); // disable interval & lockdown
    prefs.begin("bench", false);

    int sameData = 999;
    secureStore.put(2, sameData);
    secureStore.commit();

    // =========================================================
    // 1. WRITE PERFORMANCE (INT)
    // =========================================================
    Serial.println("\n--- WRITE TEST (INT) ---");

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        secureStore.commit();
    }
    end = micros();

    Serial.printf("NeuNVS Write (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i1", (int)random(0, 10000));
    }
    end = micros();

    Serial.printf("Preferences Write (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    // =========================================================
    // 2. DIRTY CHECK TEST
    // =========================================================
    Serial.println("\n--- DIRTY CHECK TEST ---");

    prefs.putInt("i2", sameData);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(2, sameData);
    }
    end = micros();

    Serial.printf("NeuNVS Dirty Skip (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i2", sameData);
    }
    end = micros();

    Serial.printf("Preferences No Check (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    // =========================================================
    // RE-INIT FOR LOCKDOWN TEST
    // =========================================================
    Serial.println("\n--- REINIT FOR LOCKDOWN TEST ---");

    secureStore.end();
    secureStore.begin(1000, 5, 5); // enable protection

    // =========================================================
    // 3. ABUSE PROTECTION TEST
    // =========================================================
    Serial.println("\n--- ABUSE PROTECTION TEST ---");

    for (int i = 0; i < 10; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        bool success = secureStore.commit();

        Serial.printf("Commit %d: %s\n",
                      i + 1,
                      success ? "SUCCESS" : "LOCKED");
    }

    Serial.println("\nWaiting 6s for cooldown...");
    delay(6000);

    // =========================================================
    // 4. STRUCT PERFORMANCE TEST
    // =========================================================
    Serial.println("\n--- STRUCT TEST ---");

    DeviceConfig cfg = {
        "Kantor_Pusat_WiFi",
        "RahasiaNegara123",
        0xC0A80101,
        25.5f,
        true};

    Serial.printf("Struct size: %d bytes\n", sizeof(DeviceConfig));

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        cfg.threshold = (float)random(0, 100) / 1.5f;
        secureStore.put(3, cfg);
        secureStore.commit();
    }
    end = micros();

    Serial.printf("NeuNVS Struct (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        cfg.threshold = (float)random(0, 100) / 1.5f;
        prefs.putBytes("cfg", &cfg, sizeof(DeviceConfig));
    }
    end = micros();

    Serial.printf("Preferences Bytes (%d ops): %lu us | %.2f us/op\n",
                  iterations, end - start, (end - start) / (float)iterations);

    // =========================================================
    // FINAL DUMP
    // =========================================================
    Serial.println("\n--- FINAL DATA DUMP ---");

    secureStore.dump(1);
    secureStore.dump(2);
    secureStore.dump(3);

    Serial.printf("\nFree NVS Entries: %d\n",
                  (int)secureStore.getTotalFreeEntries());

    Serial.println("\n=== BENCHMARK FINISHED ===");
}

void loop() {}
