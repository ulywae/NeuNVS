#include "NeuNVS.h"

uint8_t NeuNVS::_inst = 0;

// ========== CONSTRUCTOR ==========
NeuNVS::NeuNVS() : _h(0), _m(nullptr), _valid(true), _dirty(false),
                   _lastCommit(0), _lockUntil(0), _cb(nullptr)
{

    if (_inst >= 255)
    { // Check instance limits in general
        _valid = false;
        return;
    }

    snprintf(_ns, sizeof(_ns), "ns%d", _inst++);

    // 1. Reset ALL physical slots to empty state
    for (uint8_t i = 0; i < NeuNVSConfig::PHYS_SLOTS; i++)
    {
        _revMap[i] = 255;
        _slotHeat[i] = 0.0f;
    }

    // 2. Initialize logical data and initial mapping (0 to 0, 1 to 1, etc.)
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        _heatMap[i] = 0.0f;
        _heatVelocity[i] = 0.0f;
        _lastHeat[i] = 0.0f;
        _heatThreshold[i] = NeuNVSConfig::HEAT_LOCK;
        _lastWriteMap[i] = 0;

        _map[i] = i;    // Mapping logical i to physical i
        _revMap[i] = i; // Mark physical slot i as owned by logical i
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

    _m = xSemaphoreCreateMutex(); // Create a Mutex FIRST
    if (!_m)
        return false;

    _loadMap(); // Now safe if loadMap needs to call saveMap internally.

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
    // 1. Validasi input dasar
    if (!out || len == 0)
        return false;

    // 2. Langsung baca ke buffer 'out' milik user
    // _read akan memanggil _read_raw yang sudah punya pengecekan size internal
    return _read(id, out, len);
}

// ========= Update (call often in loop) =========
void NeuNVS::update()
{
    if (!_h)
        return;

    uint32_t now = millis();

    // 1. LIMITER CPU (Jalankan kalkulasi heat tiap 20ms saja)
    static uint32_t lastHeatUpdate = 0;
    if (now - lastHeatUpdate < 20)
        return;

    // Hitung delta time dalam detik
    float dt = (now - lastHeatUpdate) * 0.001f;
    lastHeatUpdate = now;

    // 2. HEAT & PREDICTION LOGIC
    float maxHeat = 0.0f;
    for (uint8_t i = 0; i < NeuNVSConfig::MAX_IDS; i++)
    {
        float &h = _heatMap[i];

        // Decay (Peluruhan panas)
        h *= (1.0f - dt * NeuNVSConfig::DECAY_RATE);
        if (h < 0.0f)
            h = 0.0f;

        // Velocity & Prediction (Menghitung seberapa cepat user nyepam)
        float dv = (h - _lastHeat[i]) / (dt + 0.0001f);
        _heatVelocity[i] = _heatVelocity[i] * 0.8f + dv * 0.2f;
        _lastHeat[i] = h;

        float predicted = h + _heatVelocity[i] * NeuNVSConfig::PREDICT_GAIN;

        // Adaptive Threshold (Semakin cepat nyepam, threshold semakin turun/ketat)
        _heatThreshold[i] = NeuNVSConfig::HEAT_LOCK - (predicted * NeuNVSConfig::THRESHOLD_K);

        // Sinkronisasi panas ke slot fisik (untuk kebutuhan dump/wear leveling)
        _slotHeat[_map[i]] = h;

        if (h > maxHeat)
            maxHeat = h;
    }

    // 3. LOCKDOWN MANAGEMENT
    // Menggunakan int32_t cast untuk menangani millis rollover
    if (_lockUntil && (int32_t)(now - _lockUntil) >= 0)
        _lockUntil = 0;

    // 4. SMART COMMIT (Delayed Write)
    if (_dirty && !_lockUntil)
    {
        // Interval commit dinamis: Semakin panas, semakin lama nunggu (biar gak nyiksa flash)
        uint32_t dynamicInterval = _commitMs + (uint32_t)(maxHeat * 100.0f);

        if ((int32_t)(now - _lastCommit) > (int32_t)dynamicInterval)
            commit(); // Panggil fungsi commit yang sudah ada Mutex-nya
    }
}

// ================= STRING API =================
void NeuNVS::putString(uint8_t id, const String &v)
{
    // Tambahkan +1 agar null terminator ikut tersimpan
    // Pastikan tidak melebihi MAX_BLOB
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
    uint8_t b[NeuNVSConfig::MAX_BLOB + 1]; // +1 untuk null terminator aman
    memset(b, 0, sizeof(b));

    // Langsung baca ke buffer lokal b
    if (_read(id, b, NeuNVSConfig::MAX_BLOB))
    {
        out = (char *)b;
        return true;
    }

    out = def;
    return false;
}

// ========= Global instance =========
NeuNVS neuNVS;