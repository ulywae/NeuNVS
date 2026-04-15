#include "NeuNVS.h"

uint8_t NeuNVS::_instanceCount = 0;

NeuNVS::NeuNVS() : _lastCommitTime(0), _interval(1000), _isDirty(false),
                   _commitCount(0), _lockdownUntil(0),
                   _maxCommits(5), _lockdownDuration(5000), _isValid(true)
{
    if (_instanceCount < NeuNVSConstants::MAX_INSTANCES)
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
    }
    _isValid = false;
}

void NeuNVS::get_key(uint8_t id, char *keyOut)
{
    snprintf(keyOut, NeuNVSConstants::MAX_KEY_LEN, "id%u", id);
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
    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t existingSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &existingSize) != ESP_OK)
        return false;
    if (existingSize != newSize)
        return false;

    // --- OPTIMIZATION START ---
    // Use a stack buffer for data <= STACK_BUFFER_POD bytes (covers almost all PODs & small structs)
    // This eliminates 'malloc' and 'free' which consume CPU & Heap cycles
    uint8_t stackBuffer[NeuNVSConstants::STACK_BUFFER_POD];
    uint8_t *existingData = nullptr;

    if (existingSize <= sizeof(stackBuffer))
        existingData = stackBuffer;
    else
    {
        existingData = (uint8_t *)malloc(existingSize);
        if (!existingData)
        {
            triggerError(ERR_ALLOC_FAILED, id);
            return false;
        }
    }

    esp_err_t err = nvs_get_blob(_handle, key, existingData, &existingSize);
    if (err != ESP_OK)
    {
        triggerError(ERR_READ_FAILED, id);
        if (existingData != stackBuffer)
            free(existingData);
        return false;
    }

    bool identical = (memcmp(existingData, newData, newSize) == 0);

    // Only free if you use malloc
    if (existingData != stackBuffer)
        free(existingData);
    // --- END OPTIMIZATION ---

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
    if (!_isValid || !_isDirty || safeMillis() < _lockdownUntil || !_handle)
        return;
    if (safeMillis() - _lastCommitTime >= _interval)
        commit();
}

bool NeuNVS::commit()
{
    if (!_isValid || !_handle)
        return false;

    if (!_isDirty)
        return true;

    uint32_t now = safeMillis();
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
        _commitCount = 0;

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
    return (safeMillis() < _lockdownUntil);
}

bool NeuNVS::remove(uint8_t id)
{
    if (!_isValid || safeMillis() < _lockdownUntil || !_handle)
        return false;

    char key[NeuNVSConstants::MAX_KEY_LEN];
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
    if (!_isValid || safeMillis() < _lockdownUntil || !_handle)
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
    if (!_isValid || !_handle)
        return false;

    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t size = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &size);
    return (err != ESP_ERR_NVS_NOT_FOUND);
}

void NeuNVS::putString(uint8_t id, const String &value)
{
    if (!_isValid || safeMillis() < _lockdownUntil || !_handle)
        return;

    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t dataLen = value.length();
    size_t totalSize = sizeof(DataHeader) + dataLen;

    uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_STRING];
    uint8_t *buffer = (totalSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(totalSize);

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

    if (buffer != stackBuf)
        free(buffer);
}

bool NeuNVS::getString(uint8_t id, String &outValue, const String &defaultValue)
{
    if (!_isValid || !_handle)
    {
        outValue = defaultValue;
        return false;
    }

    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t storedSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK || storedSize < sizeof(DataHeader))
    {
        outValue = defaultValue;
        return false;
    }

    // Hybrid Stack for reading
    uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_STRING];
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

    uint8_t *dataPtr = buffer + sizeof(DataHeader);
    uint16_t computedXOR = calculateXOR(dataPtr, header.dataSize);

    if (header.dataSize != (storedSize - sizeof(DataHeader)) || header.xorSum != computedXOR)
    {
        triggerError(header.xorSum != computedXOR ? ERR_DATA_CORRUPT : ERR_SIZE_MISMATCH, id);

        if (buffer != stackBuf)
            free(buffer);

        outValue = defaultValue;
        return false;
    }

    // Use efficient String constructor with data length
    outValue = String((const char *)dataPtr, header.dataSize);

    if (buffer != stackBuf)
        free(buffer);

    return true;
}

void NeuNVS::dump(uint8_t id, size_t byteInLen)
{
    if (!_isValid || !_handle)
    {
        Serial.println(F("NeuNVS: Instance invalid!"));
        return;
    }

    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t storedSize = 0;
    if (nvs_get_blob(_handle, key, NULL, &storedSize) != ESP_OK)
    {
        Serial.printf("ID %u: Key '%s' not found.\n", id, key);
        return;
    }

    uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_DUMP];
    uint8_t *buffer = (storedSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(storedSize);

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

        size_t dataLen = storedSize - sizeof(DataHeader);
        uint8_t *dataPtr = buffer + sizeof(DataHeader);

        for (size_t i = 0; i < dataLen; i += byteInLen)
        {
            // Print Hex
            Serial.print("Hex  : ");
            for (size_t j = 0; j < byteInLen; j++)
            {
                if (i + j < dataLen)
                    Serial.printf("%02X ", dataPtr[i + j]);
                else
                    Serial.print("  ");
            }

            // Print ASCII
            Serial.print(" | ");
            for (size_t j = 0; j < byteInLen; j++)
            {
                if (i + j < dataLen)
                {
                    uint8_t b = dataPtr[i + j];
                    Serial.print((b >= 32 && b <= 126) ? (char)b : '.');
                }
            }
            Serial.println();
        }
    }
    Serial.println(F("-------------------------------------------"));

    if (buffer != stackBuf)
        free(buffer);
}

size_t NeuNVS::getTotalFreeEntries()
{
    if (!_isValid)
        return 0;

    nvs_stats_t stats;
    esp_err_t err = nvs_get_stats(NULL, &stats);
    if (err == ESP_OK)
        return stats.free_entries;
    else
    {
        triggerError(ERR_WRITE_FAILED, 0);
        return 0;
    }
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuNVS neuNVS;
#endif