#include "NeuNVS.h"

uint8_t NeuNVS::_instanceCount = 0;

NeuNVS::NeuNVS() : _lastCommitTime(0), _interval(1000), _isDirty(false),
                   _commitCount(0), _lockdownUntil(0),
                   _maxCommits(5), _lockdownDuration(5000)
{
    // Automatically create unique namespace names: ns0, ns1, ns2...
    snprintf(_currentNS, sizeof(_currentNS), "ns%u", _instanceCount);

    // Limit to not exceed NVS limit (Max 254 namespace)
    if (_instanceCount < 254)
        _instanceCount++;
}

bool NeuNVS::begin(uint32_t intervalMs, uint32_t lockSec)
{
    _interval = intervalMs;
    _lockdownDuration = lockSec * 1000;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Opens the unique namespace that was created in the constructor
    return nvs_open(_currentNS, NVS_READWRITE, &_handle) == ESP_OK;
}

void NeuNVS::get_key(uint8_t id, char *keyOut)
{
    snprintf(keyOut, 6, "i%u", id);
}

bool NeuNVS::exists(uint8_t id)
{
    char key[6];
    get_key(id, key);
    size_t size = 0;

    // We just check if the key exists.
    // If it exists, nvs will return ESP_OK or a size error (which means the key was found).
    // If it doesn't exist at all, nvs will return ESP_ERR_NVS_NOT_FOUND.
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &size);

    return (err != ESP_ERR_NVS_NOT_FOUND);
}

size_t NeuNVS::freeEntries()
{
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_OK)
    {
        return nvs_stats.free_entries;
    }
    return 0; // Failed to fetch data
}

uint16_t NeuNVS::calculateXOR(const uint8_t *data, size_t len)
{
    uint16_t xorResult = 0xAA;
    for (size_t i = 0; i < len; i++)
        xorResult ^= data[i];

    return xorResult;
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
    if (!_isDirty || millis() < _lockdownUntil)
        return;

    if (millis() - _lastCommitTime >= _interval)
        commit();
}

bool NeuNVS::commit()
{
    if (!_isDirty)
        return true;

    uint32_t now = millis();

    if (now < _lockdownUntil)
    {
        triggerError(ERR_LOCKDOWN, 0); // 1: ERR_LOCKDOWN, 0: System ID
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
        triggerError(ERR_WRITE_FAILED, 0); // 2: ERR_WRITE_FAILED
        return false;
    }

    _lastCommitTime = now;
    _isDirty = false;
    return true;
}

bool NeuNVS::isLocked()
{
    return millis() < _lockdownUntil;
}

// Delete one specific ID
bool NeuNVS::remove(uint8_t id)
{
    if (millis() < _lockdownUntil)
        return false;

    char key[6];
    get_key(id, key);

    esp_err_t err = nvs_erase_key(_handle, key);
    if (err == ESP_OK)
    {
        _isDirty = true; // Mark as need to be committed
        return true;
    }
    return false;
}

// Delete ALL data in the internal namespace
bool NeuNVS::clearAll()
{
    if (millis() < _lockdownUntil)
        return false;

    esp_err_t err = nvs_erase_all(_handle);
    if (err == ESP_OK)
    {
        _isDirty = true;
        return commit(); // Directly commit to clean physical flash
    }
    return false;
}

void NeuNVS::putString(uint8_t id, const String &value)
{
    if (millis() < _lockdownUntil)
        return;

    char key[6];
    get_key(id, key);
    const char *strData = value.c_str();
    size_t len = value.length();

    // Dirty Check: Take old data and compare the text
    size_t existingSize = 0;
    nvs_get_blob(_handle, key, NULL, &existingSize);

    if (existingSize == len)
    {
        char buffer[len + 1];
        nvs_get_blob(_handle, key, buffer, &existingSize);
        buffer[len] = '\0';
        if (strcmp(buffer, strData) == 0)
            return; // Data identic, ignore
    }

    // Store String as raw BLOB (Without XOR Header for flexibility)
    if (nvs_set_blob(_handle, key, strData, len) == ESP_OK)
    {
        _isDirty = true;
    }
}

String NeuNVS::getString(uint8_t id, const String &defaultValue)
{
    char key[6];
    get_key(id, key);

    size_t required_size = 0;
    if (nvs_get_blob(_handle, key, NULL, &required_size) != ESP_OK)
        return defaultValue;

    char buffer[required_size + 1];
    nvs_get_blob(_handle, key, buffer, &required_size);
    buffer[required_size] = '\0';

    return String(buffer);
}

void NeuNVS::dump(uint8_t id)
{
    char key[6];
    get_key(id, key);

    size_t storedSize = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &storedSize);

    if (err != ESP_OK)
    {
        Serial.printf("ID %u: Kosong atau tidak ditemukan.\n", id);
        return;
    }

    uint8_t buffer[storedSize];
    nvs_get_blob(_handle, key, buffer, &storedSize);

    Serial.printf("Hex Dump ID %u (%u bytes):\n", id, storedSize);
    Serial.print("Header -> ");
    for (size_t i = 0; i < storedSize; i++)
    {
        if (i < sizeof(DataHeader))
        {
            Serial.printf("%02X ", buffer[i]);
            if (i == sizeof(DataHeader) - 1)
                Serial.print("| Data -> ");
        }
        else
            Serial.printf("%02X ", buffer[i]);
    }
    Serial.println("\n-------------------------------------------");
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
NeuNVS NVS;
#endif
