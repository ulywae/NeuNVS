#pragma once

#include <Arduino.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <type_traits>

// ================= CONFIG =================
namespace NeuNVSConfig
{
    static constexpr uint8_t MAX_IDS = 32;             // jumlah ID yang didukung, modifikasi jika perlu
    static constexpr uint8_t PHYS_SLOTS = MAX_IDS + 8; // Slot fisik yang tersedia di NVS
    static constexpr uint16_t MAX_BLOB = 256;          // ukuran maksimum data (payload) per ID, termasuk header internal

    static constexpr uint32_t FAST_WINDOW = 50; // ms
    static constexpr float HEAT_MAX = 10.0f;    // batas maksimum heat
    static constexpr float HEAT_LOCK = 9.5f;    // batas lock heat

    static constexpr uint32_t COMMIT_MS = 200; // interval commit otomatis
    static constexpr uint32_t LOCK_MS = 3000;  // durasi lock saat heat terlalu tinggi

    static constexpr float DECAY_RATE = 0.35f;   // decay rate per detik
    static constexpr float PREDICT_GAIN = 0.15f; // gain untuk prediksi heat masa depan
    static constexpr float THRESHOLD_K = 0.5f;   // faktor pengurang threshold berdasarkan prediksi
}

// ================= ERROR =================
enum class NeuNVS_Error
{
    None,
    Lock,
    WriteFail,
    ReadFail,
    NotFound,
    TooLarge,
    SystemFail,
    InvalidID,
    Migration
};

using NeuNVS_ErrorCb = void (*)(NeuNVS_Error, uint8_t);

// ================= HEADER =================
class NeuNVS
{
public:
    NeuNVS();
    ~NeuNVS();

    bool begin(uint32_t commitMs = NeuNVSConfig::COMMIT_MS,
               uint32_t lockMs = NeuNVSConfig::LOCK_MS);

    void end();
    void update();

    // ===== PUBLIC API (logical layer) =====
    template <typename T>
    bool put(uint8_t id, const T &data)
    {
        static_assert(std::is_trivially_copyable<T>::value,
                      "NeuNVS Error: Data type must be trivially copyable for safe flash storage!");
        static_assert(sizeof(T) <= NeuNVSConfig::MAX_BLOB,
                      "NeuNVS Error: Data size exceeds MAX_BLOB limit!");
        return put(id, (const uint8_t *)&data, sizeof(T));
    }

    template <typename T>
    bool get(uint8_t id, T &out)
    {
        static_assert(std::is_trivially_copyable<T>::value,
                      "NeuNVS Error: Output type must be trivially copyable!");
        return get(id, (uint8_t *)&out, sizeof(T));
    }
    bool put(uint8_t id, const uint8_t *data, size_t len);
    bool get(uint8_t id, uint8_t *out, size_t len);
    void putString(uint8_t id, const String &v);
    bool getString(uint8_t id, String &out, const String &def = "");

    bool remove(uint8_t id);
    bool clear();
    void commit();

    bool isLocked() const;

    // ===== HEAT ACCESS =====
    float getHeat(uint8_t id) const;
    float getHeatMax() const;
    float getHeatAvg() const;
    uint32_t lastHeatUpdate = 0;
    void dump();
    void dump(uint8_t id);

    // ===== CALLBACK =====
    void onError(NeuNVS_ErrorCb cb);

private:
    // ================= CORE IO =================
    bool _write(uint8_t id, const uint8_t *data, size_t len);
    bool _read(uint8_t id, uint8_t *out, size_t len);

    bool _write_raw(uint8_t physId, const uint8_t *data, size_t len);
    bool _read_raw(uint8_t physId, uint8_t *out, size_t len);

    // ================= MIGRATION & ALLOC =================
    void _migrate(uint8_t logicalId);
    int16_t _allocateSlot(); // cari slot fisik kosong terdingin

    bool _saveMap();
    bool _loadMap();

    // ================= UTIL =================
    uint16_t _crc(const uint8_t *data, size_t len);
    void _err(NeuNVS_Error e, uint8_t id);

    // ================= STATE =================
    nvs_handle_t _h;
    SemaphoreHandle_t _m;
    char _ns[16];
    static uint8_t _inst;
    bool _valid;

    // ================= DIRTY CONTROL =================
    bool _dirty;
    uint32_t _lockMs;
    uint32_t _lockUntil;
    uint32_t _commitMs;
    uint32_t _lastCommit;

    // ================= CALLBACK =================
    NeuNVS_ErrorCb _cb;

    // ================= HEAT (LOGICAL) =================
    float _heatMap[NeuNVSConfig::MAX_IDS];
    float _heatVelocity[NeuNVSConfig::MAX_IDS];
    float _lastHeat[NeuNVSConfig::MAX_IDS];
    float _heatThreshold[NeuNVSConfig::MAX_IDS];

    uint32_t _lastWriteMap[NeuNVSConfig::MAX_IDS];

    // ================= WEAR LEVELING (MAPPING) =================
    uint8_t _map[NeuNVSConfig::MAX_IDS];
    uint8_t _revMap[NeuNVSConfig::PHYS_SLOTS];
    float _slotHeat[NeuNVSConfig::PHYS_SLOTS];

    // ================= BUFFER =================
    uint8_t _buf[NeuNVSConfig::MAX_BLOB + 16];

    struct Header
    {
        uint8_t magic;
        uint16_t size;
        uint16_t crc;
    };
};

// ================= GLOBAL INSTANCE =================
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuNVS neuNVS;
#endif
