#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>
#include "structdata.h"

class SystemManager {
private:
    // System state
    bool m_isInitialized;
    bool m_isRunning;
    unsigned long startTime;
    unsigned long lastCheckTime;
    
    // Task management
    TaskHandle_t rs485TaskHandle;
    TaskHandle_t mqttTaskHandle;
    TaskHandle_t wifiTaskHandle;
    TaskHandle_t webServerTaskHandle;
    
    // System resources
    SemaphoreHandle_t systemMutex;
    SemaphoreHandle_t flashMutex;
    
    // System data
    DeviceStatus deviceStatus;
    CompanyInfo companyInfo;
    Settings settings;
    TimeSetup timeSetup;
    
    // Statistics
    struct SystemStats {
        uint32_t totalRestarts;
        uint32_t memoryLeaks;
        uint32_t taskFailures;
        float averageHeapUsage;
        float averageTemperature;
    } stats;
    
public:
    SystemManager();
    ~SystemManager();
    
    // Initialization
    bool initialize();
    void shutdown();
    bool isInitialized() const { return m_isInitialized; }
    bool isRunning() const { return m_isRunning; }
    
    // Task management
    bool createTasks();
    void suspendTasks();
    void resumeTasks();
    void deleteTasks();
    
    // System monitoring
    void checkSystemHealth();
    void monitorMemory();
    void monitorTasks();
    void monitorTemperature();
    
    // System control
    void restart();
    void factoryReset();
    void enterSleepMode();
    void exitSleepMode();
    
    // Data management
    bool loadSystemData();
    bool saveSystemData();
    bool backupData();
    bool restoreData();
    
    // Configuration
    void loadConfiguration();
    void saveConfiguration();
    void resetConfiguration();
    
    // Statistics and logging
    const SystemStats& getStats() const { return stats; }
    void logEvent(const char* event, const char* details = nullptr);
    void printSystemInfo();
    
    // Getters
    DeviceStatus* getDeviceStatus() { return &deviceStatus; }
    CompanyInfo* getCompanyInfo() { return &companyInfo; }
    Settings* getSettings() { return &settings; }
    TimeSetup* getTimeSetup() { return &timeSetup; }
    
    // Mutex access
    SemaphoreHandle_t getSystemMutex() const { return systemMutex; }
    SemaphoreHandle_t getFlashMutex() const { return flashMutex; }
    
private:
    // Helper functions
    bool initializeFreeRTOS();
    bool initializeFileSystem();
    bool initializeHardware();
    bool initializeNetwork();
    
    void updateStats();
    void checkTaskHealth(TaskHandle_t taskHandle, const char* taskName);
    void handleSystemError(const char* error);
    
    // System timing
    unsigned long healthCheckInterval;
    unsigned long memoryCheckInterval;
    unsigned long statsUpdateInterval;
};

// Global instance
extern SystemManager* systemManager;

// Function declarations for C compatibility
void systemInit();
void systemCheck();
void checkHeap();
void setupTime();

#endif // SYSTEM_MANAGER_H
