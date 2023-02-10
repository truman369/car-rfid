#include <Arduino.h>
#include <OneWire.h>
#include <LowPower.h>
#include <EEPROM.h>

#define READER_DELAY 1000 // delay while reader is sending the same key code
#define LOCK_DELAY 500    // impulse duration for lock moving
#define HIGH_TONE 2000    // high frequency for buzzer
#define LOW_TONE 200      // low frequency for buzzer

#define KEY_CNT_ADDR 511 // EEPROM address for saved keys count
#define KEY_MAX_CNT 10   // max count of keys to save
#define KEY_LENGTH 8     // key length in bytes
#define KEY_S_ADDR 0     // EEPROM start address for saved keys

#define WAKEUP_PIN 6       // wake up pin for external interrupt (D0 from reader)
#define VECTOR PCINT2_vect // alias for PCINT vector PCINT2 (D02-D07) <= WAKEUP_PIN
#define PCMSK PCMSK2       // alias for PCMSK (PCINT2)
#define DALLAS_PIN 7       // dallas ibutton 1-wire pin (D1 from reader)
#define LOCK_STATUS_PIN 8  // normal opened pin, connected to GND when lock is closed
#define BUZZER_PIN 9       // buzzer pin
#define JP1_PIN 10         // cfg pin
#define JP2_PIN 11         // cfg pin

// |   0   |   1   |   2   |   3   |   4   |
// |       |    _  |  _    |  _ _  |       |
// |  . .  |  . .  |  . .  |  . .  | |. .| |
// | |. .| |  . .  |  . .  |  . .  |  . .  |
// |       |    —  |  —    |  — —  |       |

#define MODE_NORMAL 0 // normal mode (no jumper or bottom)
#define MODE_ADD 1    // adding new keys (right jumper)
#define MODE_REMOVE 2 // removing keys (left jumper)
#define MODE_UNUSED 3 // reserved for future (left+right jumpers)
#define MODE_WIPE 4   // clear EEPROM (top jumper)

// relay module pins to move lock, need to send LOW signal
// first pair to open, second pair to close
const byte RELAY_PINS[4] = {2, 3, 4, 5};

// aliases
#define OPEN_LOCK moveLock(RELAY_PINS[0], RELAY_PINS[1])
#define CLOSE_LOCK moveLock(RELAY_PINS[2], RELAY_PINS[3])
#define LOCK_IS_OPENED digitalRead(LOCK_STATUS_PIN)

OneWire ds(DALLAS_PIN); // dallas ibutton wire
byte mode = 0;          // device operating mode
byte key_count = 0;     // count of saved keys
byte valid_keys[KEY_MAX_CNT][KEY_LENGTH];

// print hex key to //Serial
// void printKey(byte *key)
// {
//   for (byte i = 0; i < KEY_LENGTH; i++)
//   {
//     if (key[i] < 0x10)
//     {
//       Serial.print('0'); // print leading zero
//     }
//     Serial.print(key[i], HEX); // print byte
//     Serial.print(' ');         // space between bytes
//   }
//   // Serial.println();
// }

// move lock via relay pins
void moveLock(byte pin1, byte pin2)
{
  digitalWrite(pin1, LOW); // power up solenoid
  digitalWrite(pin2, LOW);
  delay(LOCK_DELAY);        // wait for lock moving
  digitalWrite(pin1, HIGH); // power down solenoid
  digitalWrite(pin2, HIGH);
}

// external interrupt handler for PCIINT2 (D02-D07)
ISR(VECTOR) {}

// wipe EEPROM
void wipeEEPROM()
{
  EEPROM.update(KEY_CNT_ADDR, 0);
  byte data[KEY_MAX_CNT * KEY_LENGTH];
  memset(data, 0, sizeof(data));
  EEPROM.put(KEY_S_ADDR, data);
  // Serial.println("EEPROM data cleared");
}

// beep for mode recognition
void modeBeep(byte mode)
{
  for (byte i = 0; i < mode; i++)
  {
    tone(BUZZER_PIN, 1000);
    delay(50);
    noTone(BUZZER_PIN);
    delay(50);
  }
}

// check for valid key
bool keyIsValid(byte *key)
{
  bool isValid = false;
  for (byte i = 0; i < sizeof(valid_keys) / KEY_LENGTH; i++)
  {
    if (memcmp(key, valid_keys[i], KEY_LENGTH) == 0)
    {
      isValid = true;
      break; // don't check other keys
    }
  }
  return isValid;
}

// add key handler
bool addKey(byte *key)
{
  if (keyIsValid(key))
  {
    // Serial.println("Key already exists!");
    return false;
  }
  else if (key_count == KEY_MAX_CNT)
  {
    // Serial.println("Max count of keys reached!");
    return false;
  }
  for (byte i = 0; i < KEY_LENGTH; i++)
  {
    valid_keys[key_count][i] = key[i];
  }
  EEPROM.put(KEY_S_ADDR, valid_keys);
  key_count++;
  EEPROM.put(KEY_CNT_ADDR, key_count);
  // Serial.println("Key added");
  return true;
}

