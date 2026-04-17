#include "NimBLEDevice.h"

String targetDeviceAddress = "c0:00:00:00:05:42";
bool bleConnected = false;
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pRemoteChar = nullptr;
bool isTest=false;
int numTest=0;
uint32_t lastConnectAttempt = 0;
const uint32_t reconnectInterval = 8000;

void sendCommand(const uint8_t* data, size_t len, String desc) {
  if (!bleConnected || pRemoteChar == nullptr) {
    Serial.println("❌ BLE не подключено!");
    return;
  }

  bool ok = pRemoteChar->writeValue(const_cast<uint8_t*>(data), len, false);
  return;
  Serial.print(ok ? "✅ " : "❌ ");
  Serial.print(desc + " | ");
  for (size_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print("0");
    Serial.print(String(data[i], HEX) + " ");
  }
  Serial.println();
}

void power(bool on) {
  uint8_t cmd[9] = {0x7B, 0x00, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF};
  sendCommand(cmd, sizeof(cmd), on ? "ВКЛЮЧЕНИЕ" : "ВЫКЛЮЧЕНИЕ");
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7B, 0x00, 0x07, r, g, b, 0xFF, 0xFF, 0xBF};//0x00, 0xFF, 0xBF - тоже самое
  sendCommand(cmd, sizeof(cmd), "Лента цвет");
}

void setBright(uint8_t percent) {
  uint8_t cmd[] = {0x7B, 0x00, 0x01, percent, 0x00, 0xFF, 0xFF, 0xFF, 0xBF};
  sendCommand(cmd, sizeof(cmd), "Лента яркость");
}

void powerRGB(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  sendCommand(cmd, sizeof(cmd), on ? "ВКЛЮЧЕНИЕ" : "ВЫКЛЮЧЕНИЕ");
}

void setColorRGB(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7B, 0x00, 0x0C, r, g, b, 0xFF, 0xFF, 0xBF};
  sendCommand(cmd, sizeof(cmd), "Подстаканники цвет");
}

void setBrightRGB(uint8_t percent) {
  uint8_t cmd[] = {0x7E, 0xFF, 0x01, percent, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  sendCommand(cmd, sizeof(cmd), "Подстаканники яркость");
}

void testCommands(const String& input) {
  int start = 0;
  int cmdNum = 0;

  while (start < input.length()) {
    int end = input.indexOf('\n', start);
    if (end == -1) end = input.length();

    String line = input.substring(start, end);
    line.trim();
    start = end + 1;

    if (line.length() == 0) continue;

    // Парсим байты через запятую
    uint8_t buf[32];
    size_t len = 0;
    int pos = 0;

    while (pos < line.length() && len < 32) {
      int comma = line.indexOf(',', pos);
      if (comma == -1) comma = line.length();
      String byteStr = line.substring(pos, comma);
      byteStr.trim();
      buf[len++] = (uint8_t)strtol(byteStr.c_str(), nullptr, 16);
      pos = comma + 1;
    }

    // Печатаем что отправляем
    Serial.print("Команда #" + String(cmdNum++) + ": ");
    for (size_t i = 0; i < len; i++) {
      if (buf[i] < 0x10) Serial.print("0");
      Serial.print(String(buf[i], HEX) + " ");
    }
    Serial.println();

    sendCommand(buf, len, "TEST");
    delay(2000); // пауза между командами чтобы увидеть эффект
  }

  Serial.println("Тест завершён");
}

void scanResultCallback(NimBLEAdvertisedDevice* device) {
  Serial.print("Найденное устройство: ");
  Serial.println(device->getAddress().toString().c_str());

  if (device->haveName()) {
    Serial.print("  Имя: ");
    Serial.println(device->getName().c_str());
  }

  NimBLEUUID targetService("0000FFE0-0000-1000-8000-00805F9B34FB");
  if (device->isAdvertisingService(targetService)) {
    Serial.println("  → Содержит целевой сервис!");
    targetDeviceAddress = String(device->getAddress().toString().c_str());
  }
}

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");
  esp_log_level_set("*", ESP_LOG_VERBOSE); // Максимальный уровень логирования

  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setActiveScan(true);
  pScan->setInterval(45);
  pScan->setWindow(15);

  pScan->start(60, scanResultCallback);

  Serial.println("Сканирование завершено.");

  if (targetDeviceAddress != "") {
    connectToDevice(targetDeviceAddress);
  } else {
    Serial.println("Целевое устройство не найдено.");
  }
}

