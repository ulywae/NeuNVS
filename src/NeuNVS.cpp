#include "NeuNVS.h"

uint8_t NeuNVS::_instanceCount = 0;

NeuNVS::NeuNVS() : _lastCommitTime(0), _interval(1000), _isDirty(false),
                   _commitCount(0), _lockdownUntil(0),
                   _maxCommits(5), _lockdownDuration(5000), _isValid(true)
{
    if (_instanceCount < 254)
    {
        snprintf(_currentNS, sizeof(_currentNS), "ns%u", _instanceCount);
        _instanceCount++;
    }
    else
    {
        _isValid = false;
        _currentNS[0] = '\0';
    }
}

NeuNVS::~NeuNVS()
{
    end();
}

bool NeuNVS::begin(uint32_t intervalMs, uint32_t lockSec, uint8_t maxCommits)
{
    if (!_isValid)
        return false;

    _interval = intervalMs;
    _lockdownDuration = lockSec * 1000;
    _maxCommits = maxCommits;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
        return false;

    err = nvs_open(_currentNS, NVS_READWRITE, &_handle);
    return (err == ESP_OK);
}

void NeuNVS::end()
{
    if (_handle)
    {
        commit();
        nvs_close(_handle);
        _handle = 0;
        _isValid = false;
    }
}

void NeuNVS::get_key(uint8_t id, char *keyOut)
{
    snprintf(keyOut, 6, "id%u", id);
}

uint16_t NeuNVS::calculateXOR(const uint8_t *data, size_t len)
{
    uint16_t xorResult = 0xAA;
    for (size_t i = 0; i < len; i++)
        xorResult ^= data[i];
    return xorResult;
}

bool NeuNVS::isDataIdentical(uint8_t id, const uint8_t *newData, size_t newSize)
{
    char key[6];
    get_key(id, key);

    size_t existingSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &existingSize) != ESP_OK)
        return false;
    if (existingSize != newSize)
        return false;

    uint8_t *existingData = (uint8_t *)malloc(existingSize);
    if (!existingData)
    {
        triggerError(ERR_ALLOC_FAILED, id);
        return false;
    }

    nvs_get_blob(_handle, key, existingData, &existingSize);
    bool identical = (memcmp(existingData, newData, newSize) == 0);
    free(existingData);
    return identical;
}

void NeuNVS::onError(EEPROMErrorCallback callback)
{
    _errorCallback = callback;
}

void NeuNVS::triggerError(uint8_t code, uint8_t id)
{
    if (_errorCallback)
        _errorCallback(code, id);
}

void NeuNVS::update()
{
    if (!_isValid || !_isDirty || millis() < _lockdownUntil)
        return;
    if (millis() - _lastCommitTime >= _interval)
        commit();
}

bool NeuNVS::commit()
{
    if (!_isValid)
        return false;
    if (!_isDirty)
        return true;

    uint32_t now = millis();
    if (now < _lockdownUntil)
    {
        triggerError(ERR_LOCKDOWN, 0);
        return false;
    }

    if (now - _lastCommitTime < _interval)
    {
        _commitCount++;
        if (_commitCount >= _maxCommits)
        {
            _lockdownUntil = now + _lockdownDuration;
            _commitCount = 0;
            triggerError(ERR_LOCKDOWN, 0);
            return false;
        }
    }
    else
    {
        _commitCount = 0;
    }

    if (nvs_commit(_handle) != ESP_OK)
    {
        triggerError(ERR_WRITE_FAILED, 0);
        return false;
    }

    _lastCommitTime = now;
    _isDirty = false;
    return true;
}

bool NeuNVS::isLocked()
{
    return (millis() < _lockdownUntil);
}

bool NeuNVS::remove(uint8_t id)
{
    if (!_isValid || millis() < _lockdownUntil)
        return false;
    char key[6];
    get_key(id, key);

    if (nvs_erase_key(_handle, key) == ESP_OK)
    {
        _isDirty = true;
        return true;
    }
    return false;
}

bool NeuNVS::clearAll()
{
    if (!_isValid || millis() < _lockdownUntil)
        return false;
    if (nvs_erase_all(_handle) == ESP_OK)
    {
        _isDirty = true;
        return commit();
    }
    return false;
}

