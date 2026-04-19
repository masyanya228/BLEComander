#include <Wire.h>
#include "NimBLEDevice.h"

#define SLAVE_ADDR        40
#define REG_Power         0x10
#define REG_SetRGB        0x11
#define REG_SetBR         0x12
#define REG_GetLightStatus 0x13
#define REG_GetErrorCount 0x06
#define REG_GetNextError  0x07

// ─── BLE устройство ──────────────────────────────────────────────────────────
class BLEDeviceControl {
public:
  String mac;
  NimBLEClient* client = nullptr;
  NimBLERemoteCharacteristic* characteristic = nullptr;
  bool connected = false;

  BLEDeviceControl(const String& macAddress) : mac(macAddress) {}

  bool connect() {
    if (connected && client && client->isConnected()) return true;

    if (client) NimBLEDevice::deleteClient(client);

    client = NimBLEDevice::createClient();
    client->setConnectTimeout(10);

    NimBLEAddress address(mac.c_str(), BLE_ADDR_PUBLIC);
    Serial.print("Подключение к " + mac + " ... ");

    if (!client->connect(address)) {
      Serial.println("НЕ УДАЛОСЬ");
      return false;
    }

    Serial.println("OK");
    delay(1000);

    // Исправлено: client вместо pClientSky
    std::vector<NimBLERemoteService*> services = client->getServices(true);
    if (services.empty()) {
      Serial.println("Сервисы не найдены.");
      return false;
    }

    Serial.printf("  Найдено сервисов: %d\n", services.size());

    for (auto& s : services) {
      std::vector<NimBLERemoteCharacteristic*> chars = s->getCharacteristics(true);
      for (auto& c : chars) {
        String id = c->getUUID().toString().c_str();
        Serial.print("    Хар-ка: " + id + " [");
        if (c->canRead())   Serial.print("R");
        if (c->canWrite())  Serial.print("W");
        if (c->canNotify()) Serial.print("N");
        Serial.println("]");

        if (id == "0xffe1") {
          characteristic = c;
        }
      }
    }

    if (characteristic) {
      connected = true;
      Serial.println("  → Готово: FFE1 найдена");
      return true;
    }

    Serial.println("  → FFE1 не найдена");
    return false;
  }

  // Длина всегда 9 — все LED-команды имеют фиксированный размер
  bool send(const uint8_t* data) {
    if (!connected || !characteristic) {
      Serial.println("❌ " + mac + " не подключён");
      return false;
    }
    return characteristic->writeValue(const_cast<uint8_t*>(data), 9, false);
  }
};

// ─── Состояние канала подсветки ───────────────────────────────────────────────
class LightState {
public:
  uint8_t r = 255, g = 255, b = 255;
  uint8_t br = 100;
  bool power = true;
  BLEDeviceControl* device;  // указатель, не копия
  String name;

  LightState(BLEDeviceControl* dev, const String& n) : device(dev), name(n) {}

  bool setColor(uint8_t nr, uint8_t ng, uint8_t nb, const uint8_t* data) {
    bool res = device->send(data);
    if (res) { r = nr; g = ng; b = nb; }
    log(res, data);
    return res;
  }

  bool setBright(uint8_t nbr, const uint8_t* data) {
    bool res = device->send(data);
    if (res) br = nbr;
    log(res, data);
    return res;
  }

  bool setPower(bool on, const uint8_t* data) {
    bool res = device->send(data);
    if (res) power = on;
    log(res, data);
    return res;
  }

private:
  void log(bool res, const uint8_t* data) {
    Serial.print(res ? "✅ " : "❌ ");
    Serial.print(device->mac + " | " + name + " | ");
    for (size_t i = 0; i < 9; i++) Serial.printf("%02X ", data[i]);
    Serial.println();
  }
};

// ─── Глобальные объекты ───────────────────────────────────────────────────────
BLEDeviceControl* MainDevice = nullptr;
BLEDeviceControl* SkyDevice  = nullptr;
LightState* Line = nullptr;
LightState* Lamp = nullptr;
LightState* Sky  = nullptr;

bool isTest = false;
int  numTest = 0;
uint8_t lastCmd = 0;

// ─── Команды — Линии (подстаканники, канал 0) ────────────────────────────────
void power(bool on) {
  uint8_t cmd[9] = {0x7B, 0x00, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xBF};
  if (Line) Line->setPower(on, cmd);
}

void setColor(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[9] = {0x7B, 0x00, 0x07, r, g, b, 0xFF, 0xFF, 0xBF};
  if (Line) Line->setColor(r, g, b, cmd);
}

