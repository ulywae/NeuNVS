#include "NeuNVS.h"

// ========= CRC16 sederhana =========
uint16_t NeuNVS::_crc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xAA55;
    for (size_t i = 0; i < len; i++)
    {
        crc = (crc ^ data[i]) + (crc << 1);
    }
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

// ================= CONTROL =================
bool NeuNVS::remove(uint8_t id)
{
    if (id >= NeuNVSConfig::MAX_IDS)
        return false;

    char key[12];
    uint8_t phys = _map[id];
    snprintf(key, sizeof(key), "k%d", phys);

    if (!xSemaphoreTake(_m, 10))
        return false;

    bool ok = (nvs_erase_key(_h, key) == ESP_OK);
    if (ok)
    {
        _revMap[phys] = 255; // Tandai slot fisik ini kosong & siap buat migrasi
        _heatMap[id] = 0.0f; // Reset panas ID tersebut
    }

    xSemaphoreGive(_m);
    return ok;
}

bool NeuNVS::clear()
{
    if (!xSemaphoreTake(_m, 10))
        return false;

    // Hapus total semua key di namespace ini
    bool ok = (nvs_erase_all(_h) == ESP_OK);

    if (ok)
    {
        // RESET TOTAL variabel RAM ke kondisi pabrik
        for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        {
            _map[i] = i;
            _heatMap[i] = 0.0f;
            _heatVelocity[i] = 0.0f;
        }
        for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
        {
            _revMap[i] = (i < NeuNVSConfig::MAX_IDS) ? i : 255;
            _slotHeat[i] = 0.0f;
        }
        _dirty = false;
        nvs_commit(_h); // Pastikan erase-all langsung permanen
    }

    xSemaphoreGive(_m);
    return ok;
}

// ================= INFO =================
bool NeuNVS::isLocked() const
{
    return millis() < _lockUntil;
}

float NeuNVS::getHeat(uint8_t id) const
{
    if (id >= NeuNVSConfig::MAX_IDS)
        return 0.0f;

    float h = _heatMap[id];

    // compress curve (soft saturation)
    h = 10.0f * (1.0f - expf(-h / 3.0f));

    return h;
}

float NeuNVS::getHeatMax() const
{
    float maxH = 0.0f;

    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        if (_heatMap[i] > maxH)
            maxH = _heatMap[i];
    }

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
    Serial.printf("Namespace: %s | Lock: %s\n", _ns, _lockUntil > millis() ? "ACTIVE" : "OFF");
    Serial.printf("Commit Interval: %d ms | Last: %d ms ago\n", _commitMs, millis() - _lastCommit);

    Serial.println(F("\n[ID] -> [Phys] | Heat  | Status"));
    Serial.println(F("-------------------------------"));

    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        uint8_t phys = _map[i];
        float h = _heatMap[i];

        Serial.printf("%02d   ->   %02d   | %.2f  | ", i, phys, h);

        // Visualisasi suhu sederhana
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
