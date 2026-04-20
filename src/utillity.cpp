// NeuNVS - Utility functions

#include "NeuNVS.h"

// ================= INFO =================
bool NeuNVS::isLocked() const
{
    return (int32_t)(millis() - _lockUntil) < 0;
}

float NeuNVS::getHeat(uint8_t id) const
{
    if (id >= NeuNVSConfig::MAX_IDS)
        return 0.0f;
    float h = _heatMap[id];
    h = 10.0f * (1.0f - expf(-h / 3.0f));
    return h;
}

float NeuNVS::getHeatMax() const
{
    float maxH = 0.0f;
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        if (_heatMap[i] > maxH)
            maxH = _heatMap[i];
    return maxH;
}

float NeuNVS::getHeatAvg() const
{
    float sum = 0.0f;
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        sum += _heatMap[i];
    return sum / NeuNVSConfig::MAX_IDS;
}

void NeuNVS::dump()
{
    Serial.println(F("\n=== NeuNVS SYSTEM DUMP ==="));
    Serial.printf("Namespace: %s | Lock: %s\n", _ns, isLocked() ? "ACTIVE" : "OFF");
    Serial.printf("Commit Interval: %d ms | Last: %d ms ago\n", _commitMs, millis() - _lastCommit);
    Serial.println(F("\n[ID] -> [Phys] | Heat  | Status"));
    Serial.println(F("-------------------------------"));
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        if (_map[i] == 255)
        {
            Serial.printf("%02d   ->   --   | ----- | DELETED\n", i);
            continue;
        }
        uint8_t phys = _map[i];
        float h = _heatMap[i];
        Serial.printf("%02d   ->   %02d   | %.2f  | ", i, phys, h);
        if (h >= NeuNVSConfig::HEAT_LOCK)
            Serial.print(F("LOCKED!!!"));
        else if (h >= NeuNVSConfig::HEAT_LOCK * 0.8f)
            Serial.print(F("MIGRATING..."));
        else
            Serial.print(F("OK"));
        Serial.println();
    }
    Serial.println(F("\n[Spare Physical Slots]"));
    bool foundSpare = false;
    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
    {
        if (_revMap[i] == 255)
        {
            Serial.printf("Slot %02d (Heat: %.2f) ", i, _slotHeat[i]);
            foundSpare = true;
        }
    }
    if (!foundSpare)
        Serial.print(F("NONE! (Danger)"));
    Serial.println(F("\n==========================\n"));
}

void NeuNVS::dump(uint8_t id)
{
    if (id >= NeuNVSConfig::MAX_IDS) {
        Serial.printf("[NeuNVS] Error: ID %d is out of bounds!\n", id);
        return;
    }

    Serial.printf("\n--- NeuNVS ID INSPECTOR: %d ---\n", id);
    
    if (_map[id] == 255) {
        Serial.println(F("Status       : EMPTY / DELETED"));
        Serial.println(F("------------------------------"));
        return;
    }

    // 1. Metadata Info
    uint8_t phys = _map[id];
    float currentHeat = _heatMap[id];
    float threshold = _heatThreshold[id];

    Serial.printf("Physical Slot: %d\n", phys);
    Serial.printf("Heat Level   : %.2f / %.2f\n", currentHeat, threshold);
    Serial.print(F("Condition    : "));
    
    if (currentHeat >= threshold) Serial.println(F("LOCKED (Writing Forbidden)"));
    else if (currentHeat >= NeuNVSConfig::HEAT_LOCK * 0.85f) Serial.println(F("MIGRATING (High Pressure)"));
    else Serial.println(F("HEALTHY"));

    // 2. Data Content (Hex Dump)
    uint8_t tempBuf[NeuNVSConfig::MAX_BLOB];
    // We try to read the data to verify CRC integrity
    if (_read(id, tempBuf, NeuNVSConfig::MAX_BLOB)) {
        Serial.print(F("Raw Data(HEX): "));
        // Displaying up to 16 bytes for brevity
        for (size_t i = 0; i < 16; i++) {
            Serial.printf("%02X ", tempBuf[i]);
        }
        Serial.println(F("..."));
    } else {
        Serial.println(F("Data Content : [READ ERROR] - CRC Mismatch or System Fail"));
    }
    Serial.println(F("------------------------------\n"));
}

// ================= CRC16 =================
uint16_t NeuNVS::_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xAA55;
    for (size_t i = 0; i < len; i++)
        crc = (crc ^ data[i]) + (crc << 1);
    return crc;
}

// ================= ERROR =================
void NeuNVS::onError(NeuNVS_ErrorCb cb)
{
    _cb = cb;
}

void NeuNVS::_err(NeuNVS_Error e, uint8_t id)
{
    if (_cb)
        _cb(e, id);
}
