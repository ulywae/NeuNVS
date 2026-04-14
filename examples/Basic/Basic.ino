#include <NeuNVS.h>

void setup()
{
    Serial.begin(115200);

    // Initialize with default 1s auto-commit interval and 5s lockdown duration
    NVS.begin();

    // Write data (ID-based, no strings needed!)
    uint32_t myData = 1337;
    NVS.put(1, myData);

    // Read data safely
    uint32_t savedData = NVS.get<uint32_t>(1);
    Serial.printf("Saved Data: %u\n", savedData);
}

void loop()
{
    // Call update() to process auto-commits safely
    NVS.update();
}