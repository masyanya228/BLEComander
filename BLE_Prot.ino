#include <Wire.h>
#include "NimBLEDevice.h"

String targetDeviceAddress = "c0:00:00:00:05:42";
String targetDeviceAddressSky = "a4:c1:38:20:30:0e";
bool bleConnected = false;
NimBLEClient* pClient = nullptr;
NimBLEClient* pClientSky = nullptr;
NimBLERemoteCharacteristic* pRemoteChar = nullptr;
NimBLERemoteCharacteristic* pRemoteCharSky = nullptr;
bool isTest=false;
int numTest=0;
uint32_t lastConnectAttempt = 0;
const uint32_t reconnectInterval = 8000;

#define SLAVE_ADDR 20
#define REG_Power 0x10 //Вкл/Выкл подсветку [target][state]
#define REG_SetRGB 0x11 //Установить произвольный RGB цвет [target][R][G][B]
#define REG_SetBR 0x12 //Установить яркость [target][Br]
#define REG_GetLightStatus 0x15 //Вернуть статус подсветки
#define REG_GetErrorCount 0x06
#define REG_GetNextError  0x07
uint8_t currentR = 255;
uint8_t currentG = 255;
uint8_t currentB = 255;
uint8_t currentBR = 100;
bool    powerState = true;
uint8_t lastCmd = 0;
uint32_t lastMessageTime = 0;

class BLEDeviceControl {
public:
  String mac;
  NimBLEClient* client = nullptr;
  NimBLERemoteCharacteristic* characteristic = nullptr;
  bool connected = false;

  BLEDeviceControl(String macAddress) : mac(macAddress) {}

  bool connect() {
    if (connected && client && client->isConnected()) return true;

    if (client) {
      NimBLEDevice::deleteClient(client);
    }

    client = NimBLEDevice::createClient();
    client->setConnectTimeout(10);

    NimBLEAddress address(mac.c_str(), BLE_ADDR_PUBLIC);

    Serial.print("Подключение к " + mac + " ... ");

    if (client->connect(address)) {
      Serial.println("OK");
      delay(1000);
      std::vector<NimBLERemoteService*> services = pClientSky->getServices(true);
      if (services.empty()) {
        Serial.println("Сервисы не найдены.");
      } else {
        Serial.print("  Найдено сервисов: ");
        Serial.println(services.size());

        for (auto& s : services) {
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
                characteristic = c;
              }
              if (c->canRead()) {
                std::string value = c->readValue();
                Serial.print("          Значение: ");
                Serial.println(value.empty() ? "пусто" : value.c_str());
              }
            }
          }
        }
        if(characteristic)
        {
          Serial.println("StarSky ready");
          return true;
        }
        else Serial.println("  → Сервис/характеристика не найдены"); 
      }
    } else {
      Serial.println("НЕ УДАЛОСЬ");
    }
    return false;
  }

  bool send(const uint8_t* data, size_t len) {
    if (!connected || !characteristic) {
      Serial.println("❌ " + mac + " не подключён");
      return false;
    }
    return characteristic->writeValue(const_cast<uint8_t*>(data), len, false);
  }
};

NimBLEUUID serviceUUID("0000FFE0-0000-1000-8000-00805F9B34FB");
NimBLEUUID charUUID("0000FFE1-0000-1000-8000-00805F9B34FB");

BLEDeviceControl* MainDevice = nullptr;   // c0:00:00:00:05:42
BLEDeviceControl* SkyDevice  = nullptr;   // c0:00:00:00:04:ad

class LightState {
public:
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  uint8_t brightness = 100;
  bool power = true;
  BLEDeviceControl device;
  String name;
  
  LightState(BLEDeviceControl _device, String _name)
  {
    device = _device;
    name = _name;
  }

  bool setColor(uint8_t nr, uint8_t ng, uint8_t nb, const uint8_t* data, size_t len) {
    bool res = device->send(data, len);
    if(res){
      r = nr; g = ng; b = nb;
    }
    logCom(res, data, len);
    return res;
  }

  bool setBright(uint8_t br, const uint8_t* data, size_t len) {
    bool res = device->send(data, len);
    if(res){
      brightness = br;
    }
    logCom(res, data, len);
    return res;
  }

  bool setPower(bool on, const uint8_t* data, size_t len) {
    bool res = device->send(data, len);
    if(res){
      power = on;
    }
    logCom(res, data, len);
    return res;
  }

  void logCom(bool res, const uint8_t* data, size_t len){
    Serial.print(res ? "✅ " : "❌ ");
    Serial.print(device->mac + " | " + name + " | ");
    for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
      Serial.println();
  }
};

LightState* Line = nullptr;
LightState* Lamp = nullptr;
LightState* Sky = nullptr;