bool NeuNVS::exists(uint8_t id)
{
    if (!_isValid)
        return false;
    char key[6];
    get_key(id, key);

    size_t size = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &size);
    return (err != ESP_ERR_NVS_NOT_FOUND);
}

void NeuNVS::putString(uint8_t id, const String &value)
{
    if (!_isValid || millis() < _lockdownUntil)
        return;
    char key[6];
    get_key(id, key);

    size_t dataLen = value.length();
    size_t totalSize = sizeof(DataHeader) + dataLen;
    uint8_t *buffer = (uint8_t *)malloc(totalSize);
    if (!buffer)
    {
        triggerError(ERR_ALLOC_FAILED, id);
        return;
    }

    DataHeader header = {calculateXOR((uint8_t *)value.c_str(), dataLen), (uint16_t)dataLen};
    memcpy(buffer, &header, sizeof(DataHeader));
    memcpy(buffer + sizeof(DataHeader), value.c_str(), dataLen);

    if (!isDataIdentical(id, buffer, totalSize))
    {
        if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
            _isDirty = true;
    }
    free(buffer);
}

bool NeuNVS::getString(uint8_t id, String &outValue, const String &defaultValue)
{
    if (!_isValid)
    {
        outValue = defaultValue;
        return false;
    }
    char key[6];
    get_key(id, key);

    size_t storedSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK)
    {
        outValue = defaultValue;
        return false;
    }
    if (storedSize < sizeof(DataHeader))
    {
        outValue = defaultValue;
        return false;
    }

    uint8_t *buffer = (uint8_t *)malloc(storedSize);
    if (!buffer)
    {
        triggerError(ERR_ALLOC_FAILED, id);
        outValue = defaultValue;
        return false;
    }

    if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
    {
        free(buffer);
        outValue = defaultValue;
        return false;
    }

    DataHeader header;
    memcpy(&header, buffer, sizeof(DataHeader));
    if (header.dataSize != storedSize - sizeof(DataHeader))
    {
        triggerError(ERR_SIZE_MISMATCH, id);
        free(buffer);
        outValue = defaultValue;
        return false;
    }
    if (header.xorSum != calculateXOR(buffer + sizeof(DataHeader), header.dataSize))
    {
        triggerError(ERR_DATA_CORRUPT, id);
        free(buffer);
        outValue = defaultValue;
        return false;
    }

    outValue = String((char *)(buffer + sizeof(DataHeader)), header.dataSize);
    free(buffer);
    return true;
}

void NeuNVS::dump(uint8_t id)
{
    if (!_isValid)
    {
        Serial.println(F("NeuNVS: Instance invalid!"));
        return;
    }

    char key[6];
    get_key(id, key);

    size_t storedSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK)
    {
        Serial.printf("ID %u: Key '%s' not found.\n", id, key);
        return;
    }

    uint8_t *buffer = (uint8_t *)malloc(storedSize);
    if (!buffer)
    {
        Serial.println(F("NeuNVS: Dump alloc failed!"));
        return;
    }

    nvs_get_blob(_handle, key, buffer, &storedSize);

    Serial.printf("\n--- NeuNVS Dump ID: %u (%u bytes) ---\n", id, (uint32_t)storedSize);

    if (storedSize >= sizeof(DataHeader))
    {
        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));
        Serial.printf("Header -> XOR: 0x%02X, DataSize: %u\n", header.xorSum, header.dataSize);

        Serial.print("Data   -> Hex: ");
        for (size_t i = sizeof(DataHeader); i < storedSize; i++)
        {
            Serial.printf("%02X ", buffer[i]);
        }

        Serial.print("\n          Text: ");
        for (size_t i = sizeof(DataHeader); i < storedSize; i++)
        {
            // Cek apakah karakter bisa diprint (ASCII 32-126)
            if (buffer[i] >= 32 && buffer[i] <= 126)
            {
                Serial.print((char)buffer[i]);
            }
            else
            {
                Serial.print('.'); // Ganti karakter non-printable dengan titik
            }
        }
    }
    Serial.println(F("\n-------------------------------------------"));
    free(buffer);
}

size_t NeuNVS::getTotalFreeEntries()
{
    if (!_isValid)
        return 0;

    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK)
    {
        return stats.free_entries;
    }
    else
    {
        triggerError(ERR_WRITE_FAILED, 0); // bisa pakai kode error umum
        return 0;
    }
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuNVS neuNVS;
#endif