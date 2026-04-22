bool isDebug=true;
#include <Wire.h>
#include "NimBLEDevice.h"
#include "I2CSlave.h"



#define AmbientLight 0x01
#define StarSky 0x02

I2CSlave slave;
void SaveError(uint8_t code);

// ─── BLE устройство ──────────────────────────────────────────────────────────
class BLEDeviceControl {
public:
  uint8_t type;
  String mac;
  NimBLEClient* client = nullptr;
  NimBLERemoteCharacteristic* characteristic = nullptr;
  bool connected = false;

  BLEDeviceControl(const String& macAddress, uint8_t deviceType) : mac(macAddress), type(deviceType) {}

  bool connect() {
    if (connected && client && client->isConnected()) return true;

    if (client) NimBLEDevice::deleteClient(client);

    client = NimBLEDevice::createClient();
    client->setConnectTimeout(1000);

    NimBLEAddress address(mac.c_str(), BLE_ADDR_PUBLIC);
    Serial.print("Подключение к " + mac + " ... ");

    if (!client->connect(address)) {
      Serial.println("НЕ УДАЛОСЬ");
      SaveError(type==AmbientLight?41:42);
      return connect();
    }

    Serial.println("OK");
    delay(1000);

    // Исправлено: client вместо pClientSky
    std::vector<NimBLERemoteService*> services = client->getServices(true);
    if (services.empty()) {
      Serial.println("Сервисы не найдены.");
      SaveError(type==AmbientLight?48:49);
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
    else{
      SaveError(type==AmbientLight?45:46);
    }
    Serial.println("  → FFE1 не найдена");
    return false;
  }

  // Длина всегда 9 — все LED-команды имеют фиксированный размер
  bool send(const uint8_t* data) {
    if (!connected || !characteristic) {
      Serial.println("❌ " + mac + " не подключён");
      SaveError(type==AmbientLight?52:53);
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
    if(!res)
      SaveError(device->type==AmbientLight?54:55);
    Serial.print(res ? "✅ " : "❌ ");
    Serial.print(device->mac + " | " + name + " | ");
    for (size_t i = 0; i < 9; i++) Serial.printf("%02X ", data[i]);
    Serial.println();
  }
};

// ─── Глобальные объекты ───────────────────────────────────────────────────────
BLEDeviceControl* MainDevice = nullptr;
BLEDeviceControl* SkyDevice  = nullptr;
LightState* Sky  = nullptr;
LightState* Line = nullptr;
LightState* Lamp = nullptr;

bool isTest = false;
int  numTest = 0;
uint8_t lastCmd = 0;

//Ошибки в памяти
struct Error{
  uint8_t code=0;
  uint32_t tfs=0;
  uint8_t times=0;
};
Error errors[10];
int sizeErr;
int errLen;
int nextError=0;

struct ErrorDesc {
    uint8_t code;
    const char* description;
};
const ErrorDesc errorDescriptions[] PROGMEM = {
    {41,   "Ambient not scannable"},
    {42,   "StarSky not scannable"},
    {43,   "Ambient has no target service"},
    {44,   "StarSky has no target service"},
    {45,   "Ambient has no target characteristic"},
    {46,   "StarSky has no target characteristic"},
    {47,   "Not supproted i2c command"},
    {48,   "Ambient has no services"},
    {49,   "StarSky has no services"},
    {50,   "Ambient has no characteristics"},
    {51,   "StarSky has no characteristics"},
    {52,   "Ambient still not ready to command"},
    {53,   "StarSky still not ready to command"},
    {54,   "Ambient bad response"},
    {55,   "StarSky bad response"},
    {0,   ""}   // terminator (обязательно в конце!)
};

void setup() {
  Serial.begin(115200);
  InitEEPROM();
  NimBLEDevice::init("");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  MainDevice = new BLEDeviceControl("c0:00:00:00:05:42", AmbientLight);
  SkyDevice  = new BLEDeviceControl("a4:c1:38:20:30:0e", StarSky);

  if (MainDevice->connect()) {
    Line = new LightState(MainDevice, "Линии");
    Lamp = new LightState(MainDevice, "Подстаканники");
  }
  if (SkyDevice->connect()) {
    Sky = new LightState(SkyDevice, "Звёзды");
  }

  slave.onCommand(REG_PING, cmdPing);
  slave.onCommand(REG_GetErrorCount, cmdGetErrorCount);
  slave.onCommand(REG_GetNextError, cmdGetError);
  slave.onCommand(REG_ClearErrors, cmdClearErrors);
  slave.onCommand(REG_Power, cmd_power);
  slave.onCommand(REG_SetRGB, cmd_color);
  slave.onCommand(REG_SetBR, cmd_bright);
  slave.onCommand(REG_GetLightStatus, cmd_getLightStatus);
  slave.begin();
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  slave.process();
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
    } else if (command == "eeprom init") {
      Serial.println("Сброс памяти к заводским настройкам...");
      FirstInit();
      LoadErrors();
      Serial.println("Готово");
    } else if (command == "eeprom read") {
      Serial.println("Вывод содержимого памяти...");
      LoadErrors();
      for(int i=0;i<errLen;i++)
      {
        Serial.print("#");
        Serial.print(errors[i].code);
        Serial.print("|");
        Serial.print(errors[i].times);
        Serial.print("|");
        Serial.println(errors[i].tfs);
      }
      Serial.println("Готово");
    } else {
      Serial.println("Команды: on | off | red | green | blue | white | color R G B | br 0-100 | blink [ms] | test");
    }
  }
  delay(5);
}

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