void receiveCb(int howMany) {
  if (howMany == 0) return;

  lastCmd = Wire.read();
  lastMessageTime = millis();

  Serial.printf("I2C команда: 0x%02X\n", lastCmd);

  switch (lastCmd) {
    case REG_SetRed:
      setRed();
      break;

    case REG_SetWhite:
      setWhite();
      break;

    case REG_SetRGB:
      if (howMany >= 4) {
        uint8_t r = Wire.read();
        uint8_t g = Wire.read();
        uint8_t b = Wire.read();
        setRGB(r, g, b);
      }
      break;

    case REG_Power:
      if (howMany >= 2) {
        uint8_t state = Wire.read();
        setPower(state == 1);
      }
      break;

    case REG_GetStatus:
    case REG_GetErrorCount:
    case REG_GetNextError:
      // ничего не делаем при приёме, ответ будет в requestCb()
      break;
  }
}

void requestCb() {
  switch (lastCmd) {
    case REG_GetStatus:
      // Возвращаем простое состояние: 1 = включено, 0 = выключено
      Wire.write(powerState ? 1 : 0);
      break;

    case REG_GetErrorCount:
      Wire.write(0);        // заглушка
      break;

    case REG_GetNextError:
      Wire.write(0);        // заглушка
      break;

    default:
      Wire.write(0);
  }
}

void power(bool on) {
  uint8_t cmd[9] = {0x7B, 0x00, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF};
  Line.setPower(on, cmd, sizeof(cmd));
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7B, 0x00, 0x07, r, g, b, 0xFF, 0xFF, 0xBF};
  Line.setColor(r, g, b, cmd, sizeof(cmd));
}

void setBright(uint8_t percent) {
  uint8_t cmd[] = {0x7B, 0xFF, 0x01, percent*32/100, percent, 0x00, 0xFF, 0xFF, 0xBF};
  Line.setBright(percent, cmd, sizeof(cmd));
}

void powerRGB(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  Lamp.setPower(on, cmd, sizeof(cmd));
}

void setColorRGB(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7E, 0xFF, 0x05, 0x03, r, g, b, 0xFF, 0xEF};
  Lamp.setColor(r, g, b, cmd, sizeof(cmd));
}

void setBrightRGB(uint8_t percent) {
  uint8_t cmd[] = {0x7E, 0xFF, 0x01, percent, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  Lamp.setBright(percent, cmd, sizeof(cmd));
}

void powerSKY(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  Sky.setPower(on, cmd, sizeof(cmd));
}

void setColorSKY(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[] = {0x7E, 0xFF, 0x05, 0x03, r, g, b, 0xFF, 0xEF};
  Sky.setColor(r, g, b, cmd, sizeof(cmd));
}

void setBrightSKY(uint8_t percent) {
  uint8_t cmd[] = {0x7E, 0x00, 0x01, percent, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  Sky.setBright(percent, cmd, sizeof(cmd));
}

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  // Создаём объекты устройств
  MainDevice = new BLEDeviceControl("c0:00:00:00:05:42");
  SkyDevice  = new BLEDeviceControl("c0:00:00:00:04:ad");

  // Запускаем подключение
  if(MainDevice->connect())
  {
    Line = new LightState(MainDevice, "Линии");
    Lamp = new LightState(MainDevice, "Подстаканники");
  }
  if(SkyDevice->connect())
  {
    Sky = new LightState(SkyDevice, "Звезды");
  }

  // I2C Slave
  Wire.begin(SLAVE_ADDR);
  Wire.onReceive(receiveCb);
  Wire.onRequest(requestCb);

  Serial.println("I2C Slave запущен на адресе 0x" + String(SLAVE_ADDR, HEX));
}

void loop() {
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
      powerSKY(true);
    }
    else if (command == "off"){
      power(false);
      powerRGB(false);
      powerSKY(false);
    }
    else if (command == "red"){
      setColor(255, 0, 0);
      setColorRGB(255, 0, 0);
      setColorSKY(255, 0, 0);
    }
    else if (command == "green"){
      setColor(0, 255, 0);
      setColorRGB(0, 255, 0);
      setColorSKY(0, 255, 0);
    }
    else if (command == "blue"){
      setColor(0, 0, 255);
      setColorRGB(0, 0, 255);
      setColorSKY(0, 0, 255);
    }
    else if (command == "white"){
      setColor(255, 255, 255);
      setColorRGB(255, 255, 255);
      setColorSKY(255, 255, 255);
    }
    else if (command.startsWith("br ")) {
      int br = command.substring(3).toInt();
      setBright(br);
      setBrightRGB(br);
      setBrightSKY(br);
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
    else if (command.startsWith("color ")) {
      int r = 0, g = 0, b = 0;
      sscanf(command.c_str() + 6, "%d %d %d", &r, &g, &b);
      setColor(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      setColorRGB(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
      setColorSKY(constrain(r, 0, 255), constrain(g, 0, 255), constrain(b, 0, 255));
    }
    else if(command=="test"){
      isTest=!isTest;
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