// NeuNVS - Wear Leveling NVS Manager for ESP32

#include "NeuNVS.h"

// ================= MIGRATION (FIXED) =================
void NeuNVS::_migrate(uint8_t logicalId)
{
    if (!_m || !xSemaphoreTake(_m, 20))
        return;

    uint8_t oldPhys = _map[logicalId];
    int16_t newPhys = -1;
    float bestHeat = 999.0f;

    // 1. Cari slot kosong paling dingin
    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
    {
        if (_revMap[i] == 255)
        {
            if (_slotHeat[i] < bestHeat)
            {
                bestHeat = _slotHeat[i];
                newPhys = i;
            }
        }
    }

    if (newPhys == -1 || newPhys == oldPhys)
    {
        xSemaphoreGive(_m);
        return;
    }

    // 2. Baca data lama dengan buffer LOKAL (bukan _buf global)
    char key[12];
    snprintf(key, sizeof(key), "k%d", oldPhys);
    size_t sz = NeuNVSConfig::MAX_BLOB + sizeof(Header);
    uint8_t localBuf[NeuNVSConfig::MAX_BLOB + sizeof(Header) + 4];
    esp_err_t err = nvs_get_blob(_h, key, localBuf, &sz);
    if (err != ESP_OK)
    {
        xSemaphoreGive(_m);
        return;
    }

    Header h;
    memcpy(&h, localBuf, sizeof(Header));
    if (h.magic != 0xA5 || h.size > NeuNVSConfig::MAX_BLOB)
    {
        xSemaphoreGive(_m);
        return;
    }

    uint8_t temp[NeuNVSConfig::MAX_BLOB];
    memcpy(temp, localBuf + sizeof(Header), h.size);

    // 3. Tulis ke slot baru (langsung pakai nvs_set_blob, karena kita pegang mutex)
    char newKey[12];
    snprintf(newKey, sizeof(newKey), "k%d", newPhys);
    uint8_t outBuf[sizeof(Header) + h.size];
    Header newH = {0xA5, (uint16_t)h.size, _crc(temp, h.size)};
    memcpy(outBuf, &newH, sizeof(Header));
    memcpy(outBuf + sizeof(Header), temp, h.size);

    if (nvs_set_blob(_h, newKey, outBuf, sizeof(Header) + h.size) != ESP_OK)
    {
        xSemaphoreGive(_m);
        return;
    }

    // Hapus data lama (optional)
    nvs_erase_key(_h, key);

    // 4. Update mapping
    _revMap[oldPhys] = 255;
    _map[logicalId] = (uint8_t)newPhys;
    _revMap[newPhys] = logicalId;

    // Perbaikan #3: sinkronkan heat slot baru
    _slotHeat[newPhys] = _heatMap[logicalId];

    _saveMap();

    // 5. Cool down slot lama
    _slotHeat[oldPhys] *= 0.3f;

    _err(NeuNVS_Error::Migration, logicalId);

    xSemaphoreGive(_m);
}

// ================= ALLOCATE SLOT =================
int16_t NeuNVS::_allocateSlot()
{
    int16_t bestSlot = -1;
    float bestHeat = 999.0f;
    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
    {
        if (_revMap[i] == 255)
        {
            if (_slotHeat[i] < bestHeat)
            {
                bestHeat = _slotHeat[i];
                bestSlot = i;
            }
        }
    }
    return bestSlot;
}

// ================= MAP SAVE/LOAD =================
bool NeuNVS::_saveMap()
{
    if (!_h || !_m)
        return false;
    if (xSemaphoreTake(_m, 20))
    {
        esp_err_t err = nvs_set_blob(_h, "sys_map", _map, sizeof(_map));
        if (err == ESP_OK)
        {
            nvs_commit(_h);
            xSemaphoreGive(_m);
            return true;
        }
        xSemaphoreGive(_m);
    }
    return false;
}

bool NeuNVS::_loadMap()
{
    size_t sz = sizeof(_map);
    esp_err_t err = nvs_get_blob(_h, "sys_map", _map, &sz);

    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
        _revMap[i] = 255;

    if (err == ESP_OK)
    {
        for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        {
            if (_map[i] < NeuNVSConfig::PHYS_SLOTS)
                _revMap[_map[i]] = i;
            else if (_map[i] == 255)
                continue; // logical ID kosong
            else
            {
                // nilai tidak valid, reset ke default linear
                _map[i] = i;
                _revMap[i] = i;
            }
        }
        return true;
    }
    else
    {
        // first time: mapping linear
        for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        {
            _map[i] = i;
            _revMap[i] = i;
        }
        for (uint8_t i = NeuNVSConfig::MAX_IDS; i < NeuNVSConfig::PHYS_SLOTS; i++)
            _revMap[i] = 255;
        return _saveMap();
    }
}