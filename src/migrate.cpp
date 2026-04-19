#include "NeuNVS.h"

// ================= MIGRATION =================
void NeuNVS::_migrate(uint8_t logicalId)
{
    uint8_t oldPhys = _map[logicalId];
    int16_t newPhys = -1;
    float bestHeat = 999.0f;

    // 1. CARI SLOT PALING DINGIN
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
        return;

    // 2. PINDAH DATA (Gunakan buffer lokal biar gak tabrakan sama _buf di _write_raw)
    uint8_t temp[NeuNVSConfig::MAX_BLOB];

    // Ambil header dulu untuk tahu sizenya
    // (Atau pakai _read_raw tapi simpan datanya ke 'temp')
    char key[12];
    snprintf(key, sizeof(key), "k%d", oldPhys);
    size_t sz = sizeof(_buf);

    // Kita baca manual di sini biar gak ngganggu _buf global saat _write_raw nanti
    if (nvs_get_blob(_h, key, _buf, &sz) != ESP_OK)
        return;

    Header h;
    memcpy(&h, _buf, sizeof(Header));
    // Salin datanya ke 'temp' lokal
    memcpy(temp, _buf + sizeof(Header), h.size);

    // 3. TULIS KE SLOT BARU
    // Sekarang aman panggil _write_raw karena datanya sudah di 'temp'
    if (!_write_raw((uint8_t)newPhys, temp, h.size))
        return;

    // 4. UPDATE MAPPING
    _revMap[oldPhys] = 255;
    _map[logicalId] = (uint8_t)newPhys;
    _revMap[newPhys] = logicalId;

    _saveMap(); // Pastikan mapping tersimpan

    // 5. COOL DOWN
    _slotHeat[oldPhys] *= 0.3f;

    // Beri event kalau Master mau pasang callback buat monitor pindahan
    _err(NeuNVS_Error::Migration, logicalId);
}

bool NeuNVS::_saveMap()
{
    if (!_h || !_m)
        return false;
    // We store the _map array as a system blob.
    // Use a specific key that won't conflict with k0, k1, etc.
    if (xSemaphoreTake(_m, 20))
    {
        esp_err_t err = nvs_set_blob(_h, "sys_map", _map, sizeof(_map));
        if (err == ESP_OK)
        {
            nvs_commit(_h); // Commit immediately because this is crucial data
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

    // Reset revMap to empty state
    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
        _revMap[i] = 255;

    if (err == ESP_OK)
    {
        for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        {
            if (_map[i] < NeuNVSConfig::PHYS_SLOTS)
                _revMap[_map[i]] = i;
        }
        return true;
    }
    else
    {
        // If it doesn't exist (first time running), leave default mapping (linear)
        for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
        {
            _map[i] = i;
            _revMap[i] = i;
        }
        return _saveMap(); // Save initial mapping
    }
}
