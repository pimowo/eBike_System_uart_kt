/*
 * KT_UART_Tester
 * Projekt do testowania komunikacji UART ze sterownikiem KT
 * Autor: pimowo
 * Data: 2025-05-25
 */

#include <Arduino.h>

// Konfiguracja pinów UART
#define KT_RX_PIN 16 // ESP32 RX (połącz z TX sterownika)
#define KT_TX_PIN 17 // ESP32 TX (połącz z RX sterownika)

// Parametry komunikacji
#define KT_UART_BAUD 1200 // Prędkość komunikacji - sprawdź w dokumentacji sterownika
#define SERIAL_DEBUG_BAUD 115200 // Prędkość dla Serial Monitor

// Komendy protokołu KT
#define KT_START_BYTE 0x3A
#define KT_CMD_READ 0x11
#define KT_CMD_WRITE 0x16

// Typy parametrów
#define KT_PARAM_P 1
#define KT_PARAM_C 2
#define KT_PARAM_L 3

// Zmienne globalne
bool logEnabled = true; // Czy logować szczegóły komunikacji

// Obliczanie sumy kontrolnej
uint8_t calculateChecksum(uint8_t* data, int length) {
  uint8_t sum = 0;
  for (int i = 0; i < length; i++) {
    sum += data[i];
  }
  return sum & 0xFF; // Upewnij się, że zwracasz tylko 1 bajt
}

