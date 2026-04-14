#include <NeuNVS.h>

void setup()
{
    Serial.begin(115200);
    delay(1000); // Wait for Serial to be ready

    // Initialize: default 1s interval, 5s lockdown, max 5 commits
    if (!neuNVS.begin())
    {
        Serial.println("NeuNVS Init Failed!");
        return;
    }

    // --- WRITE DATA ---
    // Using ID (1) to save uint32_t data
    uint32_t myData = 1337;
    neuNVS.put(1, myData);

    // Manual commit to ensure data is saved during setup
    if (neuNVS.commit())
    {
        Serial.println("Data saved successfully!");
    }

    // --- READ DATA ---
    uint32_t savedData;
    // get() returns a boolean for XOR validation
    if (neuNVS.get(1, savedData))
    {
        Serial.printf("Read Data from ID 1: %u\n", savedData);
    }
    else
    {
        Serial.println("Data corrupted or not found!");
    }
}

void loop()
{
    // REQUIRED: Call update() to keep the auto-commit interval running
    neuNVS.update();

    // Example of writing periodic data (Auto-commit will handle the interval)
    static uint32_t lastMillis = 0;
    if (millis() - lastMillis > 10000)
    { // Update every 10 seconds
        uint32_t uptime = millis() / 1000;
        neuNVS.put(2, uptime);
        lastMillis = millis();
        Serial.println("Uptime updated in NVS (pending auto-commit)...");
    }
}