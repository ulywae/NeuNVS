#include "NeuNVS.h"

uint8_t NeuNVS::_instanceCount = 0;

NeuNVS::NeuNVS() : _handle(0), _lastCommitTime(0), _interval(1000),
                   _isDirty(false), _lockdownUntil(0),
                   _lockdownDuration(5000), _isValid(true),
                   _adaptiveInterval(1000), _maxInterval(5000), _maxHeat(10.0f)
{
    // Initialize the cache so that it does not contain junk data
    memset(_xorCache, 0, sizeof(_xorCache));

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

bool NeuNVS::begin(uint32_t intervalMs, uint32_t lockSec)
{
    if (!_isValid)
        return false;

    _interval = intervalMs;
    _adaptiveInterval = intervalMs; // Reset adaptive to base interval
    _lockdownDuration = lockSec * 1000;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
        return false;

    err = nvs_open(_currentNS, NVS_READWRITE, &_handle);
    if (err != ESP_OK)
        return false;

    // --- WARM UP CACHE ---
    memset(_xorCache, 0, sizeof(_xorCache));

    for (uint8_t i = 0; i < NeuNVSConstants::MAX_IDS; i++)
    {
        char key[NeuNVSConstants::MAX_KEY_LEN];
        get_key(i, key);

        DataHeader header;
        size_t required = sizeof(DataHeader);

        esp_err_t readErr = nvs_get_blob(_handle, key, &header, &required);

        // Validation: Ensure the data exists AND has the correct Magic Bytes
        if ((readErr == ESP_OK || readErr == ESP_ERR_NVS_INVALID_LENGTH) && header.magic == 0xA5)
        {
            _xorCache[i] = header.xorChecksum;
        }
    }

    return true;
}

void NeuNVS::end()
{
    if (_handle)
    {
        if (_isDirty)
            commit(); // Only commit if there are actual changes
        nvs_close(_handle);
        _handle = 0;
    }
}

void NeuNVS::get_key(uint8_t id, char *keyOut)
{
    // Use "k%u" for shorter and safer buffer size 6
    // "id255" + null terminator fits 6 bytes.
    snprintf(keyOut, NeuNVSConstants::MAX_KEY_LEN, "k%u", id);
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

bool NeuNVS::isLocked()
{
    return (safeMillis() < _lockdownUntil);
}

uint16_t NeuNVS::calculateXOR(const uint8_t *data, size_t len)
{
    uint16_t xorResult = 0xAA55; // A more unique seed for XOR
    for (size_t i = 0; i < len; i++)
        xorResult = (xorResult ^ data[i]) + (xorResult << 1); // Variations to minimize collisions
    return xorResult;
}

bool NeuNVS::isDataIdentical(uint8_t id, const uint8_t *newData, size_t newSize)
{
    if (id >= NeuNVSConstants::MAX_IDS)
        return false;

    // Take the XOR of the new data sent (located in the first 2 bytes of the buffer)
    uint16_t newXOR;
    memcpy(&newXOR, newData, sizeof(uint16_t));

    // Compare with Cache in RAM
    if (_xorCache[id] == newXOR)
    {
        return true;
    }

    // DO NOT update the cache here.
    // Cache updates should only be done in put() AFTER nvs_set_blob succeeds.
    return false;
}

void NeuNVS::update()
{
    if (!_isValid || !_handle || !_isDirty)
    {
        // Passive cooling while idle
        _heat *= 0.98f;
        return;
    }

    uint32_t now = safeMillis();

    // Lockdown protection active
    if (now < _lockdownUntil)
        return;

    // Adaptive Auto-Commit Logic
    // We commit if:
    // 1. The adaptive interval has passed
    // 2. OR it has been idle for a long time (no new put()s have been received)
    uint32_t timeSinceLastPut = now - _lastPutTime;
    uint32_t timeSinceLastCommit = now - _lastCommitTime;

    // Calculate the prediction window (when it's 'safe' to write)
    float dynamicWindow = _predictWindow * (1.0f + _heat * 0.2f);

    if (timeSinceLastPut >= dynamicWindow && timeSinceLastCommit >= _adaptiveInterval)
        commit();
}

bool NeuNVS::commit()
{
    if (!_isValid || !_handle || !_isDirty)
        return !_isDirty;

    uint32_t now = safeMillis();
    if (now < _lockdownUntil)
        return false;

    uint32_t delta = now - _lastCommitTime;

    // 1. ADAPTIVE HEAT PENALTY
    if (delta < _interval)
    {
        float penalty = (float)(_interval - delta) / _interval;
        _heat += penalty * 0.8f; // Increase to be more firm against looping
    }
    else
        _heat *= 0.80f; // Cooling is faster if the user is orderly

    if (_heat > _maxHeat)
        _heat = _maxHeat;

    // 2. LOCKDOWN CHECK (Threshold aggressive 6.0)
    if (_heat >= 6.0f)
    {
        _lockdownUntil = now + _lockdownDuration;
        triggerError(ERR_LOCKDOWN, 0);
        return false;
    }

    // 3. ADAPTIVE INTERVAL CALCULATION
    _adaptiveInterval = _interval + (uint32_t)(_heat * 100.0f);
    _adaptiveInterval = constrain(_adaptiveInterval, _interval, _maxInterval);

    // 4. EXECUTION
    if (nvs_commit(_handle) == ESP_OK)
    {
        _lastCommitTime = now;
        _isDirty = false;
        return true;
    }

    _heat += 2.0f; // Extra penalty if hardware fails to respond
    return false;
}

bool NeuNVS::remove(uint8_t id)
{
    if (!_isValid || !_handle || id >= NeuNVSConstants::MAX_IDS)
        return false;

    if (safeMillis() < _lockdownUntil)
        return false;

    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    if (nvs_erase_key(_handle, key) == ESP_OK)
    {
        _xorCache[id] = 0; // Clear cache to sync
        _isDirty = true;
        return true;
    }
    return false;
}

bool NeuNVS::clearAll()
{
    // 1. Check validity & Lockdown
    if (!_isValid || !_handle || safeMillis() < _lockdownUntil)
        return false;

    // 2. Execute erase in NVS
    if (nvs_erase_all(_handle) == ESP_OK)
    {
        // 3. IMPORTANT: Reset RAM Cache
        // So that subsequent put()s don't assume the data is still there (false positives are identical)
        memset(_xorCache, 0, sizeof(_xorCache));

        // 4. Reset internal state
        _isDirty = false; // nvs_erase_all usually automatically commits, but nvs_commit is still recommended

        // According to your code logic: call commit to finalize
        if (nvs_commit(_handle) == ESP_OK)
        {
            _heat *= 0.5f; // Gives a cooling bonus because memory is now free
            return true;
        }
    }

    triggerError(ERR_WRITE_FAILED, 0);
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
    if (!_isValid || !_handle || id >= NeuNVSConstants::MAX_IDS)
        return;

    size_t dataLen = value.length();
    uint16_t newXor = calculateXOR((const uint8_t *)value.c_str(), dataLen);

    // 1. DIRTY CHECK (Fast via RAM)
    if (_xorCache[id] == newXor)
        return;

    // 2. LOCKDOWN PROTECTION
    uint32_t now = safeMillis();
    if (now < _lockdownUntil)
        return;

    // 3. UPDATE HEAT
    uint32_t dt = now - _lastPutTime;
    _heat += (dt < 500) ? (1.0f - (float)dt / 500.0f) * 0.5f : 0;
    if (_heat > _maxHeat)
        _heat = _maxHeat;
    _lastPutTime = now;

    // 4. PREPARE DATA
    char key[NeuNVSConstants::MAX_KEY_LEN];
    get_key(id, key);

    size_t totalSize = sizeof(DataHeader) + dataLen;
    uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_STRING];
    uint8_t *buffer = (totalSize <= sizeof(stackBuf)) ? stackBuf : (uint8_t *)malloc(totalSize);

    if (!buffer)
    {
        triggerError(ERR_ALLOC_FAILED, id);
        return;
    }

    // Arrange the Header complete with Magic Bytes
    DataHeader header = {newXor, (uint16_t)dataLen, 0xA5, 1, 0};
    memcpy(buffer, &header, sizeof(DataHeader));
    memcpy(buffer + sizeof(DataHeader), value.c_str(), dataLen);

    // 5. WRITE TO NVS
    if (nvs_set_blob(_handle, key, buffer, totalSize) == ESP_OK)
    {
        _isDirty = true;
        _xorCache[id] = newXor; // Update cache RAM
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

    uint8_t stackBuf[NeuNVSConstants::STACK_BUFFER_STRING];
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

    // VALIDASI MAGIC BYTE
    if (header.magic != 0xA5)
    {
        triggerError(ERR_DATA_CORRUPT, id);
        if (buffer != stackBuf)
            free(buffer);
        outValue = defaultValue;
        return false;
    }

    uint8_t *dataPtr = buffer + sizeof(DataHeader);
    uint16_t computedXOR = calculateXOR(dataPtr, header.dataSize);

    if (header.xorChecksum != computedXOR)
    {
        triggerError(ERR_DATA_CORRUPT, id);
        if (buffer != stackBuf)
            free(buffer);
        outValue = defaultValue;
        return false;
    }

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

    if (nvs_get_blob(_handle, key, buffer, &storedSize) != ESP_OK)
    {
        Serial.println(F("NeuNVS: Dump read failed!"));
        if (buffer != stackBuf)
            free(buffer);
        return;
    }

    Serial.printf("\n=== NeuNVS Dump ID: %u (%u bytes) ===\n", id, (uint32_t)storedSize);

    if (storedSize >= sizeof(DataHeader))
    {
        DataHeader header;
        memcpy(&header, buffer, sizeof(DataHeader));

        // Show Full Header info
        Serial.printf("Header -> Magic: 0x%02X | Ver: %u | XOR: 0x%04X | Size: %u\n",
                      header.magic, header.version, header.xorChecksum, header.dataSize);

        if (header.magic != 0xA5)
            Serial.println(F("WARNING: Magic Byte mismatch! Data might be corrupt."));

        size_t dataLen = (storedSize > sizeof(DataHeader)) ? (storedSize - sizeof(DataHeader)) : 0;
        uint8_t *dataPtr = buffer + sizeof(DataHeader);

        if (dataLen > 0)
        {
            for (size_t i = 0; i < dataLen; i += byteInLen)
            {
                Serial.printf("[%04u] ", i);
                // Print Hex
                for (size_t j = 0; j < byteInLen; j++)
                {
                    if (i + j < dataLen)
                        Serial.printf("%02X ", dataPtr[i + j]);
                    else
                        Serial.print("   ");
                }

                Serial.print("| ");
                // Print ASCII
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
        else
            Serial.println(F("Body   -> (Empty)"));
    }
    Serial.println(F("==========================================="));

    if (buffer != stackBuf)
        free(buffer);
}

size_t NeuNVS::getTotalFreeEntries()
{
    if (!_isValid)
        return 0;

    nvs_stats_t stats;
    // Use NULL to get total nvs partition statistics
    if (nvs_get_stats(NULL, &stats) == ESP_OK)
        return stats.free_entries;

    return 0;
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuNVS neuNVS;
#endif
