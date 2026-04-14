#define NO_GLOBAL_EEPROM

#include <Arduino.h>
#include <NeuNVS.h>

NeuNVS storage;

// Example data structure to store
struct MyConfig
{
    float temperature;
    int deviceId;
    bool status;
};

void eepromErrorHandler(uint8_t code, uint8_t id)
{
    Serial.println("\n[ CALLBACK DETECTED ]");

    switch (code)
    {
    case NeuNVS::ERR_LOCKDOWN:
        Serial.println(">>> ALERT: System Lockdown! Stop writing temporarily.");
        // Can add: digitalWrite(LED_PIN, HIGH);
        break;

    case NeuNVS::ERR_WRITE_FAILED:
        Serial.println(">>> ALERT: Failed to write to Hardware Flash!");
        break;

    case NeuNVS::ERR_DATA_CORRUPT:
        Serial.printf(">>> ALERT: %u ID Data Corrupt (XOR Mismatch)!\n", id);
        break;

    case NeuNVS::ERR_ID_NOT_FOUND:
        Serial.printf(">>> ALERT: ID %u data size does not match/Wrong type!\n", id);
        break;
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Testing NeuNVS ---");

    // Initialization: Commit interval 2 seconds, Lockdown 10 seconds
    if (storage.begin(2000, 10))
        Serial.println("NVS Initialized!");
    else
        Serial.println("NVS Failed!");

    storage.onError(eepromErrorHandler);

    // 1. Save basic type data (ID 1)
    uint32_t myNumber = 123456;
    storage.put(1, myNumber);
    storage.commit(); // Force first write

    // 2. Save data type Struct (ID 2)
    MyConfig cfg = {25.5, 99, true};
    storage.put(2, cfg);
    storage.commit();

    Serial.println("Data Initialized.");
}

void loop()
{
    // --- TESTING DIRTY CHECK & UPDATE ---
    // If the value remains 123456, storage.update() will NOT write to Flash
    uint32_t val = 123456;
    storage.put(1, val);
    storage.update();

    // --- TESTING READING ---
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 5000)
    { // Print every 5 seconds
        lastPrint = millis();

        uint32_t readVal;
        MyConfig readCfg;

        storage.get(1, readVal);
        storage.get(2, readCfg);

        Serial.println("\n--- Current Data ---");
        Serial.printf("ID 1 (uint32): %u\n", readVal);
        Serial.printf("ID 2 (Struct): Temp: %.2f, ID: %d, Active: %s\n",
                      readCfg.temperature, readCfg.deviceId, readCfg.status ? "Yes" : "No");

        if (storage.isLocked())
            Serial.println("STATUS: [LOCKED] Storage is being protected!");
        else
            Serial.println("STATUS: [READY] Storage is normal.");
    }

    // --- TESTING LOCKDOWN (For testing only) ---
    // Type 'L' in the Serial Monitor to trigger a forced write loop
    if (Serial.available() > 0)
    {
        char cmd = Serial.read();
        if (cmd == 'L')
        {
            Serial.println("\nTriggering forced write burst (Abuse Test)...");
            for (int i = 0; i < 10; i++)
            {
                uint32_t randomVal = random(100, 999);
                storage.put(1, randomVal);
                if (storage.commit())
                    Serial.printf("Commit %d success\n", i + 1);
                else
                    Serial.printf("Commit %d FAILED (Detected Abuse/Locked)\n", i + 1);
            }
        }
    }
}