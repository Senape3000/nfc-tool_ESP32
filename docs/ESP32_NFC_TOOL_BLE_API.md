# ESP32-NFC-Tool BLE API Specification

**Version**: 1.0  
**Last Updated**: February 3, 2026  
**Status**: Production Ready  
**Target Clients**: Bruce Firmware, Custom BLE Controllers

---

## 📡 BLE Service Overview

### Service Information
```
Service UUID:    6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Service Name:    Nordic UART Service (NUS)
Device Name:     ESP32-NFC-Tool
```

### Characteristics

#### TX Characteristic (Device → Client)
```
UUID:        6E400003-B5A3-F393-E0A9-E50E24DCCA9E
Properties:  NOTIFY, READ
Direction:   Device sends responses to client
Usage:       Subscribe to receive JSON responses
```

#### RX Characteristic (Client → Device)
```
UUID:        6E400002-B5A3-F393-E0A9-E50E24DCCA9E
Properties:  WRITE, WRITE_NO_RESPONSE, READ
Direction:   Client sends commands to device
Usage:       Write JSON commands to execute operations
```

### Connection Parameters
- **Max MTU**: 512 bytes (auto-negotiated)
- **Max Connections**: 2 simultaneous clients
- **Advertising Interval**: 30-60ms
- **Connection Timeout**: 30 seconds of inactivity
- **Auto-Reconnect**: Enabled (3s interval on client side)

---

## 📨 Communication Protocol

### Command Format

**All commands must be valid JSON objects:**

```json
{
  "cmd": "command_category",
  "params": {
    "action": "specific_action",
    "param1": "value1",
    "param2": "value2"
  },
  "id": "optional_command_id"
}
```

#### Field Descriptions

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `cmd` | string | ✅ Yes | Command category: `wifi`, `nfc`, `system`, `files`, `bt` |
| `params` | object | ✅ Yes | Parameters including required `action` field |
| `params.action` | string | ✅ Yes | Specific action to perform |
| `id` | string | ❌ No | Optional tracking ID (recommended for request/response matching) |

#### Command Category Format

**Two formats supported for backward compatibility:**

1. **Direct Category** (Bruce format - preferred):
   ```json
   {"cmd": "system", "params": {"action": "info"}}
   ```

2. **Prefixed Command** (legacy format):
   ```json
   {"cmd": "system_info", "params": {}}
   ```

---

### Response Format

**All responses are JSON objects:**

```json
{
  "success": true,
  "cmd": "command_name",
  "timestamp": 1234567890,
  "id": "command_id_if_provided",
  "message": "Human readable message",
  "data": {
    // Command-specific response data
  }
}
```

**Error responses include additional fields:**

```json
{
  "success": false,
  "cmd": "command_name",
  "timestamp": 1234567890,
  "id": "command_id_if_provided",
  "error": "Error description",
  "error_code": -1
}
```

#### Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | CMD_SUCCESS | Command completed successfully |
| -1 | CMD_ERROR | Command failed with error |
| -2 | CMD_INVALID | Invalid command format or parameters |
| -3 | CMD_TIMEOUT | Command processing timeout |
| -4 | CMD_NOT_IMPLEMENTED | Command not yet implemented |
| -5 | CMD_NO_DATA | No data available |

---

## 🎯 Available Commands

### 1. System Commands (`system`)

#### Get System Information
**Request:**
```json
{
  "cmd": "system",
  "params": {"action": "info"},
  "id": "sys_info"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "system_info",
  "timestamp": 1234567890,
  "id": "sys_info",
  "message": "System information retrieved",
  "data": {
    "chip_model": "ESP32",
    "chip_revision": 1,
    "cpu_freq": 240,
    "flash_size": 4194304,
    "free_heap": 234567,
    "sketch_size": 1234567,
    "free_sketch_space": 2960000,
    "wifi_connected": true,
    "nfc_ready": true,
    "ble_ready": true,
    "uptime_ms": 123456
  }
}
```

#### Get Heap Information
**Request:**
```json
{
  "cmd": "system",
  "params": {"action": "heap"},
  "id": "heap_info"
}
```

**Response Data:**
- `free_heap`: Current free heap in bytes
- `min_free_heap`: Minimum free heap since boot
- `heap_size`: Total heap size

#### System Restart
**Request:**
```json
{
  "cmd": "system",
  "params": {"action": "restart"},
  "id": "sys_restart"
}
```
⚠️ **Warning**: Device will restart after sending response

---

### 2. Bluetooth Commands (`bt`)

#### Get BLE Status
**Request:**
```json
{
  "cmd": "bt",
  "params": {"action": "status"},
  "id": "bt_status"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "bt_status",
  "timestamp": 1234567890,
  "id": "bt_status",
  "message": "Bluetooth status retrieved",
  "data": {
    "initialized": true,
    "advertising": false,
    "connected": true,
    "connected_count": 1,
    "max_connections": 2,
    "current_mtu": 512,
    "max_mtu": 512,
    "device_name": "ESP32-NFC-Tool",
    "mac_address": "AA:BB:CC:DD:EE:FF",
    "uptime_ms": 123456,
    "commands_received": 42,
    "responses_sent": 42,
    "bytes_transmitted": 12345,
    "bytes_received": 6789
  }
}
```

