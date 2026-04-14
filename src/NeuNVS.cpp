#include "NeuNVS.h"

uint8_t NeuNVS::_instanceCount = 0;

NeuNVS::NeuNVS() : _lastCommitTime(0), _interval(1000), _isDirty(false),
                   _commitCount(0), _lockdownUntil(0),
                   _maxCommits(5), _lockdownDuration(5000), _isValid(true)
{
    // Automatically create unique namespace names: ns0, ns1, ns2...
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
    if (!_isValid) return false;
    
    _interval = intervalMs;
    _lockdownDuration = lockSec * 1000;
    _maxCommits = maxCommits;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) return false;

    // Opens the unique namespace that was created in the constructor
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
    }
}

void NeuNVS::get_key(uint8_t id, char *keyOut)
{
    snprintf(keyOut, 6, "i%u", id);
}

bool NeuNVS::exists(uint8_t id)
{
    if (!_isValid) return false;
    
    char key[6];
    get_key(id, key);
    size_t size = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &size);
    return (err != ESP_ERR_NVS_NOT_FOUND);
}

size_t NeuNVS::getTotalFreeEntries()
{
    if (!_isValid) return 0;
    
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_OK)
    {
        return nvs_stats.free_entries;
    }
    return 0;
}

uint16_t NeuNVS::calculateXOR(const uint8_t *data, size_t len)
{
    uint16_t xorResult = 0xAA;
    for (size_t i = 0; i < len; i++)
        xorResult ^= data[i];
    return xorResult;
}

bool NeuNVS::isDataIdentical(uint8_t id, const uint8_t* newData, size_t newSize)
{
    char key[6];
    get_key(id, key);
    
    size_t existingSize = 0;
    nvs_get_blob(_handle, key, NULL, &existingSize);
    
    if (existingSize != newSize) return false;
    
    uint8_t* existingData = (uint8_t*)malloc(existingSize);
    if (!existingData) return false;
    
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
    if (!_isValid) return false;
    if (!_isDirty) return true;

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

    esp_err_t err = nvs_erase_key(_handle, key);
    if (err == ESP_OK)
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

    esp_err_t err = nvs_erase_all(_handle);
    if (err == ESP_OK)
    {
        _isDirty = true;
        return commit();
    }
    return false;
}

void NeuNVS::putString(uint8_t id, const String &value)
{
    if (!_isValid || millis() < _lockdownUntil)
        return;

    char key[6];
    get_key(id, key);
    
    size_t dataLen = value.length();
    size_t totalSize = sizeof(DataHeader) + dataLen;
    
    uint8_t* buffer = (uint8_t*)malloc(totalSize);
    if (!buffer) return;
    
    DataHeader header = {
        calculateXOR((uint8_t*)value.c_str(), dataLen),
        (uint16_t)dataLen
    };
    
    memcpy(buffer, &header, sizeof(DataHeader));
    memcpy(buffer + sizeof(DataHeader), value.c_str(), dataLen);
    
    if (!isDataIdentical(id, buffer, totalSize))
    {
        if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
            _isDirty = true;
    }
    
    free(buffer);
}

String NeuNVS::getString(uint8_t id, const String &defaultValue)
{
    if (!_isValid) return defaultValue;
    
    char key[6];
    get_key(id, key);
    
    size_t storedSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK)
        return defaultValue;
    
    // Validate minimum size (must have header)
    if (storedSize < sizeof(DataHeader))
        return defaultValue;
    
    uint8_t* buffer = (uint8_t*)malloc(storedSize);
    if (!buffer) return defaultValue;
    
    if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
    {
        free(buffer);
        return defaultValue;
    }
    
    DataHeader header;
    memcpy(&header, buffer, sizeof(DataHeader));
    
    // Validate data size consistency
    if (header.dataSize != storedSize - sizeof(DataHeader))
    {
        free(buffer);
        triggerError(ERR_SIZE_MISMATCH, id);
        return defaultValue;
    }
    
    // Validate XOR checksum
    if (header.xorSum != calculateXOR(buffer + sizeof(DataHeader), header.dataSize))
    {
        free(buffer);
        triggerError(ERR_DATA_CORRUPT, id);
        return defaultValue;
    }
    
    // Extract string data
    char* strBuffer = (char*)malloc(header.dataSize + 1);
    if (!strBuffer)
    {
        free(buffer);
        return defaultValue;
    }
    
    memcpy(strBuffer, buffer + sizeof(DataHeader), header.dataSize);
    strBuffer[header.dataSize] = '\0';
    
    String result = String(strBuffer);
    
    free(strBuffer);
    free(buffer);
    
    return result;
}

void NeuNVS::dump(uint8_t id)
{
    if (!_isValid)
    {
        Serial.println("Instance invalid!");
        return;
    }
    
    char key[6];
    get_key(id, key);
    
    size_t storedSize = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &storedSize);
    
    if (err != ESP_OK)
    {
        Serial.printf("ID %u: Not found or empty.\n", id);
        return;
    }
    
    uint8_t* buffer = (uint8_t*)malloc(storedSize);
    if (!buffer)
    {
        Serial.println("Memory allocation failed!");
        return;
    }
    
    nvs_get_blob(_handle, key, buffer, &storedSize);
    
    Serial.printf("Hex Dump ID %u (%u bytes):\n", id, storedSize);
    
    if (storedSize >= sizeof(DataHeader))
    {
        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));
        Serial.printf("Header: XOR=0x%04X, Size=%u\n", header.xorSum, header.dataSize);
        Serial.print("Data -> ");
        
        for (size_t i = sizeof(DataHeader); i < storedSize; i++)
        {
            Serial.printf("%02X ", buffer[i]);
            if ((i - sizeof(DataHeader) + 1) % 16 == 0 && i != storedSize - 1)
                Serial.println();
        }
    }
    else
    {
        Serial.println("Raw data (no header):");
        for (size_t i = 0; i < storedSize; i++)
        {
            Serial.printf("%02X ", buffer[i]);
        }
    }
    
    Serial.println("\n-------------------------------------------");
    free(buffer);
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_NEUNVS)
NeuNVS NeuNVS;
#endif
