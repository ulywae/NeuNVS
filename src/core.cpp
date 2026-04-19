// ================= CORE =================

#include "NeuNVS.h"

// ========= Core write (FIXED: migrasi sebelum threshold) =========
bool NeuNVS::_write(uint8_t id, const uint8_t *data, size_t len)
{
    if (!_h)
    {
        _err(NeuNVS_Error::SystemFail, id);
        return false;
    }
    if (id >= NeuNVSConfig::MAX_IDS)
    {
        _err(NeuNVS_Error::InvalidID, id);
        return false;
    }

    uint32_t now = millis();

    // Global lock check
    if ((int32_t)(now - _lockUntil) < 0)
    {
        _err(NeuNVS_Error::Lock, id);
        return false;
    }

    // Jika logical ID kosong, alokasikan slot fisik baru
    if (_map[id] == 255)
    {
        int16_t newPhys = _allocateSlot();
        if (newPhys == -1)
        {
            _err(NeuNVS_Error::SystemFail, id);
            return false;
        }
        _map[id] = (uint8_t)newPhys;
        _revMap[newPhys] = id;
        _saveMap();
    }

    uint8_t phys = _map[id];

    // Update heat (pressure)
    uint32_t dt = now - _lastWriteMap[id];
    _lastWriteMap[id] = now;
    uint32_t safe_dt = (dt == 0) ? 1 : dt;
    float pressure = (safe_dt < NeuNVSConfig::FAST_WINDOW) ? (float)NeuNVSConfig::FAST_WINDOW / (float)safe_dt : 1.0f;
    _heatMap[id] += 0.15f * pressure;
    if (_heatMap[id] > NeuNVSConfig::HEAT_MAX)
        _heatMap[id] = NeuNVSConfig::HEAT_MAX;

    // FIX #4: cek migrasi SEBELUM threshold
    if (_heatMap[id] >= (NeuNVSConfig::HEAT_LOCK * 0.85f))
        _migrate(id);

    // Threshold check (bisa jadi setelah migrasi heat masih tinggi)
    if (_heatMap[id] >= _heatThreshold[id])
    {
        _lockUntil = now + _lockMs;
        _err(NeuNVS_Error::Lock, id);
        return false;
    }

    phys = _map[id]; // ambil ulang karena migrasi bisa mengubah mapping
    return _write_raw(phys, data, len);
}

// ========= Core read =========
bool NeuNVS::_read(uint8_t id, uint8_t *out, size_t len)
{
    if (!_h)
    {
        _err(NeuNVS_Error::SystemFail, id);
        return false;
    }
    if (id >= NeuNVSConfig::MAX_IDS)
    {
        _err(NeuNVS_Error::InvalidID, id);
        return false;
    }
    if (_map[id] == 255)
    {
        _err(NeuNVS_Error::NotFound, id);
        return false;
    }
    return _read_raw(_map[id], out, len);
}

// ================= RAW WRITE =================
bool NeuNVS::_write_raw(uint8_t physId, const uint8_t *data, size_t len)
{
    if (!_m)
    {
        _err(NeuNVS_Error::SystemFail, physId);
        return false;
    }
    if (physId >= NeuNVSConfig::PHYS_SLOTS)
    {
        _err(NeuNVS_Error::InvalidID, physId);
        return false;
    }

    if (!xSemaphoreTake(_m, 10))
    {
        _err(NeuNVS_Error::SystemFail, physId);
        return false;
    }

    char key[12];
    snprintf(key, sizeof(key), "k%d", physId);

    Header h;
    h.magic = 0xA5;
    h.size = len;
    h.crc = _crc(data, len);

    memcpy(_buf, &h, sizeof(h));
    memcpy(_buf + sizeof(h), data, len);

    bool ok = (nvs_set_blob(_h, key, _buf, sizeof(h) + len) == ESP_OK);
    if (ok)
        _dirty = true;
    else
        _err(NeuNVS_Error::WriteFail, physId);

    xSemaphoreGive(_m);
    return ok;
}

// ================= RAW READ =================
bool NeuNVS::_read_raw(uint8_t physId, uint8_t *out, size_t len)
{
    if (!_m)
    {
        _err(NeuNVS_Error::SystemFail, physId);
        return false;
    }
    if (physId >= NeuNVSConfig::PHYS_SLOTS)
    {
        _err(NeuNVS_Error::InvalidID, physId);
        return false;
    }

    if (!xSemaphoreTake(_m, 10))
    {
        _err(NeuNVS_Error::SystemFail, physId);
        return false;
    }

    char key[12];
    snprintf(key, sizeof(key), "k%d", physId);

    size_t sz = sizeof(_buf);
    if (nvs_get_blob(_h, key, _buf, &sz) != ESP_OK)
    {
        _err(NeuNVS_Error::NotFound, physId);
        xSemaphoreGive(_m);
        return false;
    }

    Header h;
    memcpy(&h, _buf, sizeof(Header));
    if (h.magic != 0xA5)
    {
        xSemaphoreGive(_m);
        _err(NeuNVS_Error::ReadFail, physId);
        return false;
    }

    size_t dataSize = h.size;
    if (_crc(_buf + sizeof(Header), dataSize) != h.crc)
    {
        xSemaphoreGive(_m);
        _err(NeuNVS_Error::ReadFail, physId);
        return false;
    }

    if (dataSize > len)
    {
        xSemaphoreGive(_m);
        _err(NeuNVS_Error::TooLarge, physId);
        return false;
    }

    memcpy(out, _buf + sizeof(Header), dataSize);
    xSemaphoreGive(_m);
    return true;
}

// ========= Commit =========
void NeuNVS::commit()
{
    if (!_h || !_dirty)
        return;
    if (_m && xSemaphoreTake(_m, 50))
    {
        if (nvs_commit(_h) == ESP_OK)
        {
            _dirty = false;
            _lastCommit = millis();
        }
        xSemaphoreGive(_m);
    }
}