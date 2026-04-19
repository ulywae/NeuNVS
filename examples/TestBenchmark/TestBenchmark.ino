#define NO_GLOBAL_EEPROM

#include <Arduino.h>
#include <NeuNVS.h>

void runBenchmark()
{
    Serial.println(F("\n=== NeuNVS v1.2 PERFORMANCE TEST ==="));

    struct TestData
    {
        uint32_t val;
        float sensors;
        char msg[12];
    };

    TestData data = {12345, 36.5f, "Performance"};
    uint32_t start, end;

    // 1. WRITING TEST (SINGLE WRITE)
    start = micros();
    bool wOk = neuNVS.put(0, data);
    end = micros();
    Serial.printf("Single Write: %lu us | Status: %s\n", end - start, wOk ? "OK" : "FAIL");

    // 2. READ TEST (SINGLE READ)
    start = micros();
    TestData readBack;
    bool rOk = neuNVS.get(0, readBack);
    end = micros();
    Serial.printf("Single Read : %lu us | Status: %s\n", end - start, rOk ? "OK" : "FAIL");

    // 3. STRESS TEST (Thermal Lockdown Check)
    Serial.println(F("\nRunning Stress Test (Spamming ID 1)..."));
    int blockedAt = -1;
    for (int i = 0; i < 500; i++)
    {
        if (!neuNVS.put(1, data))
        {
            blockedAt = i;
            break;
        }
        neuNVS.update(); // Let the heat be monitored
        delay(1);        // Give me a little breath
    }

    if (blockedAt != -1)
    {
        Serial.printf("Lockdown active at iteration: %d (System Protected!)\n", blockedAt);
    }
    else
    {
        Serial.println(F("Warning: System not locked. Check HEAT_LOCK config."));
    }

    // 4. CHECK FINAL CONDITION
    neuNVS.dump();
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    neuNVS.begin();
    runBenchmark();
}

void loop() {
    neuStore.update();
}
