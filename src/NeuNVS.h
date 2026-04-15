#ifndef NEUNVS_H
#define NEUNVS_H

#include "nvs_flash.h"
#include "nvs.h"
#include <type_traits>
#include <Arduino.h>

namespace NeuNVSConstants
{
    constexpr size_t STACK_BUFFER_POD = 64;
    constexpr size_t STACK_BUFFER_STRING = 128;
    constexpr size_t STACK_BUFFER_DUMP = 256;
    constexpr size_t MAX_KEY_LEN = 6;
    constexpr size_t MAX_INSTANCES = 254;
    constexpr size_t MAX_IDS = 255;
}

#ifdef ESP32
#include <esp_timer.h>
inline uint32_t safeMillis() { return (uint32_t)(esp_timer_get_time() / 1000); }
#else
inline uint32_t safeMillis() { return millis(); }
#endif

typedef void (*EEPROMErrorCallback)(uint8_t errorCode, uint8_t id);

class NeuNVS
{
private:
    nvs_handle_t _handle;
    static uint8_t _instanceCount;
    char _currentNS[16];
    uint32_t _lastCommitTime;
    uint32_t _interval;
    bool _isDirty;
    uint16_t _xorCache[NeuNVSConstants::MAX_IDS];

    uint32_t _lockdownUntil;
    uint32_t _lockdownDuration;
    bool _isValid;

    float _heat = 0.0f;          // heat meter
    float _predictWindow = 0.0f; // prediction window
    float _maxHeat;              // maximum heat clamp

    uint32_t _lastPutTime = 0;  // last put time
    uint32_t _adaptiveInterval; // adaptive minimum interval
    uint32_t _maxInterval;      // adaptive maximum interval clamp

    struct DataHeader
    {
        uint16_t xorChecksum; // CalculateXOR result of the data
        uint16_t dataSize;    // Original data size (without header)
        uint8_t magic;        // Magic byte (e.g., 0xA5) for header validation
        uint8_t version;      // Data schema version (useful if there are later struct updates)
        uint16_t reserved;    // Padding to ensure 4-byte struct alignment (ESP32 likes this)
    };

    void get_key(uint8_t id, char *keyOut);
    uint16_t calculateXOR(const uint8_t *data, size_t len);
    bool isDataIdentical(uint8_t id, const uint8_t *newData, size_t newSize);

    EEPROMErrorCallback _errorCallback = nullptr;
    void triggerError(uint8_t code, uint8_t id);

public:
    enum NeuError : uint8_t
    {
        ERR_NONE = 0,
        ERR_LOCKDOWN,
        ERR_WRITE_FAILED,
        ERR_READ_FAILED,
        ERR_ID_NOT_FOUND,
        ERR_DATA_CORRUPT,
        ERR_SIZE_MISMATCH,
        ERR_INSTANCE_INVALID,
        ERR_ALLOC_FAILED
    };

    NeuNVS();
    ~NeuNVS();

    bool begin(uint32_t intervalMs = 1000, uint32_t lockSec = 2);
    void end();
    void update();
    bool commit();
    bool isLocked();
    bool remove(uint8_t id);
    bool clearAll();
    void dump(uint8_t id, size_t byteInLen = 16);
    bool exists(uint8_t id);
    size_t getTotalFreeEntries();
    void onError(EEPROMErrorCallback callback);

    // Tambahkan di dalam class NeuNVS
    float getHeat() const { return _heat; }

    float getConfidence() const
    {
        return 1.0f - (_heat / _maxHeat);
    }

    const char *getNamespace() const
    {
        return _currentNS;
    }
    bool isValid() const { return _isValid; }

    void putString(uint8_t id, const String &value);
    bool getString(uint8_t id, String &outValue, const String &defaultValue = "");

    template <typename T>
    void put(uint8_t id, const T &value)
    {
        if (!_isValid || !_handle || id >= NeuNVSConstants::MAX_IDS)
            return;

        uint16_t newXor = calculateXOR((uint8_t *)&value, sizeof(T));
        if (_xorCache[id] == newXor)
            return;

        uint32_t now = safeMillis();
        if (now < _lockdownUntil)
            return;

        uint32_t dt = now - _lastPutTime;

        // _heat += (dt < 500) ? (1.0f - (float)dt / 500.0f) * 0.4f : 0;
        _heat += (dt < 500) ? (1.0f - (float)dt / 500.0f) * 0.1f : 0;

        if (_heat > _maxHeat)
            _heat = _maxHeat;
        _lastPutTime = now;

        char key[NeuNVSConstants::MAX_KEY_LEN];
        get_key(id, key);

        size_t totalSize = sizeof(DataHeader) + sizeof(T);
        uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_POD];
        uint8_t *buffer = (totalSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(totalSize);

        if (!buffer)
        {
            triggerError(ERR_ALLOC_FAILED, id);
            return;
        }

        // Arrange Header complete with Magic Bytes
        DataHeader header;
        header.xorChecksum = newXor;
        header.dataSize = (uint16_t)sizeof(T);
        header.magic = 0xA5; // Validation key
        header.version = 1;
        header.reserved = 0;

        memcpy(buffer, &header, sizeof(DataHeader));
        memcpy(buffer + sizeof(DataHeader), &value, sizeof(T));

        if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
        {
            _isDirty = true;
            _xorCache[id] = newXor;
        }

        if (buffer != stackBuf)
            free(buffer);
    }

    template <typename T>
    bool get(uint8_t id, T &outValue, T defaultValue = T())
    {
        if (!_isValid || !_handle)
        {
            outValue = defaultValue;
            return false;
        }

        char key[NeuNVSConstants::MAX_KEY_LEN];
        get_key(id, key);

        size_t storedSize = 0;
        if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK)
        {
            outValue = defaultValue;
            return false;
        }

        uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_POD];
        uint8_t *buffer = (storedSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(storedSize);

        if (!buffer || nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
        {
            if (buffer && buffer != stackBuf)
                free(buffer);
            outValue = defaultValue;
            return false;
        }

        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));

        // MAGIC & SIZE VALIDATION
        if (header.magic != 0xA5 || header.dataSize != sizeof(T))
        {
            triggerError(header.magic != 0xA5 ? ERR_DATA_CORRUPT : ERR_SIZE_MISMATCH, id);
            if (buffer != stackBuf)
                free(buffer);
            outValue = defaultValue;
            return false;
        }

        memcpy(&outValue, buffer + sizeof(DataHeader), sizeof(T));
        uint16_t computedXOR = calculateXOR((uint8_t *)&outValue, sizeof(T));

        if (header.xorChecksum != computedXOR)
        {
            triggerError(ERR_DATA_CORRUPT, id);
            outValue = defaultValue;
            if (buffer != stackBuf)
                free(buffer);
            return false;
        }

        if (buffer != stackBuf)
            free(buffer);
        return true;
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuNVS neuNVS;
#endif

#endif
