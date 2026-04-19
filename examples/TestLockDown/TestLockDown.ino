#include <Arduino.h>
#include <NeuNVS.h>

// Example data structure
struct MyConfig
{
    float temperature;
    int deviceId;
    bool state;
};

void neuNVSErrorHandler(NeuNVS_Error e, uint8_t id)
{
    Serial.println(F("\n[ NeuNVS EVENT DETECTED ]"));
    switch (e)
    {
    case NeuNVS_Error::Lock:
        Serial.printf(">>> ALERT: ID %u is too hot! Lockdown ACTIVE.\n", id);
        break;
    case NeuNVS_Error::ReadFail:
        Serial.printf(">>> ALERT: ID %u Data Corrupt (CRC Mismatch)!\n", id);
        break;
    case NeuNVS_Error::Migration:
        Serial.printf(">>> SYSTEM: ID %u is migrating to a cooler slot.\n", id);
        break;
    default:
        Serial.printf(">>> EVENT: %d on ID %d\n", (int)e, id);
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println(F("\n--- NeuNVS Smart Storage System ---"));

    // Initialize NVS
    if (neuNVS.begin())
    {
        Serial.println(F("NVS Initialized!"));
    }
    else
    {
        Serial.println(F("NVS Failed!"));
        while (1)
            ;
    }

    // Register security guard error
    neuNVS.onError(neuNVSErrorHandler);

    // Load initial data or set defaults
    uint32_t myNumber;
    if (!neuNVS.get(1, myNumber))
    {
        myNumber = 123456;
        neuNVS.put(1, myNumber);
    }

    MyConfig cfg;
    if (!neuNVS.get(2, cfg))
    {
        cfg = {25.5, 99, true};
        neuNVS.put(2, cfg);
    }
}

void loop()
{
    // 1. UPDATE (MANDATORY): Managing Heat Decay & Auto-Commit
    // The library will automatically commit to Flash if:
    // - There is new data (Dirty)
    // - The COMMIT_MS time interval has been met
    // - The system is not in Lockdown
    neuNVS.update();

    // 2. WRITE SIMULATION (Change value every 10 seconds)
    static uint32_t lastChange = 0;
    if (millis() - lastChange > 10000)
    {
        lastChange = millis();
        uint32_t newVal = random(1000, 9000);

        Serial.printf("\nUpdating ID 1 to: %u (Pending Auto-Commit)\n", newVal);
        neuNVS.put(1, newVal);
        // We don't need to call commit() manually, let update() take care of it!
    }

    // 3. MONITORING & DUMP (Every 5 seconds)
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000)
    {
        lastPrint = millis();

        uint32_t readVal;
        MyConfig readCfg;
        neuNVS.get(1, readVal);
        neuNVS.get(2, readCfg);

        Serial.println(F("\n--- System Status ---"));
        Serial.printf("ID 1: %u | ID 2 Temp: %.2f\n", readVal, readCfg.temperature);
        Serial.printf("Current Heat Max: %.2f\n", neuNVS.getHeatMax());

        if (neuNVS.isLocked())
            Serial.println(F("STATUS: [LOCKED] Protection Active!"));
    }

    // 4. ABUSE TEST (Send 'L' to torture Flash)
    if (Serial.available() > 0)
    {
        char cmd = Serial.read();
        if (cmd == 'L')
        {
            Serial.println(F("\n!!! TRIGGERING BURST WRITE (ABUSE TEST) !!!"));
            for (int i = 0; i < 20; i++)
            {
                uint32_t val = i;
                neuNVS.put(1, val);
                // Here we force a manual commit to trigger a quick Lockdown
                neuNVS.commit();
                Serial.print(".");
                delay(10);
            }
            Serial.println();
        }
        else if (cmd == 'D')
        {
            neuNVS.dump(); // View the mapping and heatmap internals
        }
    }
}
