#include "NimBLEDevice.h"

String targetDeviceAddress = "c0:00:00:00:05:42";
bool bleConnected = false;
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pRemoteChar = nullptr;
bool isTest=false;
int numTest=0;

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
  sendCommand(cmd, 9, on ? "ВКЛЮЧЕНИЕ" : "ВЫКЛЮЧЕНИЕ");
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7B, 0x00, 0x07, r, g, b, 0xFF, 0xFF, 0xBF};
  sendCommand(cmd, sizeof(cmd), "ЦВЕТ");
}

void powerRGB(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  sendCommand(cmd, 9, on ? "ВКЛЮЧЕНИЕ" : "ВЫКЛЮЧЕНИЕ");
}

void setBrightRGB(uint8_t br) {
  uint8_t cmd[] = {0x7E, 0xFF, 0x01, br, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  sendCommand(cmd, sizeof(cmd), "Подстаканники яркость");
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
  Serial.print("Подключение к устройству: ");
  Serial.println(deviceAddress);

  pClient = NimBLEDevice::createClient();
  if (!pClient) {
    Serial.println("Ошибка создания клиента");
    return;
  }

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
              isTest=true;
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
  if(isTest)
  {
    if(numTest>10)
    {
      numTest=0;
    }

    if(numTest==0)
    {
      setColor(255,0,0);
    }
    else if(numTest==1)
    {
      setColor(0,255,0);
    }
    else if(numTest==2)
    {
      setColor(0,0,255);
    }
    else if(numTest==3)
    {
      setColor(255,255,255);
    }
    else if(numTest==4)
    {
      power(false);
    }
    else if(numTest==5)
    {
      power(true);
    }
    else if(numTest==6)
    {
      setBrightRGB(10);
    }
    else if(numTest==7)
    {
      setBrightRGB(20);
    }
    else if(numTest==8)
    {
      setBrightRGB(60);
    }
    else if(numTest==9)
    {
      powerRGB(false);
    }
    else if(numTest==10)
    {
      powerRGB(true);
    }
    numTest++;
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "on")      power(true);
    else if (command == "off") power(false);
    else if (command == "blink"){
      setColor(255, 0, 0);
      delay(200);
      setColor(255, 255, 255);
      delay(200);
      setColor(255, 0, 0);
      delay(200);
      setColor(255, 255, 255); 
      delay(200);
      setColor(255, 0, 0);
      delay(200);
      setColor(255, 255, 255);
    }
    else if (command == "red")   setColor(255, 0, 0);
    else if (command == "green") setColor(0, 255, 0);
    else if (command == "blue")  setColor(0, 0, 255);
    else if (command == "white") setColor(255, 255, 255);
    else if (command.startsWith("color ")) {
      int r = 0, g = 0, b = 0;
      sscanf(command.c_str() + 6, "%d %d %d", &r, &g, &b);
      setColor(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
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
    else {
      Serial.println("Доступные команды:");
      Serial.println("  on | off | red | green | blue | white");
      Serial.println("  color 255 0 0");
      Serial.println("  scan");
      Serial.println("  raw FFFF");
    }
  }

  delay(300);
}