// remove key handler
bool removeKey(byte *key)
{
  bool flag = false;
  for (byte i = 0; i < sizeof(valid_keys) / KEY_LENGTH; i++)
  {
    if (flag || memcmp(key, valid_keys[i], KEY_LENGTH) == 0)
    {
      flag = true;
      for (byte j = 0; j < KEY_LENGTH; j++)
      {
        valid_keys[i][j] = valid_keys[i + 1][j];
      }
    }
  }
  if (flag)
  {
    for (byte i = 0; i < KEY_LENGTH; i++)
    {
      valid_keys[key_count][i] = 0;
    }
    EEPROM.put(KEY_S_ADDR, valid_keys);
    key_count--;
    EEPROM.put(KEY_CNT_ADDR, key_count);
    // Serial.println("Key removed");
    return true;
  }
  // Serial.println("Key not found");
  return false;
}

// normal mode handler
bool lockHandler(byte *key)
{
  if (keyIsValid(key))
  {
    if (LOCK_IS_OPENED)
    {
      // Serial.println("Lock is opened, closing...");
      CLOSE_LOCK;
      if (!LOCK_IS_OPENED)
      {
        // Serial.println("Lock closed sucessfully");
        return true;
      }
      else
      {
        // Serial.println("Failed to close lock");
        return false;
      }
    }
    else
    {
      // Serial.println("Lock is closed, opening...");
      OPEN_LOCK;
      if (LOCK_IS_OPENED)
      {
        // Serial.println("Lock opened sucessfully");
        return true;
      }
      else
      {
        // Serial.println("Failed to open lock");
        return false;
      }
    }
  }
  else
  {
    // Serial.println("Invalid key");
    return false;
  }
}

void setup()
{
  // Serial.begin(9600);
  // Serial.println("Loading...");
  for (byte pin : RELAY_PINS)
  {
    pinMode(pin, OUTPUT);    // init relay pins
    digitalWrite(pin, HIGH); // HIGH - relay is disabled
  }
  pinMode(LOCK_STATUS_PIN, INPUT_PULLUP);

  // check configuration jumper
  pinMode(JP1_PIN, INPUT_PULLUP);
  pinMode(JP2_PIN, INPUT_PULLUP);
  mode += !digitalRead(JP1_PIN);
  mode += !digitalRead(JP2_PIN) * 2;
  if (mode == 0)
  { // check if cfg pins are connected to each other
    pinMode(JP1_PIN, OUTPUT);
    digitalWrite(JP1_PIN, LOW);
    mode += !digitalRead(JP2_PIN) * 4;
  }
  // Serial.println("Mode: " + String(mode));

  // clear EEPROM on wipe mode
  if (mode == MODE_WIPE)
  {
    wipeEEPROM();
  }

  // get key count from EEPROM
  key_count = EEPROM.read(KEY_CNT_ADDR);
  if (key_count > KEY_MAX_CNT)
  { // flush keys on incorrect value
    key_count = 0;
    wipeEEPROM();
  }

  // read saved keys from EEPROM
  EEPROM.get(KEY_S_ADDR, valid_keys);

  // Serial.println("Saved keys:");
  //  for (byte i = 0; i < key_count; i++)
  //  {
  //    printKey(valid_keys[i]);
  //  }

  pinMode(WAKEUP_PIN, INPUT_PULLUP);                // init wake up pin for interrupts
  PCICR |= (1 << digitalPinToPCICRbit(WAKEUP_PIN)); // enable PCINT interrupt for group
  PCMSK |= (1 << digitalPinToPCMSKbit(WAKEUP_PIN)); // enable PCINT interrupt for pin

  tone(BUZZER_PIN, 500); // startup beep
  delay(50);
  noTone(BUZZER_PIN);
  modeBeep(mode);
  // delay(500);
}

void loop()
{
  // sleep and wake up on external interrupt by reader
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  delay(50); // delay for wake up
  // Serial.println("Wake up, Neo!");
  if (ds.reset())
  {                 // if card is detected
    ds.write(0x33); // send reader cmd to receive card data
    // read 8 bytes of dallas key
    byte key_buf[KEY_LENGTH]; // key buffer
    for (byte i = 0; i < KEY_LENGTH; i++)
    {
      key_buf[i] = ds.read();
    }
    // check family key and CRC
    if (key_buf[0] == 0x01 && key_buf[KEY_LENGTH - 1] == ds.crc8(key_buf, KEY_LENGTH - 1))
    {
      bool res; // result of handler
      switch (mode)
      {
      case MODE_NORMAL:
        res = lockHandler(key_buf);
        break;
      case MODE_ADD:
        res = addKey(key_buf);
        break;
      case MODE_REMOVE:
        res = removeKey(key_buf);
        break;
      default:
        break;
      }
      modeBeep(mode);
      // beep with high tone on ok and low tone on fail
      uint16_t toneFreq;
      toneFreq = (res) ? HIGH_TONE : LOW_TONE;
      tone(BUZZER_PIN, toneFreq);
      delay(READER_DELAY); // delay while reader is sending the same key
      noTone(BUZZER_PIN);
    }
  }
}