void setBright(uint8_t pct) {
  uint8_t cmd[9] = {0x7B, 0xFF, 0x01, (uint8_t)(pct * 32 / 100), pct, 0x00, 0xFF, 0xFF, 0xBF};
  if (Line) Line->setBright(pct, cmd);
}

// ─── Команды — Подсветка RGB (Лампы/Lamp) ────────────────────────────────────
void powerRGB(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  if (Lamp) Lamp->setPower(on, cmd);
}

void setColorRGB(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x05, 0x03, r, g, b, 0xFF, 0xEF};
  if (Lamp) Lamp->setColor(r, g, b, cmd);
}

void setBrightRGB(uint8_t pct) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x01, pct, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  if (Lamp) Lamp->setBright(pct, cmd);
}

// ─── Команды — Звёздное небо ──────────────────────────────────────────────────
void powerSKY(bool on) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x04, on ? 0x01 : 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xEF};
  if (Sky) Sky->setPower(on, cmd);
}

void setColorSKY(uint8_t r, uint8_t g, uint8_t b) {
  uint8_t cmd[9] = {0x7E, 0xFF, 0x05, 0x03, r, g, b, 0xFF, 0xEF};
  if (Sky) Sky->setColor(r, g, b, cmd);
}

void setBrightSKY(uint8_t pct) {
  uint8_t cmd[9] = {0x7E, 0x00, 0x01, pct, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
  if (Sky) Sky->setBright(pct, cmd);
}

// ─── I2C ─────────────────────────────────────────────────────────────────────
void receiveCb(int howMany) {
  if (howMany == 0) return;
  lastCmd = Wire.read();
  Serial.printf("I2C команда: 0x%02X\n", lastCmd);

  switch (lastCmd) {
    case REG_SetRGB:
      if (howMany >= 4) {
        uint8_t r = Wire.read();
        uint8_t g = Wire.read();
        uint8_t b = Wire.read();
        setColor(r, g, b);
        setColorRGB(r, g, b);
        setColorSKY(r, g, b);
      }
      break;

    case REG_SetBR:
      if (howMany >= 2) {
        uint8_t br = Wire.read();
        setBright(br);
        setBrightRGB(br);
        setBrightSKY(br);
      }
      break;

    case REG_Power:
      if (howMany >= 3) {
        uint8_t target = GetTarget();
        bool state = Wire.read() == 1;
        cmd_power(target, state);
      }
      break;

    case REG_GetLightStatus:
    case REG_GetErrorCount:
    case REG_GetNextError:
      break;  // ответ в requestCb
  }
}

/*
1 - Line
2 - Lamp
3 - Sky
4 - Line + Lamp
5 - Lamp + Sky
6 - Line + Sky
7 - All
*/
uint8_t GetTarget(){
  return Wire.read();
}

bool cmd_power(uint8_t target, bool state)
{
  if(target==1)
    return power(state);
  if(target==2)
    return powerRGB(state);
  if(target==3)
    powerSKY(state);
  if(target==4){
    return power(state) & powerRGB(state);
  }
  if(target==5){
    return powerRGB(state) & powerSKY(state);
  }
  if(target==6){
    return power(state) & powerSKY(state);
  }
  if(target==7){
    return power(state) & powerRGB(state) & powerSKY(state);
  }
}

bool cmd_color(uint8_t target, uint8_t r, uint8_t g, uint8_t b)
{
  if(target==1)
    return setColor(r, g, b);
  if(target==2)
    return setColorRGB(r, g, b);
  if(target==3)
    setColorSKY(r, g, b);
  if(target==4){
    return setColor(r, g, b) & setColorRGB(r, g, b);
  }
  if(target==5){
    return setColorRGB(r, g, b) & setColorSKY(r, g, b);
  }
  if(target==6){
    return setColor(r, g, b) & setColorSKY(r, g, b);
  }
  if(target==7){
    return setColor(r, g, b) & setColorRGB(r, g, b) & setColorSKY(r, g, b);
  }
}

bool cmd_bright(uint8_t target, uint8_t br)
{
  if(target==1)
    return setBright(br);
  if(target==2)
    return setBrightRGB(br);
  if(target==3)
    setBrightSKY(br);
  if(target==4){
    return setBright(br) & setBrightRGB(br);
  }
  if(target==5){
    return setBrightRGB(br) & setBrightSKY(br);
  }
  if(target==6){
    return setBright(br) & setBrightSKY(br);
  }
  if(target==7){
    return setBright(br) & setBrightRGB(br) & setBrightSKY(br);
  }
}

void requestCb() {
  switch (lastCmd) {
    case REG_GetLightStatus:
      Wire.write(Line ? (Line->power ? 1 : 0) : 2);
      Wire.write(Lamp ? (Lamp->power ? 1 : 0) : 2);
      Wire.write(Sky ? (Sky->power ? 1 : 0) : 2);
      Wire.write(Line ? Line->r : 0);
      Wire.write(Line ? Line->g : 0);
      Wire.write(Line ? Line->b : 0);
      Wire.write(Line ? Line->br : 0);
      Wire.write(Lamp ? Lamp->r : 0);
      Wire.write(Lamp ? Lamp->g : 0);
      Wire.write(Lamp ? Lamp->b : 0);
      Wire.write(Lamp ? Lamp->br : 0);
      Wire.write(Sky ? Sky->r : 0);
      Wire.write(Sky ? Sky->g : 0);
      Wire.write(Sky ? Sky->b : 0);
      Wire.write(Sky ? Sky->br : 0);
      break;
    case REG_Power:
cmd_power
      break;
    case REG_GetErrorCount:
    case REG_GetNextError:
    default:
      Wire.write(0);
      break;
  }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  MainDevice = new BLEDeviceControl("c0:00:00:00:05:42");
  SkyDevice  = new BLEDeviceControl("c0:00:00:00:04:ad");

  if (MainDevice->connect()) {
    Line = new LightState(MainDevice, "Линии");
    Lamp = new LightState(MainDevice, "Подстаканники");
  }
  if (SkyDevice->connect()) {
    Sky = new LightState(SkyDevice, "Звёзды");
  }

  Wire.begin(SLAVE_ADDR);
  Wire.onReceive(receiveCb);
  Wire.onRequest(requestCb);

  Serial.println("I2C Slave: 0x" + SLAVE_ADDR);
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  if (isTest) {
    if (numTest > 16) numTest = 0;
    switch (numTest) {
      case 0:  setColor(255, 0, 0);        break;
      case 1:  setColor(0, 255, 0);        break;
      case 2:  setColor(0, 0, 255);        break;
      case 3:  setColor(255, 255, 255);    break;
      case 4:  setBright(10);              break;
      case 5:  setBright(50);              break;
      case 6:  setBright(100);             break;
      case 7:  power(false);               break;
      case 8:  power(true);                break;
      case 9:  setColorRGB(255, 0, 0);     break;
      case 10: setColorRGB(0, 255, 0);     break;
      case 11: setColorRGB(0, 0, 255);     break;
      case 12: setColorRGB(255, 255, 255); break;
      case 13: setBrightRGB(10);           break;
      case 14: setBrightRGB(60);           break;
      case 15: powerRGB(false);            break;
      case 16: powerRGB(true);             break;
    }
    numTest++;
    delay(100);
  }

  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();

    if (command == "on") {
      power(true); powerRGB(true); powerSKY(true);
    } else if (command == "off") {
      power(false); powerRGB(false); powerSKY(false);
    } else if (command == "red") {
      setColor(255,0,0); setColorRGB(255,0,0); setColorSKY(255,0,0);
    } else if (command == "green") {
      setColor(0,255,0); setColorRGB(0,255,0); setColorSKY(0,255,0);
    } else if (command == "blue") {
      setColor(0,0,255); setColorRGB(0,0,255); setColorSKY(0,0,255);
    } else if (command == "white") {
      setColor(255,255,255); setColorRGB(255,255,255); setColorSKY(255,255,255);
    } else if (command.startsWith("br ")) {
      int br = constrain(command.substring(3).toInt(), 0, 100);
      setBright(br); setBrightRGB(br); setBrightSKY(br);
    } else if (command.startsWith("color ")) {
      int r = 0, g = 0, b = 0;
      sscanf(command.c_str() + 6, "%d %d %d", &r, &g, &b);
      r = constrain(r, 0, 255); g = constrain(g, 0, 255); b = constrain(b, 0, 255);
      setColor(r,g,b); setColorRGB(r,g,b); setColorSKY(r,g,b);
    } else if (command.startsWith("blink")) {
      int ms = command.length() > 6 ? command.substring(6).toInt() : 200;
      for (int i = 0; i < 3; i++) {
        setColor(255, 0, 0); delay(ms);
        setColor(255, 255, 255); delay(ms);
      }
    } else if (command == "test") {
      isTest = !isTest;
      Serial.println(isTest ? "Тест включён" : "Тест выключен");
    } else {
      Serial.println("Команды: on | off | red | green | blue | white | color R G B | br 0-100 | blink [ms] | test");
    }
  }

  delay(50);
}
