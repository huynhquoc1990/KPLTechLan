// ============================================================================
// TTL FIRMWARE CHECKSUM VERIFICATION CODE
// ============================================================================
// Thêm code này vào readRS485Data() để verify TTL firmware checksum calculation
// 
// CÁCH DÙNG:
// 1. Uncomment code dưới đây trong readRS485Data()
// 2. Build và deploy
// 3. Monitor serial output để xem TTL firmware tính checksum có đúng không
// ============================================================================

// Case 2: Pump Log Data - Inside the valid packet check
if (firstByte == 1 && Serial2.available() >= LOG_SIZE)
{
  size_t bytesRead = Serial2.readBytes(buffer, LOG_SIZE);
  
  if (bytesRead == LOG_SIZE)
  {
    // ============================================================================
    // VERIFICATION CODE - ADD THIS BEFORE VALIDATION
    // ============================================================================
    
    // Only verify packets with correct header and footer structure
    if (buffer[0] == 1 && buffer[1] == 2 && buffer[31] == 4)
    {
      // Calculate checksum WITHOUT footer (OLD way - bug)
      uint8_t checksumOld = 0xA5;
      for (size_t i = 2; i < 29; i++) {  // Stops at byte 28
        checksumOld ^= buffer[i];
      }
      
      // Calculate checksum WITH footer (NEW way - fixed)
      uint8_t checksumNew = 0xA5;
      for (size_t i = 2; i < 30; i++) {  // Includes byte 29
        checksumNew ^= buffer[i];
      }
      
      uint8_t checksumReceived = buffer[30];
      uint8_t footerByte = buffer[29];
      
      // Log analysis results
      Serial.println("\n========================================");
      Serial.println("TTL CHECKSUM VERIFICATION:");
      Serial.println("========================================");
      Serial.printf("Received Checksum:  0x%02X\n", checksumReceived);
      Serial.printf("Old Calc (no ft):   0x%02X %s\n", 
                    checksumOld, 
                    (checksumOld == checksumReceived ? "✓ MATCH" : "✗ NO MATCH"));
      Serial.printf("New Calc (w/ ft):   0x%02X %s\n", 
                    checksumNew, 
                    (checksumNew == checksumReceived ? "✓ MATCH" : "✗ NO MATCH"));
      Serial.printf("Footer Byte [29]:   0x%02X (expect 0x03)\n", footerByte);
      Serial.println("----------------------------------------");
      
      // Verdict
      if (checksumOld == checksumReceived && checksumNew != checksumReceived)
      {
        Serial.println("VERDICT: TTL firmware BUGGY");
        Serial.println("  → TTL does NOT include footer in checksum");
        Serial.println("  → ESP32 fix must be ROLLED BACK");
        Serial.println("  → OR update TTL firmware first");
      }
      else if (checksumNew == checksumReceived && checksumOld != checksumReceived)
      {
        Serial.println("VERDICT: TTL firmware CORRECT");
        Serial.println("  → TTL includes footer in checksum");
        Serial.println("  → ESP32 fix is CORRECT, keep it");
      }
      else if (checksumOld == checksumReceived && checksumNew == checksumReceived)
      {
        Serial.println("VERDICT: INCONCLUSIVE (footer = 0xA5?)");
        Serial.println("  → Footer byte happens to make both checksums equal");
        Serial.println("  → Need more packets to verify");
      }
      else
      {
        Serial.println("VERDICT: BOTH WRONG");
        Serial.println("  → Neither calculation matches");
        Serial.println("  → Packet may be corrupt");
      }
      
      Serial.println("========================================\n");
    }
    
    // ============================================================================
    // END VERIFICATION CODE
    // ============================================================================
    
    // Continue with normal validation...
    uint8_t calculatedChecksum = calculateChecksum_LogData(buffer, LOG_SIZE);
    uint8_t receivedChecksum = buffer[30];
    
    // ... rest of existing code ...
  }
}