#### Get BLE Statistics
**Request:**
```json
{
  "cmd": "bt",
  "params": {"action": "stats"},
  "id": "bt_stats"
}
```

#### Reset BLE Statistics
**Request:**
```json
{
  "cmd": "bt",
  "params": {"action": "reset_stats"},
  "id": "reset_stats"
}
```

---

### 3. NFC Commands (`nfc`)

#### Read SRIX Tag
**Request:**
```json
{
  "cmd": "nfc",
  "params": {
    "action": "read_srix",
    "timeout": 10
  },
  "id": "nfc_srix"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "nfc_read_srix",
  "timestamp": 1234567890,
  "id": "nfc_srix",
  "message": "SRIX tag read successfully",
  "data": {
    "uid": "E0040100ABCD1234",
    "protocol": "SRIX",
    "block_count": 64,
    "block_size": 4,
    "dump": "base64_encoded_data_here..."
  }
}
```

**Parameters:**
- `timeout`: Read timeout in seconds (default: 10)

#### Read Mifare UID Only
**Request:**
```json
{
  "cmd": "nfc",
  "params": {
    "action": "mifare_uid",
    "timeout": 5
  },
  "id": "nfc_mf_uid"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "nfc_mifare_uid",
  "timestamp": 1234567890,
  "id": "nfc_mf_uid",
  "message": "Mifare UID read successfully",
  "data": {
    "uid": "04ABCD12345678",
    "uid_length": 7,
    "protocol": "Mifare Classic",
    "sak": "08",
    "atqa": "0004"
  }
}
```

**Parameters:**
- `timeout`: Read timeout in seconds (default: 5)

#### Read Mifare Full Dump
**Request:**
```json
{
  "cmd": "nfc",
  "params": {
    "action": "mifare_read",
    "timeout": 10
  },
  "id": "nfc_mf_read"
}
```

**Response includes full block dump (base64 encoded)**

#### Save Tag Data
**Request:**
```json
{
  "cmd": "nfc",
  "params": {
    "action": "save",
    "filename": "tag_12345678.bin"
  },
  "id": "nfc_save"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "nfc_save",
  "data": {
    "filename": "tag_12345678.bin",
    "protocol": "Mifare Classic"
  },
  "message": "Tag data saved successfully"
}
```

---

### 4. File Commands (`files`)

#### List Files
**Request:**
```json
{
  "cmd": "files",
  "params": {
    "action": "list",
    "protocol": "srix"
  },
  "id": "files_list"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "files_list",
  "timestamp": 1234567890,
  "id": "files_list",
  "message": "File list retrieved",
  "data": {
    "protocol": "srix",
    "files": [
      {"name": "tag_001.bin", "size": 256},
      {"name": "tag_002.bin", "size": 256}
    ]
  }
}
```

**Parameters:**
- `protocol`: Filter by protocol (`srix`, `mifare`)

#### Delete File
**Request:**
```json
{
  "cmd": "files",
  "params": {
    "action": "delete",
    "filename": "tag_001.bin",
    "protocol": "srix"
  },
  "id": "file_delete"
}
```

---

### 5. WiFi Commands (`wifi`)

#### Get WiFi Status
**Request:**
```json
{
  "cmd": "wifi",
  "params": {"action": "status"},
  "id": "wifi_status"
}
```

**Response:**
```json
{
  "success": true,
  "cmd": "wifi_status",
  "data": {
    "connected": true,
    "ssid": "MyNetwork",
    "ip": "192.168.1.100",
    "rssi": -45,
    "gateway": "192.168.1.1",
    "mac": "AA:BB:CC:DD:EE:FF"
  }
}
```

---

## 🔐 Security Features

### SHA-256 Checksum (Automatic)
For responses larger than 1024 bytes, a SHA-256 checksum is automatically added:

```json
{
  "success": true,
  "data": {...},
  "checksum": "a1b2c3d4e5f6...",
  "checksum_size": 4096
}
```

**Verification**: 
1. Extract `checksum` and `checksum_size` from response
2. Remove these fields from JSON
3. Serialize remaining JSON
4. Compute SHA-256 of serialized string
5. Compare with provided checksum

### Connection Activity Tracking
- Each received command updates connection activity timestamp
- Connections idle for > 30 seconds are automatically terminated
- Client should implement heartbeat (e.g., periodic `bt status` commands)

---

## 📊 Error Handling

### Common Error Responses

#### Invalid Command Format
```json
{
  "success": false,
  "cmd": "unknown",
  "error": "Missing command parameter",
  "error_code": -2
}
```

#### Command Timeout
```json
{
  "success": false,
  "cmd": "nfc_read_srix",
  "error": "Timeout: No SRIX tag found",
  "error_code": -3
}
```

#### Not Implemented
```json
{
  "success": false,
  "cmd": "unknown_action",
  "error": "Unknown command category",
  "error_code": -4
}
```

---

## 🧪 Testing & Examples

### Connection Flow (Bruce Firmware Example)

