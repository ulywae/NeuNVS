#line 1 "C:\\Users\\ulywa\\OneDrive\\Desktop\\makeLibESP\\SecureEEPROM.cpp"
#include "SecureEEPROM.h"

uint8_t SecureEEPROM::_instanceCount = 0;

SecureEEPROM::SecureEEPROM() : _lastCommitTime(0), _interval(1000), _isDirty(false),
                               _commitCount(0), _lockdownUntil(0),
                               _maxCommits(5), _lockdownDuration(5000)
{
    // Otomatis buat nama namespace unik: ns0, ns1, ns2...
    snprintf(_currentNS, sizeof(_currentNS), "ns%u", _instanceCount);

    // Batasi agar tidak melebihi limit NVS (Max 254 namespace)
    if (_instanceCount < 254)
        _instanceCount++;
}

bool SecureEEPROM::begin(uint32_t intervalMs, uint32_t lockSec)
{
    _interval = intervalMs;
    _lockdownDuration = lockSec * 1000;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Membuka namespace unik yang sudah dibuat di konstruktor
    return nvs_open(_currentNS, NVS_READWRITE, &_handle) == ESP_OK;
}

void SecureEEPROM::get_key(uint8_t id, char *keyOut)
{
    snprintf(keyOut, 6, "i%u", id);
}

bool SecureEEPROM::exists(uint8_t id)
{
    char key[6];
    get_key(id, key);
    size_t size = 0;

    // Kita hanya cek apakah key-nya ada.
    // Jika ada, nvs akan memberikan ESP_OK atau error ukuran (yang berarti key ditemukan).
    // Jika tidak ada sama sekali, nvs akan memberikan ESP_ERR_NVS_NOT_FOUND.
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &size);

    return (err != ESP_ERR_NVS_NOT_FOUND);
}

size_t SecureEEPROM::freeEntries()
{
    nvs_stats_t nvs_stats;
    esp_err_t err = nvs_get_stats(NULL, &nvs_stats);
    if (err == ESP_OK)
    {
        return nvs_stats.free_entries;
    }
    return 0; // Gagal mengambil data
}

uint16_t SecureEEPROM::calculateXOR(const uint8_t *data, size_t len)
{
    uint16_t xorResult = 0xAA;
    for (size_t i = 0; i < len; i++)
        xorResult ^= data[i];

    return xorResult;
}

void SecureEEPROM::onError(EEPROMErrorCallback callback)
{
    _errorCallback = callback;
}

void SecureEEPROM::triggerError(uint8_t code, uint8_t id)
{
    if (_errorCallback)
        _errorCallback(code, id);
}

void SecureEEPROM::update()
{
    if (!_isDirty || millis() < _lockdownUntil)
        return;

    if (millis() - _lastCommitTime >= _interval)
        commit();
}

bool SecureEEPROM::commit()
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

bool SecureEEPROM::isLocked()
{
    return millis() < _lockdownUntil;
}

// Menghapus satu ID spesifik
bool SecureEEPROM::remove(uint8_t id)
{
    if (millis() < _lockdownUntil)
        return false;

    char key[6];
    get_key(id, key);

    esp_err_t err = nvs_erase_key(_handle, key);
    if (err == ESP_OK)
    {
        _isDirty = true; // Tandai agar perlu di-commit
        return true;
    }
    return false;
}

// Menghapus SELURUH data dalam namespace internal
bool SecureEEPROM::clearAll()
{
    if (millis() < _lockdownUntil)
        return false;

    esp_err_t err = nvs_erase_all(_handle);
    if (err == ESP_OK)
    {
        _isDirty = true;
        return commit(); // Langsung commit untuk membersihkan fisik flash
    }
    return false;
}

void SecureEEPROM::dump(uint8_t id)
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
SecureEEPROM EEPROM;
#endif