void connectToDevice(const String& deviceAddress) {
  if (pClient && pClient->isConnected()) {
    Serial.println("Уже подключены");
    return;
  }

  Serial.print("Подключение к устройству: ");
  Serial.println(deviceAddress);

  if (pClient) {
    NimBLEDevice::deleteClient(pClient);
  }

  pClient = NimBLEDevice::createClient();

  NimBLEAddress address(deviceAddress.c_str(), BLE_ADDR_PUBLIC);
  if (pClient->connect(address)) {
    Serial.println("Успешное подключение!");
    bleConnected=true;
    // Добавляем задержку для стабилизации соединения
    delay(1000);
    // Важно: явно запрашиваем обнаружение сервисов
    //Serial.println("Запрашиваем список сервисов...");
    //bool success = pClient->discoverServices();

    // Получаем список сервисов
    std::vector<NimBLERemoteService*> services = pClient->getServices(true);
    if (services.empty()) {
      Serial.println("  Сервисы не найдены. Возможно, устройство не транслирует их.");
    } else {
      Serial.print("  Найдено сервисов: ");
      Serial.println(services.size());

      for (auto& s : services) {
        Serial.print("    Сервис: ");
        Serial.println(s->getUUID().toString().c_str());

        // Получаем характеристики для каждого сервиса
        std::vector<NimBLERemoteCharacteristic*> characteristics = s->getCharacteristics(true);
        if (characteristics.empty()) {
          Serial.println("      Характеристик нет.");
        } else {
          Serial.print("      Найдено характеристик: ");
          Serial.println(characteristics.size());

          for (auto& c : characteristics) {
            String id = c->getUUID().toString().c_str();
            
            Serial.print("        Характеристика: ");
            Serial.println(id);
            Serial.print("          Свойства: ");
            if (c->canRead()) Serial.print("READ ");
            if (c->canNotify()) Serial.print("NOTIFY ");
            if (c->canWrite()) Serial.print("WRITE ");
            Serial.println();

            if(id=="0xffe1"){
              pRemoteChar = c;
              //isTest=true;
              Serial.println("НАЙДЕНА НУЖНАЯ");
            }

            // Читаем значение, если можно
            if (c->canRead()) {
              std::string value = c->readValue();
              Serial.print("          Значение: ");
              Serial.println(value.empty() ? "пусто" : value.c_str());
            }
          }
        }
      }
    }
  } else {
    Serial.println("Не удалось подключиться");
  }

  //NimBLEDevice::deleteClient(pClient);
}

