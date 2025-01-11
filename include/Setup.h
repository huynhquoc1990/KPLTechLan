#include <Arduino.h>
#include <LittleFS.h>

/// @brief Format thông tin Flash
void formatLittleFS()
{
  if (LittleFS.format())
  {
    Serial.println("LittleFS formatted successfully");
  }
  else
  {
    Serial.println("Failed to format LittleFS");
  }
}


