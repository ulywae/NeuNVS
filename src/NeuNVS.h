#ifndef NEUNVS_H
#define NEUNVS_H

#include "nvs_flash.h"
#include "nvs.h"
#include <Arduino.h>

typedef void (*EEPROMErrorCallback)(uint8_t errorCode, uint8_t id);

class NeuNVS
{
private:
    nvs_handle_t _handle;          // NVS handle
    static uint8_t _instanceCount; // Global counter for all instances
    char _currentNS[16];           // This instance's unique namespace name
    uint32_t _lastCommitTime;      // Last commit time
    uint32_t _interval;            // Commit interval
    bool _isDirty;                 // Dirty flag

    uint8_t _commitCount;
    uint32_t _lockdownUntil;
    uint8_t _maxCommits;           // Made non-const to be configurable
    uint32_t _lockdownDuration;
    bool _isValid;                  // Flag for instance validity

    struct DataHeader
    {
        uint16_t xorSum;
        uint16_t dataSize;
    };

    void get_key(uint8_t id, char *keyOut);
    uint16_t calculateXOR(const uint8_t *data, size_t len);
    
    // Helper for dirty check with dynamic allocation
    bool isDataIdentical(uint8_t id, const uint8_t* newData, size_t newSize);
    
    EEPROMErrorCallback _errorCallback = nullptr;
    void triggerError(uint8_t code, uint8_t id);

public:
    // ERROR CODE
    enum NeuError : uint8_t
    {
        ERR_NONE = 0,
        ERR_LOCKDOWN = 1,
        ERR_WRITE_FAILED = 2,
        ERR_ID_NOT_FOUND = 3,
        ERR_DATA_CORRUPT = 4,
        ERR_SIZE_MISMATCH = 5,
        ERR_INSTANCE_INVALID = 6
    };

    NeuNVS();
    ~NeuNVS();  // Destructor for cleanup
    bool begin(uint32_t intervalMs = 1000, uint32_t lockSec = 5, uint8_t maxCommits = 5);
    void end();  // Cleanup method
    void update();
    bool commit();
    bool isLocked();
    bool remove(uint8_t id);
    bool clearAll();
    void dump(uint8_t id);
    bool exists(uint8_t id);
    size_t getTotalFreeEntries();  // Renamed to avoid confusion
    void onError(EEPROMErrorCallback callback);
    
    // Get instance info
    const char* getNamespace() const { return _currentNS; }
    bool isValid() const { return _isValid; }

    // String methods with XOR protection
    void putString(uint8_t id, const String &value);
    String getString(uint8_t id, const String &defaultValue = "");

    // Template methods for POD types
    template <typename T>
    void put(uint8_t id, const T &value)
    {
        // Validation
        if (millis() < _lockdownUntil || !_isValid)
            return;

        char key[6];
        get_key(id, key);

        size_t totalSize = sizeof(DataHeader) + sizeof(T);
        
        // Use dynamic allocation for variable size
        uint8_t* buffer = (uint8_t*)malloc(totalSize);
        if (!buffer) return; // Allocation failed
        
        DataHeader header = {calculateXOR((uint8_t *)&value, sizeof(T)), (uint16_t)sizeof(T)};
        memcpy(buffer, &header, sizeof(DataHeader));
        memcpy(buffer + sizeof(DataHeader), &value, sizeof(T));

        // Dirty check with existing data
        if (!isDataIdentical(id, buffer, totalSize))
        {
            if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
                _isDirty = true;
        }
        
        free(buffer);
    }

    template <typename T>
    T get(uint8_t id, T defaultValue = T())
    {
        if (!_isValid) return defaultValue;
        
        char key[6];
        get_key(id, key);

        size_t storedSize = 0;
        esp_err_t err = nvs_get_blob(_handle, key, NULL, &storedSize);

        // Check if ID exists
        if (err == ESP_ERR_NVS_NOT_FOUND)
            return defaultValue;

        // Validate size compatibility
        if (storedSize != sizeof(DataHeader) + sizeof(T))
        {
            triggerError(ERR_SIZE_MISMATCH, id);
            return defaultValue;
        }

        uint8_t* buffer = (uint8_t*)malloc(storedSize);
        if (!buffer) return defaultValue;
        
        if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
        {
            free(buffer);
            return defaultValue;
        }

        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));

        T result;
        memcpy(&result, buffer + sizeof(DataHeader), sizeof(T));
        free(buffer);

        // XOR Validation
        if (header.xorSum != calculateXOR((uint8_t *)&result, sizeof(T)))
        {
            triggerError(ERR_DATA_CORRUPT, id);
            return defaultValue;
        }

        // Validate dataSize
        if (header.dataSize != sizeof(T))
        {
            triggerError(ERR_SIZE_MISMATCH, id);
            return defaultValue;
        }

        return result;
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_NEUNVS)
extern NeuNVS NeuNVS;
#endif

#endif
