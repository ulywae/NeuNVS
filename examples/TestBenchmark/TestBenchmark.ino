#define NO_GLOBAL_EEPROM // Avoiding global instantiation

#include <Arduino.h>
#include <NeuNVS.h>
#include <Preferences.h>

// Objek Testing
NeuNVS secureStore;
Preferences prefs;

// 1. Struct Data for Complicated Tests
struct DeviceConfig
{
    char ssid[32];
    char password[64];
    uint32_t ip;
    float threshold;
    bool autoUpdate;
};

// Benchmark Helper Function
void runBenchmark()
{
    uint32_t start, end;
    const int iterations = 15;

    Serial.println(F("\n==========================================="));
    Serial.println(F("       NEUNVS ULTIMATE TEST SUITE        "));
    Serial.println(F("==========================================="));

    // --- TEST 1: SINGLE DATA (INT) ---
    Serial.println(F("\n[1] Testing Single Data (Int) - Raw Write"));
    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(1, (int)random(0, 32767));
        secureStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS Write   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i1", (int)random(0, 32767));
    }
    end = micros();
    Serial.printf("Preferences    : %lu us\n", end - start);

    // --- COOLING DOWN ---
    Serial.println(F("\nCooling down for 3 seconds..."));
    delay(3000);

    // --- TEST 2: THE 4US DIRTY CHECK (THE CHAMPION) ---
    Serial.println(F("\n[2] Testing Dirty Check (Same Data Skip)"));
    int stableVal = 8888;
    secureStore.put(2, stableVal);
    secureStore.commit();

    start = micros();
    for (int i = 0; i < 50; i++)
    { // Test 50 times bypass
        secureStore.put(2, stableVal);
    }
    end = micros();
    Serial.printf("NeuNVS (XOR-Cache) : %lu us (Super Fast!)\n", end - start);

    // --- COOLING DOWN ---
    Serial.println(F("\nCooling down for 3 seconds..."));
    delay(3000);

    // --- TEST 3: LARGE DATA STRUCT ---
    Serial.println(F("\n[3] Testing Data Struct (Complex POD)"));
    DeviceConfig cfg = {"WiFi_NeuNVS", "Pass1234", 0xC0A8010A, 45.5f, true};

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        cfg.threshold = (float)random(0, 100);
        secureStore.put(3, cfg);
        secureStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS Struct  : %lu us\n", end - start);

    // --- COOLING DOWN ---
    Serial.println(F("\nCooling down for 3 seconds..."));
    delay(3000);

    // --- TEST 4: STRING DATA ---
    Serial.println(F("\n[4] Testing String Data (Dynamic)"));
    String testStr = "NeuNVS_is_the_fastest_NVS_Library_for_ESP32";

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.putString(4, testStr);
        secureStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS String  : %lu us\n", end - start);

    // --- COOLING DOWN ---
    Serial.println(F("\nCooling down for 3 seconds..."));
    delay(3000);

    // --- TEST 5: ABUSE PROTECTION (STRESS) ---
    Serial.println(F("\n[5] Testing Abuse Protection (Lockdown Check)"));
    Serial.println(F("Spamming commit..."));
    for (int i = 0; i < 10; i++)
    {
        secureStore.put(5, i);
        bool res = secureStore.commit();
        Serial.printf("Commit %d: %s | Heat: %.2f\n", i + 1, res ? "SUCCESS" : "LOCKED", secureStore.getHeat());
    }

    // --- FINAL: DUMP DATA ---
    Serial.println(F("\n[6] Final Data Audit (Verification)"));
    secureStore.dump(1); // Int
    secureStore.dump(3); // Struct
    secureStore.dump(4); // String

    Serial.printf("Free Entries: %zu\n", secureStore.getTotalFreeEntries());

    Serial.println(F("\n==========================================="));
    Serial.println(F("          BENCHMARK COMPLETED            "));
    Serial.println(F("==========================================="));
}

void setup()
{
    Serial.begin(115200);
    delay(2000);

    // Init NeuNVS: Interval 1s, Lockdown 2s
    if (!secureStore.begin(1000, 2))
    {
        Serial.println("NeuNVS Init Failed!");
    }

    prefs.begin("nvs_test", false);

    runBenchmark();
}

void loop() {}