//I2C commands
void cmd_power(const uint8_t* buf, uint8_t len)
{
  uint8_t target = buf[1];
  bool state = buf[2];
  if(target==1)
    powerSKY(state);
  if(target==2)
    power(state);
  if(target==3)
    powerRGB(state);
  if(target==4){
    powerSKY(state); power(state);
  }
  if(target==5){
    power(state); powerRGB(state);
  }
  if(target==6){
    power(state); powerRGB(state); powerSKY(state);
  }
  slave.respondByte(0x01);
}

void cmd_color(const uint8_t* buf, uint8_t len)
{
  uint8_t target = buf[1];
  uint8_t r=buf[2];
  uint8_t g=buf[3];
  uint8_t b=buf[4];
  if(target==1)
    setColorSKY(r, g, b);
  if(target==2)
    setColor(r, g, b);
  if(target==3)
    setColorRGB(r, g, b);
  if(target==4){
    setColorSKY(r, g, b); setColor(r, g, b);
  }
  if(target==5){
    setColor(r, g, b); setColorRGB(r, g, b);
  }
  if(target==6){
    setColor(r, g, b); setColorRGB(r, g, b); setColorSKY(r, g, b);
  }
  slave.respondByte(0x01);
}

void cmd_bright(const uint8_t* buf, uint8_t len)
{
  uint8_t target=buf[1];
  uint8_t br=buf[2];
  Serial.print("I2C br: ");
  Serial.print(target);
  Serial.print("=");
  Serial.print(br);
  Serial.println("%");
  if(target==1)
    setBrightSKY(br);
  if(target==2)
    setBright(br);
  if(target==3)
    setBrightRGB(br);
  if(target==4){
    setBrightSKY(br); setBright(br);
  }
  if(target==5){
    setBright(br); setBrightRGB(br);
  }
  if(target==6){
    setBright(br); setBrightRGB(br); setBrightSKY(br);
  }
  slave.respondByte(0x01);
}

void cmd_getLightStatus(const uint8_t*, uint8_t) {
  uint8_t resp[16];
  resp[0] = 1;
  resp[1] = Sky->power;
  resp[2] = Sky->r;
  resp[3] = Sky->g;
  resp[4] = Sky->b;
  resp[5] = Sky->br;
  resp[6] = Line->power;
  resp[7] = Line->r;
  resp[8] = Line->g;
  resp[9] = Line->b;
  resp[10] = Line->br;
  resp[11] = Lamp->power;
  resp[12] = Lamp->r;
  resp[13] = Lamp->g;
  resp[14] = Lamp->b;
  resp[15] = Lamp->br;
  slave.respond(resp, 16);
}

void cmdPing(const uint8_t*, uint8_t) {
  slave.respondByte(0x01);
}

void cmdGetErrorCount(const uint8_t*, uint8_t) {
  Serial.print("cmdGetErrorCount: ");
  uint8_t count = 0;
  for (uint8_t i = 0; i < errLen; i++)
      if (errors[i].times > 0) count++;
  uint8_t resp[2]={1, count};
  Serial.println(count);
  slave.respond(resp, sizeof(resp));
}

void cmdGetError(const uint8_t* buf, uint8_t len) {
  Serial.print("getError #");
  uint8_t index = (len >= 2) ? buf[1] : 0;
  Serial.println(index);
  uint8_t found = 0;
  for (uint8_t i = 0; i < errLen; i++) {
    if (errors[i].times == 0) continue;
    if (found++ == index) {
      Serial.print("Code: ");
      Serial.println(errors[i].code);
      uint8_t resp[7];
      resp[0] = 1;
      resp[1] = errors[i].code;
      memcpy(&resp[2], &errors[i].tfs, 4);
      resp[6] = errors[i].times;
      slave.respond(resp, 7);
      return;
    }
  }
  uint8_t resp[7] = {};
  slave.respond(resp, 7);
}

void cmdClearErrors(const uint8_t*, uint8_t) {
  Serial.println("cmdClearErrors");
  memset(errors, 0, sizeof(errors));
  slave.respondByte(0x01);
}

void logS(String str){
  if(!isDebug)
    return;
  Serial.println(str);
}

void logI(String str, int i){
  if(!isDebug)
    return;
  Serial.print(str);
  Serial.print(" : ");
  Serial.println(i);
}