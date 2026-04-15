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

    uint8_t _commitCount;
    uint32_t _lockdownUntil;
    uint8_t _maxCommits;
    uint32_t _lockdownDuration;
    bool _isValid;

    struct DataHeader
    {
        uint16_t xorSum;   // checksum ringan
        uint16_t dataSize; // ukuran data
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
    bool begin(uint32_t intervalMs = 1000, uint32_t lockSec = 5, uint8_t maxCommits = 5);
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

    const char *getNamespace() const { return _currentNS; }
    bool isValid() const { return _isValid; }

    void putString(uint8_t id, const String &value);
    bool getString(uint8_t id, String &outValue, const String &defaultValue = "");

    template <typename T>
    void put(uint8_t id, const T &value)
    {
        static_assert(std::is_standard_layout<T>::value && std::is_trivial<T>::value,
                      "Data type is too complex for put()! Use putString for String.");

        if (safeMillis() < _lockdownUntil || !_isValid || !_handle)
            return;

        char key[NeuNVSConstants::MAX_KEY_LEN];
        get_key(id, key);

        size_t totalSize = sizeof(DataHeader) + sizeof(T);

        // --- Hybrid Stack/Heap Allocation ---
        uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_POD];
        uint8_t *buffer = (totalSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(totalSize);

        if (!buffer)
        {
            triggerError(ERR_ALLOC_FAILED, id);
            return;
        }

        DataHeader header = {calculateXOR((uint8_t *)&value, sizeof(T)), (uint16_t)sizeof(T)};
        memcpy(buffer, &header, sizeof(DataHeader));
        memcpy(buffer + sizeof(DataHeader), &value, sizeof(T));

        if (!isDataIdentical(id, buffer, totalSize))
        {
            if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
                _isDirty = true;
        }

        if (buffer != stackBuf)
            free(buffer);
    }

    template <typename T>
    bool get(uint8_t id, T &outValue, T defaultValue = T())
    {
        static_assert(std::is_standard_layout<T>::value && std::is_trivial<T>::value,
                      "Data type is too complex for get()! Use getString for String.");

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

        if (storedSize != sizeof(DataHeader) + sizeof(T))
        {
            triggerError(ERR_SIZE_MISMATCH, id);
            outValue = defaultValue;
            return false;
        }

        // --- Hybrid Stack/Heap Allocation ---
        uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_POD];
        uint8_t *buffer = (storedSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(storedSize);

        if (!buffer)
        {
            triggerError(ERR_ALLOC_FAILED, id);
            outValue = defaultValue;
            return false;
        }

        if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
        {
            if (buffer != stackBuf)
                free(buffer);

            outValue = defaultValue;
            return false;
        }

        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));
        memcpy(&outValue, buffer + sizeof(DataHeader), sizeof(T));

        uint16_t computedXOR = calculateXOR((uint8_t *)&outValue, sizeof(T));
        bool success = (header.xorSum == computedXOR && header.dataSize == sizeof(T));

        if (!success)
        {
            triggerError(header.xorSum != computedXOR ? ERR_DATA_CORRUPT : ERR_SIZE_MISMATCH, id);
            outValue = defaultValue;
        }

        if (buffer != stackBuf)
            free(buffer);

        return success;
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern NeuNVS neuNVS;
#endif

#endif