void loop() {
  if (!bleConnected && millis() - lastConnectAttempt > reconnectInterval) {
    lastConnectAttempt = millis();
    Serial.println("Попытка автоматического переподключения...");
    NimBLEDevice::getScan()->start(8, false);
  }

  if (isTest) {
    if (numTest > 16) numTest = 0;

    if (numTest == 0) setColor(255, 0, 0);
    else if (numTest == 1) setColor(0, 255, 0);
    else if (numTest == 2) setColor(0, 0, 255);
    else if (numTest == 3) setColor(255, 255, 255);
    else if (numTest == 4) setBright(10);
    else if (numTest == 5) setBright(50);
    else if (numTest == 6) setBright(100);
    else if (numTest == 7) power(false);
    else if (numTest == 8) power(true);
    else if (numTest == 9) setColorRGB(255, 0, 0);
    else if (numTest == 10) setColorRGB(0, 255, 0);
    else if (numTest == 11) setColorRGB(0, 0, 255);
    else if (numTest == 12) setColorRGB(255, 255, 255);
    else if (numTest == 13) setBrightRGB(10);
    else if (numTest == 14) setBrightRGB(60);
    else if (numTest == 15) powerRGB(false);
    else if (numTest == 16) powerRGB(true);

    numTest++;
    delay(100);
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "on"){
      power(true);
      powerRGB(true);
    }
    else if (command == "off"){
      power(false);
      powerRGB(false);
    }
    else if (command == "red"){
      setColor(255, 0, 0);
      setColorRGB(255, 0, 0);
    }
    else if (command == "green"){
      setColor(0, 255, 0);
      setColorRGB(0, 255, 0);
    }
    else if (command == "blue"){
      setColor(0, 0, 255);
      setColorRGB(0, 0, 255);
    }
    else if (command == "white"){
      setColor(255, 255, 255);
      setColorRGB(255, 255, 255);
    }
    else if (command.startsWith("br ")) {
      int br = command.substring(3).toInt();
      setBright(br);
      setBrightRGB(br);
    }
    else if (command.startsWith("blink")){
      int ms=100;
      if(command.length()>6)
        ms=command.substring(6).toInt();
      else ms=200;
      setColor(255, 0, 0);
      delay(ms);
      setColor(255, 255, 255);
      delay(ms);
      setColor(255, 0, 0);
      delay(ms);
      setColor(255, 255, 255); 
      delay(ms);
      setColor(255, 0, 0);
      delay(ms);
      setColor(255, 255, 255);
    }
    else if (command == "scan") {
      Serial.println("Перезапуск сканирования:");
      Serial.println(targetDeviceAddress);
      BLEDevice::getScan()->start(15, false);
    }
    else if (command.startsWith("nscan ")) {
      String nmac = command.substring(6);
      targetDeviceAddress = nmac;
      Serial.println("Перезапуск сканирования другого устройства:");
      Serial.println(targetDeviceAddress);
      BLEDevice::getScan()->start(15, false);
    }
    else if (command.startsWith("raw ")) {
      String hex = command.substring(4);
      uint8_t buf[32]; size_t len = 0;
      char* ptr = (char*)hex.c_str();
      while (*ptr && len < 32) {
        sscanf(ptr, "%02X", &buf[len++]);
        ptr += 2;
      }
      sendCommand(buf, len, "RAW");
    }
    else if(command=="test"){
      isTest=!isTest;
    }
    else if (command == "tc"){
      String cmds =
        "7E,00,07,FF,00,00,00,FF,BF\n"
"7E,00,07,00,FF,00,00,FF,BF\n"
"7E,00,07,00,00,FF,00,FF,BF\n"
"7E,00,07,FF,FF,00,00,FF,BF\n"
"7E,00,07,00,FF,FF,00,FF,BF\n"
"7E,00,07,FF,00,00,FF,FF,BF\n"
"7E,00,07,00,FF,00,FF,FF,BF\n"
"7E,00,07,00,00,FF,FF,FF,BF\n"
"7E,00,07,FF,FF,FF,00,FF,BF\n"
"7E,00,0C,FF,00,00,FF,FF,BF\n"
"7E,00,0C,00,FF,00,FF,FF,BF\n"
"7E,00,0C,00,00,FF,FF,FF,BF\n"
"7E,00,0C,FF,FF,00,FF,FF,BF\n"
"7E,00,0C,00,FF,FF,FF,FF,BF\n"
"7E,00,08,FF,00,00,FF,FF,BF\n"
"7E,00,08,00,FF,00,FF,FF,BF\n"
"7E,00,08,00,00,FF,FF,FF,BF\n"
"7E,00,09,FF,00,00,FF,FF,BF\n"
"7E,00,09,00,FF,00,FF,FF,BF\n"
"7E,00,09,00,00,FF,FF,FF,BF\n"
"7E,00,0A,FF,00,00,FF,FF,BF\n"
"7E,00,0A,00,FF,00,FF,FF,BF\n"
"7E,00,0A,00,00,FF,FF,FF,BF\n"
"7E,00,07,FF,00,00,FF,00,BF\n"
"7E,00,07,00,FF,00,FF,00,BF\n"
"7E,00,07,00,00,FF,FF,00,BF\n"
"7E,00,0C,FF,00,00,00,FF,BF\n"
"7E,00,0C,00,FF,00,00,FF,BF\n"
"7E,00,0C,00,00,FF,00,FF,BF\n"
"7E,00,0C,FF,FF,00,00,FF,BF\n"
"7E,00,0C,00,FF,FF,00,FF,BF\n"
"7E,00,07,FF,00,00,00,00,BF\n"
"7E,00,0C,FF,00,00,00,00,BF\n"
"7E,00,07,00,FF,00,00,00,BF\n"
"7E,00,0C,00,FF,00,00,00,BF\n"
"7E,00,07,00,00,FF,00,00,BF\n"
"7E,00,0C,00,00,FF,00,00,BF\n"
"7E,00,07,FF,FF,00,00,00,BF\n"
"7E,00,0C,FF,FF,00,00,00,BF\n"
"7E,00,07,00,FF,FF,00,00,BF\n"
"7E,00,0C,00,FF,FF,00,00,BF\n"
"7E,00,07,FF,00,FF,00,FF,BF\n"
"7E,00,0C,FF,00,FF,00,FF,BF\n"
"7E,00,07,FF,00,00,FF,00,FF\n"
"7E,00,0C,FF,00,00,FF,00,FF\n"
"7E,00,07,00,FF,00,FF,00,FF\n"
"7E,00,0C,00,FF,00,FF,00,FF\n"
"7E,00,07,00,00,FF,FF,00,FF\n"
"7E,00,0C,00,00,FF,FF,00,FF\n"
"7E,00,07,FF,FF,FF,00,FF,00\n"
"7E,00,0C,FF,FF,FF,00,FF,00\n"
"7E,00,07,FF,00,00,00,FF,00\n"
"7E,00,0C,FF,00,00,00,FF,00\n"
"7E,00,07,00,FF,00,00,FF,00\n"
"7E,00,0C,00,FF,00,00,FF,00\n"
"7E,00,07,00,00,FF,00,FF,00\n"
"7E,00,0C,00,00,FF,00,FF,00\n"
"7E,00,07,FF,FF,00,FF,00,00\n"
"7E,00,0C,FF,FF,00,FF,00,00\n"
"7E,00,07,FF,00,FF,FF,00,00\n"
"7E,00,0C,FF,00,FF,FF,00,00\n"
"7E,00,07,00,FF,FF,FF,00,00\n"
"7E,00,0C,00,FF,FF,FF,00,00\n"
"7E,00,07,FF,00,00,FF,FF,00\n"
"7E,00,0C,FF,00,00,FF,FF,00\n"
"7E,00,07,00,FF,00,FF,FF,00\n"
"7E,00,0C,00,FF,00,FF,FF,00\n"
"7E,00,07,00,00,FF,FF,FF,00\n"
"7E,00,0C,00,00,FF,FF,FF,00\n"
"7E,00,07,FF,FF,FF,FF,00,00\n"
"7E,00,0C,FF,FF,FF,FF,00,00\n"
"7E,00,07,FF,00,00,00,00,FF\n"
"7E,00,0C,FF,00,00,00,00,FF\n"
"7E,00,07,00,FF,00,00,00,FF\n"
"7E,00,0C,00,FF,00,00,00,FF\n"
"7E,00,07,00,00,FF,00,00,FF\n"
"7E,00,0C,00,00,FF,00,00,FF\n"
"7E,00,07,FF,FF,00,00,00,FF\n"
"7E,00,0C,FF,FF,00,00,00,FF\n"
"7E,00,07,00,FF,FF,00,00,FF\n"
"7E,00,0C,00,FF,FF,00,00,FF\n"
"7E,00,07,00,00,FF,FF,00,FF\n"
"7E,00,0C,00,00,FF,FF,00,FF\n"
"7E,00,07,FF,00,00,FF,00,FF\n"
"7E,00,0C,FF,00,00,FF,00,FF\n"
"7E,00,07,00,FF,00,FF,00,FF\n"
"7E,00,0C,00,FF,00,FF,00,FF\n"
"7E,00,07,00,00,FF,FF,00,FF\n"
"7E,00,0C,00,00,FF,FF,00,FF";
      testCommands(cmds);
    }
    else {
      Serial.println("Доступные команды:");
      Serial.println("  on | off | red | green | blue | white");
      Serial.println("  color 255 0 0");
      Serial.println("  br 0-100");
      Serial.println("  scan");
      Serial.println("  nscan mac");
      Serial.println("  raw FFFF");
    }
  }

  delay(50);
}