```cpp
// 1. Scan for device
NimBLEScan *scan = NimBLEDevice::getScan();
NimBLEUUID nusUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
NimBLEScanResults results = scan->getResults(6000, false);

// Find device advertising NUS or with name "ESP32-NFC-Tool"
for (int i = 0; i < results.getCount(); i++) {
    auto adv = results.getDevice(i);
    if (adv->isAdvertisingService(nusUUID)) {
        // Found device
    }
}

// 2. Connect
NimBLEClient *client = NimBLEDevice::createClient();
client->connect(device);

// 3. Discover characteristics
auto service = client->getService(nusUUID);
auto txChar = service->getCharacteristic("6E400003-...");
auto rxChar = service->getCharacteristic("6E400002-...");

// 4. Subscribe to TX notifications
txChar->subscribe(true, notifyCallback);

// 5. Send command
String cmd = "{\"cmd\":\"system\",\"params\":{\"action\":\"info\"}}";
rxChar->writeValue(cmd.c_str(), cmd.length(), true);

// 6. Receive response in notifyCallback
void notifyCallback(NimBLERemoteCharacteristic *pChar, 
                    uint8_t *pData, size_t length, bool isNotify) {
    // Parse JSON response
    String response((char*)pData, length);
    // Process response...
}
```

### Response Parsing (Bruce Example)

```cpp
void displayJsonResponse(const String &json) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, json);
    
    bool success = doc["success"] | false;
    const char *message = doc["message"] | "";
    
    Serial.println(success ? "Success" : "Failed");
    if (strlen(message) > 0) {
        Serial.println(message);
    }
    
    if (doc.containsKey("data")) {
        JsonObject data = doc["data"];
        // Display data fields...
    }
}
```

---

## 🎛️ Configuration Constants

### Defined in `src/config.h`

```cpp
// BLE Configuration
#define BT_DEVICE_NAME              "ESP32-NFC-Tool"
#define BT_MAX_CONNECTIONS          2
#define BT_MAX_MTU_SIZE             512
#define BT_CONNECTION_TIMEOUT_MS    30000  // 30 seconds
#define BT_MAX_COMMAND_SIZE         2048
#define BT_JSON_BUFFER_SIZE         2048

// Advertising
#define BT_ADVERTISING_INTERVAL_MIN 48    // 30ms (units of 0.625ms)
#define BT_ADVERTISING_INTERVAL_MAX 96    // 60ms

// NUS UUIDs
#define BT_NUS_SERVICE_UUID    "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BT_NUS_TX_CHAR_UUID    "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define BT_NUS_RX_CHAR_UUID    "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
```

---

## 🔍 Troubleshooting

### Device Not Found
- ✅ Check device is powered on
- ✅ Verify device is advertising (LED should blink)
- ✅ Ensure NUS UUID matches: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- ✅ Try searching by name: "ESP32-NFC-Tool"
- ✅ Check Bluetooth is enabled on client device

### Connection Drops
- ✅ Ensure commands sent at least every 30 seconds
- ✅ Implement heartbeat (e.g., periodic `bt status`)
- ✅ Check client stays within BLE range (~10m)
- ✅ Verify no other device is connected (max 2 connections)

### Commands Return Error
- ✅ Validate JSON format (use JSON validator)
- ✅ Check `cmd` field matches available categories
- ✅ Ensure `params.action` is provided
- ✅ Verify command is implemented (check error_code -4)
- ✅ Check timeout values for NFC operations

### Response Parsing Issues
- ✅ Subscribe to TX characteristic before sending commands
- ✅ Buffer complete JSON (check balanced braces `{}`)
- ✅ Handle multi-packet responses (MTU might split JSON)
- ✅ Verify ArduinoJson version compatibility (v7.4.2+ recommended)

---

## 📚 Integration Checklist

### For Bruce Firmware (or Similar Clients)

- [x] Implement BLE scan for NUS UUID or device name
- [x] Handle connection establishment and MTU negotiation
- [x] Subscribe to TX characteristic for notifications
- [x] Implement JSON command builder
- [x] Implement JSON response parser with brace counting
- [x] Handle connection timeout (reconnect logic)
- [x] Implement heartbeat mechanism (optional but recommended)
- [ ] Test all command categories
- [ ] Verify response display formatting
- [ ] Handle multi-packet responses if MTU < JSON size
- [ ] Implement checksum verification for large transfers

---

## 📞 Support & Issues

### Debugging
Enable verbose logging by setting `ESP_LOG_LEVEL = 6` in `src/logger.h`:
```cpp
#define ESP_LOG_LEVEL 6  // VERBOSE
```

Monitor serial output at 115200 baud to see detailed BLE communication logs.

### Known Limitations
- Maximum 2 simultaneous BLE connections
- NFC operations block other commands during execution
- File transfers limited by MTU size (chunking not yet implemented)
- WiFi scanning not yet implemented via BLE

### Version History
- **v1.0** (Feb 3, 2026) - Initial stable release with full Bruce compatibility

---

**End of API Specification**

For development status and internal implementation details, see `DEVELOPMENT_STATUS.md`.
