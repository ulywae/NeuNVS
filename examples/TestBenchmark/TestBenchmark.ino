#define NO_GLOBAL_EEPROM // Avoiding global instantiation

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
    // Interval 1 detik, Lockdown 5 detik, Max Commit 5x
    secureStore.begin(1000, 5, 5);
    prefs.begin("bench", false);

    delay(2000);
    runBenchmark();
}

/**
  * Note: Preferences internally handles commits differently depending on usage. 
  * This benchmark reflects typical usage patterns in Arduino-based applications.
  */
void runBenchmark()
{
    uint32_t start, end;
    const int iterations = 100;

    Serial.println("\n=== START BENCHMARK ===");

    int sameData = 999;
    secureStore.put(2, sameData);
    secureStore.commit();

    // 1. TEST: WRITE PERFORMANCE (INT)
    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        secureStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS Write (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i1", (int)random(0, 10000));
    }
    end = micros();
    Serial.printf("Preferences Write (%d ops): %lu us\n", iterations, end - start);

    // 2. TEST: DIRTY CHECK EFFICIENCY
    Serial.println("\nTesting Dirty Check...");
    prefs.putInt("i2", sameData);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(2, sameData);
    }
    end = micros();
    Serial.printf("\nNeuNVS Dirty Check Skip (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i2", sameData);
    }
    end = micros();
    Serial.printf("Preferences (No Dirty Check) (%d ops): %lu us\n", iterations, end - start);

    delay(6000); // Wait for the lockdown from the first test to finish

    // 3. TEST: ABUSE PROTECTION (LOCKDOWN)
    Serial.println("\nTesting Abuse Protection...");
    for (int i = 0; i < 10; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        bool success = secureStore.commit();
        Serial.printf("Commit %d: %s\n", i + 1, success ? "SUCCESS" : "LOCKED/FAILED");
    }

    // --- IMPORTANT: Wait for the abuse test lockdown to complete before the struct test can enter ---
    Serial.println("\nWaiting 6s for lockdown to lift...");
    delay(6000);

    // 4. TEST: LARGE DATA (STRUCT) PERFORMANCE
    Serial.println("=== LARGE DATA (STRUCT) TEST ===");
    DeviceConfig myConfig = {"Kantor_Pusat_WiFi", "RahasiaNegara123", 0xC0A80101, 25.5f, true};

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        myConfig.threshold = (float)random(0, 100) / 1.5f;
        secureStore.put(3, myConfig);
        secureStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS Struct (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        myConfig.threshold = (float)random(0, 100) / 1.5f;
        prefs.putBytes("cfg", &myConfig, sizeof(DeviceConfig));
    }
    end = micros();
    Serial.printf("Preferences Bytes (%d ops): %lu us\n", iterations, end - start);

    // 5. DATA DUMP & VERIFICATION
    Serial.println("\n=== FINAL DATA DUMP ===");
    secureStore.dump(1);
    secureStore.dump(2);
    secureStore.dump(3);

    Serial.printf("\nFree NVS Entries: %d\n", (int)secureStore.getTotalFreeEntries());
    Serial.println("=== BENCHMARK FINISHED ===");
}

void loop() {}