// Wysyłanie komendy odczytu parametru
void sendReadCommand(uint8_t paramType, uint8_t paramNumber) {
  if (paramType < 1 || paramType > 3 || paramNumber < 1 || 
      (paramType == KT_PARAM_P && paramNumber > 5) || 
      (paramType == KT_PARAM_C && paramNumber > 15) ||
      (paramType == KT_PARAM_L && paramNumber > 3)) {
    Serial.println("Błędny parametr lub numer parametru!");
    return;
  }

  uint8_t data[4];
  data[0] = KT_START_BYTE; // Start byte
  data[1] = KT_CMD_READ; // Komenda odczytu
  data[2] = ((paramType & 0x0F) << 4) | (paramNumber & 0x0F); // Typ i numer parametru
  data[3] = calculateChecksum(data, 3); // Suma kontrolna
  
  // Wyślij dane przez UART do sterownika
  Serial2.write(data, 4);
  
  // Log wysyłanej komendy
  if (logEnabled) {
    Serial.print("TX [READ]: ");
    for (int i = 0; i < 4; i++) {
      Serial.print("0x");
      if (data[i] < 0x10) Serial.print("0");
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.print(" | Param: ");
    
    // Wyświetl typ parametru jako literę
    if (paramType == KT_PARAM_P) Serial.print("P");
    else if (paramType == KT_PARAM_C) Serial.print("C");
    else if (paramType == KT_PARAM_L) Serial.print("L");
    
    Serial.println(paramNumber);
  }
}

// Wysyłanie komendy zapisu parametru
void sendWriteCommand(uint8_t paramType, uint8_t paramNumber, uint8_t value) {
  if (paramType < 1 || paramType > 3 || paramNumber < 1 || 
      (paramType == KT_PARAM_P && paramNumber > 5) || 
      (paramType == KT_PARAM_C && paramNumber > 15) ||
      (paramType == KT_PARAM_L && paramNumber > 3)) {
    Serial.println("Błędny parametr lub numer parametru!");
    return;
  }

  uint8_t data[5];
  data[0] = KT_START_BYTE; // Start byte
  data[1] = KT_CMD_WRITE; // Komenda zapisu
  data[2] = ((paramType & 0x0F) << 4) | (paramNumber & 0x0F); // Typ i numer parametru
  data[3] = value; // Wartość parametru
  data[4] = calculateChecksum(data, 4); // Suma kontrolna
  
  // Wyślij dane przez UART do sterownika
  Serial2.write(data, 5);
  
  // Log wysyłanej komendy
  if (logEnabled) {
    Serial.print("TX [WRITE]: ");
    for (int i = 0; i < 5; i++) {
      Serial.print("0x");
      if (data[i] < 0x10) Serial.print("0");
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.print(" | Param: ");
    
    // Wyświetl typ parametru jako literę
    if (paramType == KT_PARAM_P) Serial.print("P");
    else if (paramType == KT_PARAM_C) Serial.print("C");
    else if (paramType == KT_PARAM_L) Serial.print("L");
    
    Serial.print(paramNumber);
    Serial.print(" = ");
    Serial.println(value);
  }
}

// Obsługa odebranych danych ze sterownika
void processReceivedData() {
  static uint8_t buffer[10]; // Bufor na odebrane dane
  static uint8_t bufferIndex = 0; // Aktualny indeks bufora
  static unsigned long lastByteTime = 0; // Czas ostatnio odebranego bajtu
  const unsigned long TIMEOUT = 100; // Timeout w ms
  
  // Reset bufora po timeoucie
  if (bufferIndex > 0 && millis() - lastByteTime > TIMEOUT) {
    if (logEnabled && bufferIndex > 1) { // Tylko loguj jeśli rzeczywiście coś było w buforze
      Serial.print("Timeout - reset bufora [");
      Serial.print(bufferIndex);
      Serial.println(" bajtów]");
    }
    bufferIndex = 0;
  }
  
  // Odczyt danych z UART
  while (Serial2.available()) {
    uint8_t byte = Serial2.read();
    lastByteTime = millis();
    
    // Jeśli to pierwszy bajt i nie jest to bajt startowy, ignoruj
    if (bufferIndex == 0 && byte != KT_START_BYTE) {
      if (logEnabled) {
        Serial.print("Ignoruję bajt: 0x");
        if (byte < 0x10) Serial.print("0");
        Serial.println(byte, HEX);
      }
      continue;
    }
    
    // Dodaj do bufora
    buffer[bufferIndex++] = byte;
    
    // Szczegółowy log każdego bajtu
    if (logEnabled) {
      Serial.print("RX[");
      Serial.print(bufferIndex - 1);
      Serial.print("]: 0x");
      if (byte < 0x10) Serial.print("0");
      Serial.println(byte, HEX);
    }
    
    // Sprawdź czy mamy kompletny pakiet odpowiedzi
    if (bufferIndex >= 4) { // Minimalny rozmiar pakietu to 4 bajty
      // Sprawdźmy jaki rodzaj pakietu otrzymaliśmy
      uint8_t command = buffer[1];
      
      int expectedLength = 0;
      if (command == KT_CMD_READ) {
        expectedLength = 5; // Start + Cmd + ParamInfo + Value + Checksum
      } else if (command == KT_CMD_WRITE) {
        expectedLength = 4; // Start + Cmd + ParamInfo + Checksum
      } else {
        // Nieznana komenda, spróbujmy założyć że to 4 bajty
        expectedLength = 4;
      }
      
      // Sprawdź czy mamy wszystkie dane
      if (bufferIndex >= expectedLength) {
        // Loguj cały pakiet
        if (logEnabled) {
          Serial.print("RX [Pełny pakiet]: ");
          for (int i = 0; i < bufferIndex; i++) {
            Serial.print("0x");
            if (buffer[i] < 0x10) Serial.print("0");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
          }
          Serial.println();
          
          // Interpretacja pakietu
          uint8_t paramInfo = buffer[2];
          uint8_t paramType = (paramInfo >> 4) & 0x0F;
          uint8_t paramNumber = paramInfo & 0x0F;
          
          Serial.print("Komenda: 0x");
          if (command < 0x10) Serial.print("0");
          Serial.print(command, HEX);
          
          // Wyświetl typ parametru jako literę
          Serial.print(" | Param: ");
          if (paramType == KT_PARAM_P) Serial.print("P");
          else if (paramType == KT_PARAM_C) Serial.print("C");
          else if (paramType == KT_PARAM_L) Serial.print("L");
          else Serial.print("?");
          Serial.print(paramNumber);
          
          // Jeśli to komenda odczytu, pokaż wartość
          if (command == KT_CMD_READ) {
            uint8_t value = buffer[3];
            Serial.print(" | Wartość: ");
            Serial.print(value);
            Serial.print(" (0x");
            if (value < 0x10) Serial.print("0");
            Serial.print(value, HEX);
            Serial.print(")");
          }
          
          // Sprawdź sumę kontrolną
          uint8_t expectedChecksum = calculateChecksum(buffer, bufferIndex - 1);
          uint8_t receivedChecksum = buffer[bufferIndex - 1];
          
          Serial.print(" | Suma kontr.: 0x");
          if (receivedChecksum < 0x10) Serial.print("0");
          Serial.print(receivedChecksum, HEX);
          
          if (expectedChecksum == receivedChecksum) {
            Serial.println(" [Poprawna]");
          } else {
            Serial.print(" [Błędna - oczekiwano 0x");
            if (expectedChecksum < 0x10) Serial.print("0");
            Serial.print(expectedChecksum, HEX);
            Serial.println("]");
          }
          
          Serial.println("----------------------------------------");
        }
        
        // Reset bufora po przetworzeniu pakietu
        bufferIndex = 0;
      }
    }
    
    // Zabezpieczenie przed przepełnieniem bufora
    if (bufferIndex >= sizeof(buffer)) {
      Serial.println("Przepełnienie bufora! Reset.");
      bufferIndex = 0;
    }
  }
}

// Wyświetlanie pomocy w konsoli
void showHelp() {
  Serial.println("\n=== KT_UART_Tester - Komendy ===");
  Serial.println("read [p|c|l][numer] - Odczyt parametru, np. 'read p1'");
  Serial.println("write [p|c|l][numer] [wartość] - Zapis parametru, np. 'write p1 42'");
  Serial.println("readall - Odczyt wszystkich parametrów (P1-P5, C1-C15, L1-L3)");
  Serial.println("log [on|off] - Włącz/wyłącz logowanie szczegółów");
  Serial.println("help - Wyświetla tę pomoc");
  Serial.println("================================\n");
}

// Odczyt wszystkich parametrów
void readAllParameters() {
  Serial.println("Odczytuję wszystkie parametry...");
  
  // Parametry P
  for (int i = 1; i <= 5; i++) {
    sendReadCommand(KT_PARAM_P, i);
    delay(200); // Daj czas na odpowiedź
  }
  
  // Parametry C
  for (int i = 1; i <= 15; i++) {
    sendReadCommand(KT_PARAM_C, i);
    delay(200);
  }
  
  // Parametry L
  for (int i = 1; i <= 3; i++) {
    sendReadCommand(KT_PARAM_L, i);
    delay(200);
  }
  
  Serial.println("Odczyt zakończony.");
}

// Obsługa komend z Serial Monitor
void handleSerialCommands() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command.startsWith("read ")) {
      // Komenda odczytu parametru
      String paramStr = command.substring(5);
      paramStr.trim();
      
      if (paramStr.length() >= 2) {
        char paramType = paramStr.charAt(0);
        int paramNumber = paramStr.substring(1).toInt();
        
        uint8_t typeCode = 0;
        if (paramType == 'p' || paramType == 'P') typeCode = KT_PARAM_P;
        else if (paramType == 'c' || paramType == 'C') typeCode = KT_PARAM_C;
        else if (paramType == 'l' || paramType == 'L') typeCode = KT_PARAM_L;
        
        if (typeCode > 0 && paramNumber > 0) {
          sendReadCommand(typeCode, paramNumber);
        } else {
          Serial.println("Błędny format parametru. Przykład: 'read p1'");
        }
      } else {
        Serial.println("Niepoprawny format. Przykład: 'read p1'");
      }
    }
    else if (command.startsWith("write ")) {
      // Komenda zapisu parametru
      String rest = command.substring(6);
      rest.trim();
      
      int spaceIndex = rest.indexOf(' ');
      if (spaceIndex > 0) {
        String paramStr = rest.substring(0, spaceIndex);
        String valueStr = rest.substring(spaceIndex + 1);
        
        if (paramStr.length() >= 2 && valueStr.length() > 0) {
          char paramType = paramStr.charAt(0);
          int paramNumber = paramStr.substring(1).toInt();
          int value = valueStr.toInt();
          
          uint8_t typeCode = 0;
          if (paramType == 'p' || paramType == 'P') typeCode = KT_PARAM_P;
          else if (paramType == 'c' || paramType == 'C') typeCode = KT_PARAM_C;
          else if (paramType == 'l' || paramType == 'L') typeCode = KT_PARAM_L;
          
          if (typeCode > 0 && paramNumber > 0 && value >= 0 && value <= 255) {
            sendWriteCommand(typeCode, paramNumber, value);
          } else {
            Serial.println("Błędny parametr lub wartość. Przykład: 'write p1 42'");
          }
        } else {
          Serial.println("Niepoprawny format. Przykład: 'write p1 42'");
        }
      } else {
        Serial.println("Niepoprawny format. Przykład: 'write p1 42'");
      }
    }
    else if (command == "readall") {
      readAllParameters();
    }
    else if (command == "log on") {
      logEnabled = true;
      Serial.println("Logowanie włączone");
    }
    else if (command == "log off") {
      logEnabled = false;
      Serial.println("Logowanie wyłączone");
    }
    else if (command == "help") {
      showHelp();
    }
    else {
      Serial.println("Nieznana komenda. Wpisz 'help' aby wyświetlić dostępne komendy.");
    }
  }
}

void setup() {
  // Inicjalizacja UART dla Serial Monitor
  Serial.begin(SERIAL_DEBUG_BAUD);
  delay(500); // Daj czas na stabilizację

  // Wyczyść bufor i wyświetl nagłówek
  Serial.println("\n\n");
  Serial.println("===================================");
  Serial.println(" KT Sterownik - Tester UART");
  Serial.println("===================================");
  
  // Inicjalizacja UART dla sterownika KT
  Serial2.begin(KT_UART_BAUD, SERIAL_8N1, KT_RX_PIN, KT_TX_PIN);
  
  Serial.println("UART zainicjalizowany:");
  Serial.print("- RX pin: ");
  Serial.println(KT_RX_PIN);
  Serial.print("- TX pin: ");
  Serial.println(KT_TX_PIN);
  Serial.print("- Prędkość: ");
  Serial.print(KT_UART_BAUD);
  Serial.println(" bodów");
  Serial.println("===================================");
  
  showHelp();
}

void loop() {
  // Obsługa komend z Serial Monitor
  handleSerialCommands();
  
  // Odbieranie i przetwarzanie danych ze sterownika
  processReceivedData();
  
  // Małe opóźnienie dla stabilności
  delay(10);
}
