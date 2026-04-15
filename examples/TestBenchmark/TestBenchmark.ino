#define NO_GLOBAL_EEPROM

#include <Arduino.h>
#include <NeuNVS.h>
#include <Preferences.h>

NeuNVS neuStore;
Preferences prefs;

struct DeviceConfig {
    char ssid[32];
    char password[64];
    uint32_t ip;
    float threshold;
    bool autoUpdate;
};

// Error callback untuk NeuNVS
void onNeuError(uint8_t code, uint8_t id) {
    Serial.printf("[NeuNVS Error] Code: %d, ID: %d\n", code, id);
    switch(code) {
        case NeuNVS::ERR_LOCKDOWN:
            Serial.println("  -> LOCKDOWN ACTIVE! Write blocked.");
            break;
        case NeuNVS::ERR_DATA_CORRUPT:
            Serial.println("  -> DATA CORRUPTED!");
            break;
        case NeuNVS::ERR_SIZE_MISMATCH:
            Serial.println("  -> SIZE MISMATCH!");
            break;
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n===========================================");
    Serial.println("     NEUNVS vs PREFERENCES BENCHMARK");
    Serial.println("===========================================");

    // Init NeuNVS
    neuStore.onError(onNeuError);
    if (!neuStore.begin(1000, 2)) {
        Serial.println("NeuNVS Init FAILED!");
    } else {
        Serial.println("NeuNVS Init OK");
    }

    // Init Preferences
    prefs.begin("benchmark", false);
    prefs.clear();
    Serial.println("Preferences Init OK\n");

    runBenchmark();
}

void runBenchmark() {
    uint32_t start, end;
    const int iterations = 20;
    const int dirtyCheckIter = 100;

    // ========== TEST 1: INT WRITE ==========
    Serial.println("--- TEST 1: INT Write (20x) ---");
    
    start = micros();
    for (int i = 0; i < iterations; i++) {
        neuStore.put(1, i * 100);
        neuStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++) {
        prefs.putInt("intKey", i * 100);
        prefs.commit();
    }
    end = micros();
    Serial.printf("Prefs    : %lu us\n\n", end - start);

    delay(2000);

    // ========== TEST 2: INT READ ==========
    Serial.println("--- TEST 2: INT Read (20x) ---");
    
    start = micros();
    for (int i = 0; i < iterations; i++) {
        int val;
        neuStore.get(1, val);
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++) {
        prefs.getInt("intKey", 0);
    }
    end = micros();
    Serial.printf("Prefs    : %lu us\n\n", end - start);

    delay(2000);

    // ========== TEST 3: DIRTY CHECK (WRITE SAME DATA) ==========
    Serial.printf("--- TEST 3: Dirty Check (Write same data %dx) ---\n", dirtyCheckIter);
    
    neuStore.put(2, 12345);
    neuStore.commit();
    
    start = micros();
    for (int i = 0; i < dirtyCheckIter; i++) {
        neuStore.put(2, 12345);  // Data tidak berubah
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us (XOR cache skip!)\n", end - start);

    start = micros();
    for (int i = 0; i < dirtyCheckIter; i++) {
        prefs.putInt("dirtyKey", 12345);
        prefs.commit();
    }
    end = micros();
    Serial.printf("Prefs    : %lu us (Always write to flash)\n\n", end - start);

    delay(2000);

    // ========== TEST 4: STRUCT ==========
    Serial.println("--- TEST 4: Struct Write (20x) ---");
    DeviceConfig cfg = {"NeuNVS_WiFi", "SecurePass123", 0xC0A80101, 25.5f, true};
    
    start = micros();
    for (int i = 0; i < iterations; i++) {
        cfg.threshold = 20.0f + i;
        neuStore.put(3, cfg);
        neuStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++) {
        cfg.threshold = 20.0f + i;
        prefs.putBytes("structKey", &cfg, sizeof(cfg));
        prefs.commit();
    }
    end = micros();
    Serial.printf("Prefs    : %lu us\n\n", end - start);

    delay(2000);

    // ========== TEST 5: STRING ==========
    Serial.println("--- TEST 5: String Write (20x) ---");
    String testStr = "NeuNVS_TheUltimateFastStorageLibraryForESP32";
    
    start = micros();
    for (int i = 0; i < iterations; i++) {
        neuStore.putString(4, testStr + String(i));
        neuStore.commit();
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++) {
        prefs.putString("strKey", testStr + String(i));
        prefs.commit();
    }
    end = micros();
    Serial.printf("Prefs    : %lu us\n\n", end - start);

    delay(2000);

    // ========== TEST 6: STRING READ ==========
    Serial.println("--- TEST 6: String Read (20x) ---");
    String dummy;
    
    start = micros();
    for (int i = 0; i < iterations; i++) {
        neuStore.getString(4, dummy);
    }
    end = micros();
    Serial.printf("NeuNVS   : %lu us\n", end - start);

    start = micros();
    for (int i = 0; i < iterations; i++) {
        dummy = prefs.getString("strKey", "");
    }
    end = micros();
    Serial.printf("Prefs    : %lu us\n\n", end - start);

    delay(2000);

    // ========== TEST 7: ABUSE PROTECTION ==========
    Serial.println("--- TEST 7: Abuse Protection (Spam commit) ---");
    Serial.println("Spamming 15x rapid commit...");
    for (int i = 0; i < 15; i++) {
        neuStore.put(5, i);
        bool ok = neuStore.commit();
        Serial.printf("Commit %2d: %s | Heat: %.2f | Locked: %s\n", 
                      i+1, ok ? "OK" : "BLOCKED", 
                      neuStore.getHeat(),
                      neuStore.isLocked() ? "YES" : "no");
        delay(10);
    }
    Serial.println();

    // ========== TEST 8: MEMORY USAGE ==========
    Serial.println("--- TEST 8: Estimated RAM Usage ---");
    Serial.printf("NeuNVS _xorCache: %u bytes (%d x 2)\n", 
                  NeuNVSConstants::MAX_IDS * 2, NeuNVSConstants::MAX_IDS);
    Serial.printf("NeuNVS instance overhead: ~%u bytes\n", sizeof(NeuNVS));
    Serial.printf("Preferences overhead: ~%u bytes (depends on keys)\n\n", sizeof(Preferences));

    // ========== VERIFICATION ==========
    Serial.println("--- VERIFICATION: Data Dump ---");
    neuStore.dump(1);
    neuStore.dump(3);
    neuStore.dump(4);
    
    Serial.printf("\nFree NVS entries: %zu\n", neuStore.getTotalFreeEntries());
    
    Serial.println("\n===========================================");
    Serial.println("             BENCHMARK DONE");
    Serial.println("===========================================");
}

void loop() {
    // update() WAJIB dipanggil untuk pendinginan dan auto-commit
    neuStore.update();
    
    // Optional: tampilkan heat setiap 5 detik
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        Serial.printf("[Monitor] Heat: %.2f | Locked: %s | Confidence: %.2f\n",
                      neuStore.getHeat(),
                      neuStore.isLocked() ? "YES" : "no",
                      neuStore.getConfidence());
        lastPrint = millis();
    }
}
