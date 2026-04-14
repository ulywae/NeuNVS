#define NO_GLOBAL_EEPROM // Avoiding global instantiation

#include <Arduino.h>
#include <Preferences.h>
#include <NeuNVS.h>

NeuNVS secureStore;
Preferences prefs;

void setup()
{
    Serial.begin(115200);
    secureStore.begin(1000, 5); // Interval 1 second, Lockdown 5 seconds
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

    // 1. TEST: WRITE PERFORMANCE
    // Write new, different data to force a physical write
    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(1, (int)random(0, 10000));
        secureStore.commit(); // Forced write
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

    // 2. TEST: DIRTY CHECK EFFICIENCY
    // Writing the SAME data repeatedly
    int sameData = 999;
    secureStore.put(2, sameData);
    secureStore.commit();
    prefs.putInt("i2", sameData);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        secureStore.put(2, sameData); // Should have been passed by Dirty Check
    }
    end = micros();
    Serial.printf("\nSecureEEPROM Dirty Check Skip (%d ops): %lu us\n", iterations, end - start);

    start = micros();
    for (int i = 0; i < iterations; i++)
    {
        prefs.putInt("i2", sameData); // Preferences still processes internal key-values
    }
    end = micros();
    Serial.printf("Preferences (No internal Dirty Check) (%d ops): %lu us\n", iterations, end - start);

    delay(6000); // Waiting for the lockdown period from the previous benchmark to finish

    // 3. TEST: ABUSE PROTECTION (LOCKDOWN)
    Serial.println("\nTesting Abuse Protection (With Dirty Data)...");
    for (int i = 0; i < 10; i++)
    {
        // LOCK: We need to provide new data to make _isDirty true
        // Without this, the commit() function will simply return true without checking for abuse.
        secureStore.put(1, (int)random(0, 10000));

        bool success = secureStore.commit();
        Serial.printf("SecureEEPROM Commit %d: %s\n", i + 1, success ? "SUCCESS" : "LOCKED/FAILED");
    }
}

void loop() {}
