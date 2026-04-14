#line 1 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\SecureEEPROM.h"
#ifndef SECURE_EEPROM_H
#define SECURE_EEPROM_H

#include "nvs_flash.h"
#include "nvs.h"
#include <Arduino.h>

typedef void (*EEPROMErrorCallback)(uint8_t errorCode, uint8_t id);

class SecureEEPROM
{
private:
    nvs_handle_t _handle;
    static uint8_t _instanceCount; // Counter global untuk semua instance
    char _currentNS[16];           // Nama namespace unik instance ini
    uint32_t _lastCommitTime;
    uint32_t _interval;
    bool _isDirty;

    uint8_t _commitCount;
    uint32_t _lockdownUntil;
    const uint8_t _maxCommits;
    uint32_t _lockdownDuration;

    struct DataHeader
    {
        uint16_t xorSum;
        uint16_t dataSize;
    };

    void get_key(uint8_t id, char *keyOut);
    uint16_t calculateXOR(const uint8_t *data, size_t len);

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
        ERR_DATA_CORRUPT = 4
    };

    SecureEEPROM();
    bool begin(uint32_t intervalMs = 1000, uint32_t lockSec = 5);
    void update();
    bool commit();
    bool isLocked();
    bool remove(uint8_t id);
    bool clearAll();
    void dump(uint8_t id);
    bool exists(uint8_t id);
    size_t freeEntries();
    void onError(EEPROMErrorCallback callback);

    template <typename T>
    void put(uint8_t id, const T &value)
    {
        if (millis() < _lockdownUntil)
            return;

        char key[6];
        get_key(id, key);

        size_t totalSize = sizeof(DataHeader) + sizeof(T);
        uint8_t buffer[totalSize];

        DataHeader header = {calculateXOR((uint8_t *)&value, sizeof(T)), (uint16_t)sizeof(T)};
        memcpy(buffer, &header, sizeof(DataHeader));
        memcpy(buffer + sizeof(DataHeader), &value, sizeof(T));

        size_t existingSize = 0;
        nvs_get_blob(_handle, key, NULL, &existingSize);
        if (existingSize == totalSize)
        {
            uint8_t existingData[totalSize];
            nvs_get_blob(_handle, key, existingData, &existingSize);
            if (memcmp(existingData, buffer, totalSize) == 0)
                return;
        }

        if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
            _isDirty = true;
    }

    template <typename T>
    T get(uint8_t id, T defaultValue = T())
    {
        char key[6];
        get_key(id, key);

        size_t storedSize = 0;
        esp_err_t err = nvs_get_blob(_handle, key, NULL, &storedSize);

        // 1. Cek apakah ID ada di NVS
        if (err == ESP_ERR_NVS_NOT_FOUND)
            // Tidak perlu trigger error karena mungkin memang data pertama kali dibuat
            return defaultValue;

        // 2. Cek kecocokan ukuran (mencegah salah tipe data)
        if (storedSize != sizeof(DataHeader) + sizeof(T))
        {
            triggerError(ERR_ID_NOT_FOUND, id); // ERR_ID_NOT_FOUND atau ERR_SIZE_MISMATCH
            return defaultValue;
        }

        uint8_t buffer[storedSize];
        if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
            return defaultValue;

        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));

        T result;
        memcpy(&result, buffer + sizeof(DataHeader), sizeof(T));

        // 3. Validasi XOR (Cek integritas data)
        if (header.xorSum != calculateXOR((uint8_t *)&result, sizeof(T)))
        {
            triggerError(ERR_DATA_CORRUPT, id); // ERR_DATA_CORRUPT
            return defaultValue;
        }

        return result;
    }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern SecureEEPROM EEPROM; // Deklarasi objek global bernama 'EEPROM'
#endif

#endif
