#include <EEPROM.h>

void InitEEPROM() {
  sizeErr = sizeof(errors[0]);
  errLen = sizeof(errors)/sizeErr;
    LoadErrors();                     // загружаем текущее состояние
    if (false) {            // первый запуск
        if (isDebug) Serial.println(F("EEPROM Init - first run"));
        int i = 0;
        while (true) {
            uint16_t code = pgm_read_word(&errorDescriptions[i].code);
            if (code == 0) break;

            errors[i].code  = code;
            errors[i].tfs   = 0;
            errors[i].times = 0;
            i++;
        }
        EEPROM.put(0, errors);        // сохраняем только разрешённые коды
        if (isDebug) Serial.print(F("Initialized ")); Serial.print(errLen); Serial.println(F(" error slots"));
    }
}

void LoadErrors() {
  EEPROM.get(0, errors);
}

int IndexOfError(uint8_t code) {
  for (int i = 0; i < errLen; i++) {
    if (errors[i].code == code) {
      return i;
    }
  }
  return -1;
}

// Проверка, разрешён ли код ошибки
bool IsErrorCodeAllowed(uint8_t code) {
    for (int i = 0; ; i++) {
        uint8_t c = pgm_read_word(&errorDescriptions[i].code);
        if (c == 0) break;
        if (c == code) return true;
    }
    return false;
}

void SaveError(uint8_t code) {
  if (!IsErrorCodeAllowed(code)) {
    if (isDebug) { Serial.print(F("Error code ")); Serial.print(code); Serial.println(F(" not allowed")); }
    return;
  }
  logI("encountered", code);

  int i = IndexOfError(code);
  if (i == -1) return;

  errors[i].times++;
  if (errors[i].tfs == 0) {
    errors[i].tfs = millis();
  }
  EEPROM.put(sizeErr * i, errors[i]);
}

void ClearAllErrors() {
  for (int i = 0; i < errLen; i++) {
    errors[i].tfs   = 0;
    errors[i].times = 0;
  }
  EEPROM.put(0, errors);
  if (isDebug) Serial.println(F("All errors cleared (times and tfs reset)"));
}

void ResetError(uint8_t code){
  int i = IndexOfError(code);
  if (i == -1) return;
  errors[i].tfs=0;
  errors[i].times=0;
  EEPROM.put(sizeErr*i, errors[i]);
  logI("reseted", code);
}

void SendError(int err){
  uint8_t flag=0;
  for(int i=0; i<errLen; i++)
  {
    if(errors[i].times>0)
    {
      if(err==0)
      {
        flag=1;
        I2C_writeAnything(flag);
        I2C_writeAnything(errors[i].code);
        I2C_writeAnything(errors[i].tfs);
        I2C_writeAnything(errors[i].times);
        logI("SendError", errors[i].code);
        return;
      }
      err--;
    }
  }
  logI("SendError was UOR", nextError);
  I2C_writeAnything(flag);
}