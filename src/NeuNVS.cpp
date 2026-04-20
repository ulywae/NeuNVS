#include "NeuNVS.h"

uint8_t NeuNVS::_inst = 0;

// ========== CONSTRUCTOR ==========
NeuNVS::NeuNVS() : _h(0), _m(nullptr), _valid(true), _dirty(false),
                   _lastCommit(0), _lockUntil(0), _cb(nullptr)
{
    if (_inst >= 255)
    {
        _valid = false;
        return;
    }

    snprintf(_ns, sizeof(_ns), "ns%d", _inst++);

    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
    {
        _revMap[i] = 255;
        _slotHeat[i] = 0.0f;
    }

    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        _heatMap[i] = 0.0f;
        _heatVelocity[i] = 0.0f;
        _lastHeat[i] = 0.0f;
        _heatThreshold[i] = NeuNVSConfig::HEAT_LOCK;
        _lastWriteMap[i] = 0;

        _map[i] = i;
        _revMap[i] = i;
    }
}

NeuNVS::~NeuNVS()
{
    end();
}

// ========= Begin =========
bool NeuNVS::begin(uint32_t commitMs, uint32_t lockMs)
{
    if (!_valid)
        return false;
    if (nvs_flash_init() != ESP_OK)
        return false;
    if (nvs_open(_ns, NVS_READWRITE, &_h) != ESP_OK)
        return false;

    _m = xSemaphoreCreateMutex();
    if (!_m)
        return false;

    _loadMap();

    _lockMs = lockMs;
    _commitMs = commitMs;
    _lastCommit = millis();

    return true;
}

// ========= End =========
void NeuNVS::end()
{
    if (_h)
    {
        if (_dirty && xSemaphoreTake(_m, 10))
        {
            nvs_commit(_h);
            xSemaphoreGive(_m);
        }
        nvs_close(_h);
        _h = 0;
    }

    if (_m)
    {
        vSemaphoreDelete(_m);
        _m = nullptr;
    }
}

// ========== PUT RAW ==========
bool NeuNVS::put(uint8_t id, const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return false;
    if (len > NeuNVSConfig::MAX_BLOB)
    {
        _err(NeuNVS_Error::TooLarge, id);
        return false;
    }
    return _write(id, data, len);
}

bool NeuNVS::get(uint8_t id, uint8_t *out, size_t len)
{
    if (!out || len == 0)
        return false;
    return _read(id, out, len);
}

// ========= Update (FIXED rollover) =========
void NeuNVS::update()
{
    if (!_h)
        return;

    uint32_t now = millis();

    int32_t diff = (int32_t)(now - lastHeatUpdate);
    if (diff < 20)
        return;

    float dt = (float)diff * 0.001f;
    lastHeatUpdate = now;

    float maxHeat = 0.0f;
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        float &h = _heatMap[i];
        h *= (1.0f - dt * NeuNVSConfig::DECAY_RATE);
        if (h < 0.0f)
            h = 0.0f;

        float dv = (h - _lastHeat[i]) / (dt + 0.0001f);
        _heatVelocity[i] = _heatVelocity[i] * 0.8f + dv * 0.2f;
        _lastHeat[i] = h;

        float predicted = h + _heatVelocity[i] * NeuNVSConfig::PREDICT_GAIN;
        _heatThreshold[i] = NeuNVSConfig::HEAT_LOCK - (predicted * NeuNVSConfig::THRESHOLD_K);

        if (_map[i] != 255)
            _slotHeat[_map[i]] = h;

        if (h > maxHeat)
            maxHeat = h;
    }

    // Lockdown management
    if (_lockUntil && (int32_t)(now - _lockUntil) >= 0)
        _lockUntil = 0;

    // Smart commit
    if (_dirty && !_lockUntil)
    {
        uint32_t dynamicInterval = _commitMs + (uint32_t)(maxHeat * 100.0f);
        if ((int32_t)(now - _lastCommit) > (int32_t)dynamicInterval)
            commit();
    }
}

// ================= STRING API =================
void NeuNVS::putString(uint8_t id, const String &v)
{
    size_t len = v.length() + 1;
    if (len > NeuNVSConfig::MAX_BLOB)
    {
        _err(NeuNVS_Error::TooLarge, id);
        return;
    }
    _write(id, (const uint8_t *)v.c_str(), len);
}

bool NeuNVS::getString(uint8_t id, String &out, const String &def)
{
    uint8_t b[NeuNVSConfig::MAX_BLOB + 1];
    memset(b, 0, sizeof(b));
    if (_read(id, b, NeuNVSConfig::MAX_BLOB))
    {
        out = (char *)b;
        return true;
    }
    out = def;
    return false;
}

// ================= REMOVE (FIXED) =================
bool NeuNVS::remove(uint8_t id)
{
    if (id >= NeuNVSConfig::MAX_IDS)
        return false;
    if (_map[id] == 255)
        return true; // already removed

    uint8_t phys = _map[id];
    char key[12];
    snprintf(key, sizeof(key), "k%d", phys);

    if (!xSemaphoreTake(_m, 10))
        return false;

    esp_err_t err = nvs_erase_key(_h, key);
    bool ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
    if (ok)
    {
        _map[id] = 255;
        _revMap[phys] = 255;
        _heatMap[id] = 0.0f;
        _heatVelocity[id] = 0.0f;
        _lastHeat[id] = 0.0f;
        _slotHeat[phys] = 0.0f;
        _dirty = true;
        _saveMap();
    }
    xSemaphoreGive(_m);
    return ok;
}

// ================= CLEAR =================
bool NeuNVS::clear()
{
    if (!xSemaphoreTake(_m, 10))
        return false;
    bool ok = (nvs_erase_all(_h) == ESP_OK);
    if (ok)
    {
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
        nvs_commit(_h);
    }
    xSemaphoreGive(_m);
    return ok;
}

// ================= GLOBAL INSTANCE =================
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuNVS neuNVS;
#endif
