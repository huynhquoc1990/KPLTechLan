#ifndef RS485_MANAGER_H
#define RS485_MANAGER_H

#include <HardwareSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "structdata.h"
#include "TTL.h"

class RS485Manager {
private:
    HardwareSerial *serial;
    QueueHandle_t dataQueue;
    QueueHandle_t logLossQueue;
    
    // Communication state
    bool m_isInitialized;
    unsigned long lastSendTime;
    unsigned long lastReceiveTime;
    
    // Buffer management
    byte receiveBuffer[LOG_SIZE];
    byte sendBuffer[LOG_SIZE];
    
    // Statistics
    struct Stats {
        uint32_t packetsReceived;
        uint32_t packetsSent;
        uint32_t errors;
        uint32_t checksumErrors;
    } stats;
    
public:
    RS485Manager(HardwareSerial *serialPort);
    ~RS485Manager();
    
    // Initialization
    bool begin(int baudRate, int rxPin, int txPin);
    void end();
    // check initialized
    bool isInitialized() const { return m_isInitialized; }
    
    // Data handling
    bool readData(PumpLog &log);
    bool sendData(const byte *data, size_t length);
    bool sendCommand(const char* command);
    
    // Log management
    bool sendLogRequest(uint32_t logId);
    bool sendStartupCommand();
    bool sendSetTimeCommand(const TimeSetup *time);
    
    // Queue management
    bool enqueueLogLoss(uint32_t logId);
    bool dequeueLogLoss(uint32_t &logId);
    int getLogLossQueueSize() const;
    
    // Data processing
    void processReceivedData();
    bool validateData(const byte *data, size_t length);
    bool parseLogData(const byte *data, PumpLog &log);
    
    // Statistics
    const Stats& getStats() const { return stats; }
    void resetStats();
    void printStats();
    
    // Configuration
    void setSendInterval(unsigned long interval);
    void setReceiveTimeout(unsigned long timeout);
    
private:
    // Helper functions
    bool waitForResponse(unsigned long timeout);
    bool calculateChecksum(const byte *data, size_t length, byte &checksum);
    bool verifyChecksum(const byte *data, size_t length, byte expectedChecksum);
    void updateStats(bool success, bool checksumError = false);
    
    // Communication timing
    unsigned long sendInterval;
    unsigned long receiveTimeout;
};

// Global instance
extern RS485Manager* rs485Manager;

// Function declarations for C compatibility
void readRS485Data(byte *buffer);
void sendLogRequest(uint32_t logId);
void sendStartupCommand();
// thêm chương trình gởi lệnh rs485 với id trong LogIdLossQueue để đọc giá trị lên


#endif // RS485_MANAGER_H
