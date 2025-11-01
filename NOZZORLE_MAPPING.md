# 🔧 Nozzorle Mapping - UpdatePrice

Giải thích cách map `Nozzorle` từ JSON vào RS485 Device ID

---

## 📋 **JSON Structure (Mới)**

### **Payload format:**
```json
{
  "topic": "11223311A",
  "clientid": "11223311A/GetStatus/QA-T01-V01",
  "message": [
    {
      "Key": "UpdatePrice",
      "item": {
        "IDChiNhanh": "11223311A",
        "IdDevice": "00112211",
        "UnitPrice": 10200,
        "Nozzorle": "13"
      }
    }
  ]
}
```

### **Các field quan trọng:**

| Field | Type | Description | Example |
|-------|------|-------------|---------|
| **IDChiNhanh** | String | Mã số thuế công ty | `"11223311A"` |
| **IdDevice** | String | ID thiết bị/cột bơm | `"00112211"` |
| **UnitPrice** | Number | Giá bán đơn vị (VND) | `10200` |
| **Nozzorle** | String | **RS485 Device ID (11-20)** | `"13"` |

---

## 🎯 **Nozzorle Mapping Logic**

### **Công thức:**
```
RS485 Device ID = Nozzorle (TRỰC TIẾP, không cần convert)
```

### **Bảng mapping:**

| Nozzorle (JSON) | RS485 Device ID | Pump Number |
|----------------|-----------------|-------------|
| `"11"` | `11` | Pump 1 |
| `"12"` | `12` | Pump 2 |
| `"13"` | `13` | Pump 3 |
| `"14"` | `14` | Pump 4 |
| `"15"` | `15` | Pump 5 |
| `"16"` | `16` | Pump 6 |
| `"17"` | `17` | Pump 7 |
| `"18"` | `18` | Pump 8 |
| `"19"` | `19` | Pump 9 |
| `"20"` | `20` | Pump 10 |

---

## 🔄 **Flow xử lý**

```
1. MQTT nhận JSON payload
   ↓
2. Parse "Nozzorle" field (string)
   ↓
3. Convert to integer: atoi("13") = 13
   ↓
4. Validate: 11 ≤ deviceId ≤ 20
   ↓
5. Queue PriceChangeRequest
   ↓
6. RS485 Task gửi xuống KPL device với ID=13
```

---

## ✅ **Validation Rules**

### **1. Nozzorle bắt buộc:**
```cpp
if (strlen(nozzorle) == 0) {
  Serial.printf("[MQTT] Error: Missing Nozzorle field\n");
  // Skip this entry
}
```

### **2. Device ID phải trong range 11-20:**
```cpp
uint8_t deviceIdNum = atoi(nozzorle); // Dùng trực tiếp

if (deviceIdNum < 11 || deviceIdNum > 20) {
  Serial.printf("[MQTT] Invalid Nozzorle: %s (must be 11-20)\n", nozzorle);
  // Skip this entry
}
```

### **3. IDChiNhanh phải khớp với MST:**
```cpp
if (strcmp(idChiNhanh, companyInfo.Mst) != 0) {
  // Skip - not for this company
}
```

---

## 📊 **Ví dụ xử lý**

### **Example 1: Valid payload**
```json
{
  "item": {
    "IDChiNhanh": "11223311A",
    "IdDevice": "00112211",
    "UnitPrice": 10200,
    "Nozzorle": "13"
  }
}
```

**Processing:**
```
Nozzorle = "13"
→ deviceIdNum = 13 (dùng trực tiếp)
→ ✓ Valid (13 is in range 11-20)
→ Send to RS485: [09][0D][010200][CS][0A]
```

---

### **Example 2: Multiple pumps**
```json
{
  "message": [
    { "item": { "Nozzorle": "11", "UnitPrice": 10000 } },
    { "item": { "Nozzorle": "15", "UnitPrice": 12500 } },
    { "item": { "Nozzorle": "20", "UnitPrice": 15000 } }
  ]
}
```

**Processing:**
```
Entry 1: Nozzorle="11" → DeviceID=11 → Price=10000
Entry 2: Nozzorle="15" → DeviceID=15 → Price=12500
Entry 3: Nozzorle="20" → DeviceID=20 → Price=15000

All queued for RS485 task (processed every 300ms)
```

---

### **Example 3: Invalid Nozzorle**
```json
{
  "item": {
    "Nozzorle": "25",
    "UnitPrice": 10000
  }
}
```

**Processing:**
```
Nozzorle = "25"
→ deviceIdNum = 25
→ ✗ Invalid (25 > 20, out of range)
→ Skipped with error log
```

---

## 🚨 **Error Handling**

### **1. Missing Nozzorle:**
```
[MQTT] Error: Missing Nozzorle field for IdDevice=00112211, skipping...
```

### **2. Invalid Nozzorle value:**
```
[MQTT] Invalid Nozzorle: 99 (deviceId=109, must be 11-20), skipping...
```

### **3. Wrong company:**
```
[MQTT] Skipping - IDChiNhanh=99999999 doesn't match MST=11223311A
```

---

## 🔧 **Code Implementation**

### **Parse Nozzorle:**
```cpp
const char* nozzorle = item["Nozzorle"] | "";

// Check if Nozzorle is provided
if (strlen(nozzorle) == 0) {
  Serial.printf("[MQTT] Error: Missing Nozzorle field\n");
  continue;
}

// Parse to integer (Nozzorle là RS485 Device ID trực tiếp)
uint8_t deviceIdNum = atoi(nozzorle);

// Validate range
if (deviceIdNum < 11 || deviceIdNum > 20) {
  Serial.printf("[MQTT] Invalid Nozzorle: %s (must be 11-20)\n", nozzorle);
  continue;
}
```

---

## 📌 **So sánh: Cũ vs Mới**

### **❌ Cũ (parse từ IdDevice):**
```json
{
  "IdDevice": "QA-T01-V03",
  "UnitPrice": 10000
}
```
- Parse `"V03"` → `pumpNumber = 3` → `deviceId = 13`
- **Nhược điểm**: Phụ thuộc format `IdDevice`

### **✅ Mới (dùng Nozzorle):**
```json
{
  "IdDevice": "00112211",
  "Nozzorle": "13",
  "UnitPrice": 10000
}
```
- Dùng trực tiếp `"13"` → `deviceId = 13`
- **Ưu điểm**: Rõ ràng, không phụ thuộc format, không cần convert

---

## 🎯 **Testing**

### **Test với MQTT Explorer:**
```json
Topic: 11223311A/UpdatePrice

Payload:
{
  "topic": "11223311A",
  "clientid": "11223311A/GetStatus/QA-T01-V01",
  "message": [
    {
      "Key": "UpdatePrice",
      "item": {
        "IDChiNhanh": "11223311A",
        "IdDevice": "00112211",
        "UnitPrice": 10200,
        "Nozzorle": "13"
      }
    }
  ]
}
```

### **Expected Serial Output (Debug mode):**
```
[MQTT] UpdatePrice command received - parsing payload...
[MQTT] ✅ Processing Entry: IDChiNhanh=11223311A, IdDevice=00112211, Nozzorle=13
[MQTT] UnitPrice=10200.00
[MQTT] Nozzorle=13 -> RS485 DeviceID=13
[MQTT] ✓ Queued price change: IdDevice=00112211 -> PumpID=13, Price=10200.00
[RS485 CMD] Sending: [09][0D][010200][XX][0A]
[RS485 READ] ✓ SUCCESS - DeviceID=13 price updated successfully
```

---

**Tác giả:** QuocAnh  
**Ngày cập nhật:** 2025-10-31  
**Version:** 2.0

