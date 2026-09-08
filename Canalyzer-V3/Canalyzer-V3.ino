

#include <CAN.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>


const int rs = 4;
const int en = 5;
const int d4 = A2;
const int d5 = A3;
const int d6 = A4;
const int d7 = A5;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

const int SPI_CS_PIN  = 10;
const int SPI_INT_PIN = 2;

const int SAVE_BUTTON_PIN = 7;  //save
const int TEST_BUTTON_PIN = 9;  //test

const uint8_t REV_MANUFACTURER_ID = 5; //frc bullshit

const int MAX_DEVICES = 40;  //storage limit of uno
  

//EEPROM STORAGE SHIT

const uint16_t EEPROM_MAGIC = 0xCAFE;

const int EEPROM_MAGIC_ADDRESS = 0;

const int EEPROM_COUNT_ADDRESS = 2;

const int EEPROM_DEVICE_LIST_ADDRESS = 4;

uint16_t savedDevices[MAX_DEVICES];
uint8_t savedDeviceCount = 0;

uint16_t detectedDevices[MAX_DEVICES];
uint8_t detectedDeviceCount = 0;


// ===================================FUNCTION DECLARATIONS============================================


void nMsg(const char* top, const char* bottom = nullptr);
void printCountLine(uint8_t count, const char* label = "");

void saveKnownGoodBus();
bool loadSavedBus();

void testBus();

void clearDetectedDevices();
void processCANMessage();

bool deviceAlreadyStored(uint16_t *array, uint8_t count, uint16_t key);
void addDetectedDevice(uint16_t key);

uint16_t makeDeviceKey(uint8_t deviceType, uint8_t deviceNumber);

uint8_t getDeviceType(uint32_t canId);
uint8_t getDeviceNumber(uint32_t canId);
uint8_t getManufacturer(uint32_t canId);

void printDeviceList(uint16_t *array, int count);

bool buttonPressed(int pin);

// ===========================================SETUP==============================================


void setup() {

  Serial.begin(9600);
  pinMode(SAVE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(TEST_BUTTON_PIN, INPUT_PULLUP);
  lcd.begin(16, 2);
  nMsg("Initializing...", "");

  //setup mcp thingy

  CAN.setPins(SPI_CS_PIN, SPI_INT_PIN);
  CAN.setClockFrequency(8E6);  //initiate 8mhz crystal

  if (CAN.begin(1000E3)) {   //init @ 1Mbps (FRC COMM SPEED)
    Serial.println("MCP2515 initialized successfully.");
    nMsg("CAN Ready", "@ 1 Mbps");
    delay(1500);
  } else {
    Serial.println("ERROR: MCP2515 initialization failed.");
    nMsg("MCP2515 ERROR", "Check Wiring");
    while (1);
  }


 
  if (loadSavedBus()) {  //find my bs in eeprom

    Serial.print("Loaded ");
    Serial.print(savedDeviceCount);
    Serial.println(" saved devices.");

    nMsg("Saved Bus Loaded", "");
    printCountLine(savedDeviceCount);
    delay(1500);

  } else {
    Serial.println("No valid saved bus found.");
    nMsg("No Bus Saved", "Or Invalid Data");
    delay(1500);
  }


  nMsg("Ready", "SAVE or TEST");

  Serial.println();
  Serial.println("--------------------------------");
  Serial.println("CAN BUS DIAGNOSTIC READY");
  Serial.println("SAVE button = pin 7");
  Serial.println("TEST button = pin 9");
  Serial.println("--------------------------------");
}


// =================================LOOP====================================================

void loop() {

    //always process to prevent mcp buffer filling up while waiting for button
  processCANMessage();

  if (buttonPressed(SAVE_BUTTON_PIN)) {  //save
    saveKnownGoodBus();

    while (digitalRead(SAVE_BUTTON_PIN) == LOW) { //wait for button release
      processCANMessage();
    }
    delay(200);
  }

  if (buttonPressed(TEST_BUTTON_PIN)) {  //test
    testBus();

    while (digitalRead(TEST_BUTTON_PIN) == LOW) {  //wait for button release
      processCANMessage();
    }

    delay(200);
  }
}

// =========================================================FUNCTIONS=============================================

void processCANMessage() {

  int packetSize = CAN.parsePacket();
  if (!packetSize) {
    return;
  }

  // only give a shit about extended can frames
  if (!CAN.packetExtended()) {
    return;
  }

  uint32_t canId = CAN.packetId();

  uint8_t manufacturer = getManufacturer(canId);  //parse out rev id
  if (manufacturer != REV_MANUFACTURER_ID) {
    return;
  }


  //get the actual device number.
  uint8_t deviceNumber = getDeviceNumber(canId);

  // Get the FRC device type.
  uint8_t deviceType = getDeviceType(canId);

  //make one identifier representing dev.
  uint16_t deviceKey = makeDeviceKey(deviceType, deviceNumber);

  //add it to current list if not there already
  addDetectedDevice(deviceKey);
}
//=====================================================================

//parse
uint8_t getDeviceNumber(uint32_t canId) {
  //frc dev number is bottom 6bit of 29 bit can arbitration id.

  return (uint8_t)(canId & 0x3F);
}
//=====================================================================

uint8_t getManufacturer(uint32_t canId) {

  // FRC CAN manufacturer field
  return (uint8_t)((canId >> 16) & 0xFF);
}
//=====================================================================

uint8_t getDeviceType(uint32_t canId) {

  //frc can device type  (occ. bits 24-28)
  return (uint8_t)((canId >> 24) & 0x1F);
}

//=====================================================================

//make device key to store in eeprom
uint16_t makeDeviceKey(uint8_t deviceType, uint8_t deviceNumber) {

/*
  stores both device type & number
  1 16 bit value
  upper 5 bit - device type
  lower 6 bit - device num
*/
  return ((uint16_t)deviceType << 6) | deviceNumber;
}
//=====================================================================

//check if its already stored
bool deviceAlreadyStored(uint16_t *array, uint8_t count, uint16_t key) {
  for (uint8_t i = 0; i < count; i++) {
    if (array[i] == key) {
      return true;
    }
  }
  return false;
}
//=====================================================================

//look at the name for this one dumbass
void addDetectedDevice(uint16_t key) {
  if (detectedDeviceCount >= MAX_DEVICES) {
    return;
  }
  if (deviceAlreadyStored(
        detectedDevices,
        detectedDeviceCount,
        key)) {

    return;
  }

  detectedDevices[detectedDeviceCount] = key;
  detectedDeviceCount++;

  //print new dev. to serial
  uint8_t deviceNumber = key & 0x3F;
  uint8_t deviceType = key >> 6;

  Serial.print("Detected REV device - Type: ");
  Serial.print(deviceType);

  Serial.print("  CAN ID: ");
  Serial.println(deviceNumber);
}


//=====================================================================

//the fuck do you think this one does
void clearDetectedDevices() {

  detectedDeviceCount = 0;

  for (uint8_t i = 0; i < MAX_DEVICES; i++) {
    detectedDevices[i] = 0;
  }
}
//=====================================================================

//save the BUS to EEPROM
void saveKnownGoodBus() {

  Serial.println();
  Serial.println("================================");
  Serial.println("LEARNING KNOWN-GOOD BUS");
  Serial.println("================================");


  nMsg("Learning Bus...", "Please Wait");

  clearDetectedDevices();

  // --CHANGE--   listen for 3 secs and collect unique rev devices, rather than messages
  unsigned long startTime = millis();

  while (millis() - startTime < 3000) {

    processCANMessage();
  }

  //check if something was found
  if (detectedDeviceCount == 0) {

    Serial.println("ERROR: No REV devices detected.");
    nMsg("SAVE FAILED", "No Devices");
    delay(2000);
    nMsg("Ready", "SAVE or TEST");
    return;
  }

//copy detected list into saved
  savedDeviceCount = detectedDeviceCount;
  for (int i = 0; i < savedDeviceCount; i++) {
    savedDevices[i] = detectedDevices[i];
  }

  //save to eeprom.
  //idk how to do this shit so chatgpt did this one.
  EEPROM.put(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC);
  EEPROM.put(EEPROM_COUNT_ADDRESS, savedDeviceCount);

  int address = EEPROM_DEVICE_LIST_ADDRESS;

  for (uint8_t i = 0; i < savedDeviceCount; i++) {
    EEPROM.put(address, savedDevices[i]);
    address += sizeof(uint16_t);
  }

  //print results  (SERIAL)
  Serial.println();
  Serial.print("Saved ");
  Serial.print(savedDeviceCount);
  Serial.println(" devices.");
  Serial.println("Known-good device list:");
  printDeviceList(savedDevices, savedDeviceCount);


  // LCD

  nMsg("Bus Saved!", "");
  printCountLine(savedDeviceCount);
  delay(2500);
  nMsg("Ready", "SAVE or TEST");
}

//=====================================================================

//yeah no i have no fucking idea how it works
//if it works dont touch this shit
bool loadSavedBus() {
  uint16_t magic;
  uint8_t count;

  EEPROM.get(EEPROM_MAGIC_ADDRESS, magic);

  if (magic != EEPROM_MAGIC) {
    return false;
  }

  EEPROM.get(EEPROM_COUNT_ADDRESS, count);

  if (count == 0 || count > MAX_DEVICES) {
    return false;
  }


  savedDeviceCount = count;


  int address = EEPROM_DEVICE_LIST_ADDRESS;


  for (uint8_t i = 0; i < savedDeviceCount; i++) {

    EEPROM.get(
      address,
      savedDevices[i]
    );

    address += sizeof(uint16_t);
  }


  return true;
}

//=====================================================================

void testBus() {

  Serial.println();
  Serial.println("TESTING CAN BUS");

  //make sure of good bus in savefile
  if (savedDeviceCount == 0) {

    Serial.println("ERROR: No known-good bus saved.");

    nMsg("NO BUS SAVED", "Press SAVE");

    delay(2000);

    nMsg("Ready", "SAVE or TEST");

    return;
  }

    //CHANGE  important-- clears prev. test results
    //fixes issue where old ids could remain in buffer and f with next test
  clearDetectedDevices();

  nMsg("Canalyzing", "Please Wait");

  //listen for 2 sec
  unsigned long startTime = millis();

  while (millis() - startTime < 2000) {
    processCANMessage();
  }

  //count missing devices
  uint8_t missingCount = 0;

  uint16_t firstMissing = 0;

  for (uint8_t i = 0; i < savedDeviceCount; i++) {
    if (!deviceAlreadyStored(detectedDevices, detectedDeviceCount, savedDevices[i])) {
      if (missingCount == 0) {
        firstMissing = savedDevices[i];
      }
      missingCount++;
    }
  }

  //do this if all is well  (0.0001% chance *insert steph curry shooting basketball from moon gif here*)
  //(can bus ok case)
  if (missingCount == 0) {

    Serial.println();
    Serial.println("======== BUS OK ========");

    Serial.print("Detected ");
    Serial.print(detectedDeviceCount);
    Serial.print("/");
    Serial.print(savedDeviceCount);
    Serial.println(" devices.");

    nMsg("Bus OK");
    delay(2500);
    nMsg("Ready", "SAVE or TEST");

    return;
  }


  // ==========================================================
  //run if bus not ok
  //yeah lwk predicted

  Serial.println();
  Serial.println("******** BUS ERROR ********");

  Serial.print("Detected ");
  Serial.print(detectedDeviceCount);
  Serial.print("/");
  Serial.print(savedDeviceCount);
  Serial.println(" devices.");

  Serial.print("Missing ");
  Serial.print(missingCount);
  Serial.println(" device(s):");


  // Print EVERY missing device to Serial

  for (uint8_t i = 0; i < savedDeviceCount; i++) {

    if (!deviceAlreadyStored(
          detectedDevices,
          detectedDeviceCount,
          savedDevices[i])) {

      uint8_t deviceNumber =
        savedDevices[i] & 0x3F;

      uint8_t deviceType =
        savedDevices[i] >> 6;

      Serial.print("  CAN ID: ");
      Serial.print(deviceNumber);

      Serial.print("  Type: ");
      Serial.println(deviceType);
    }
  }

  //lcd has limited columns(did i spell it right?) so put first missing dev. there
  uint8_t missingNumber =
    firstMissing & 0x3F;


  nMsg("CAN BUS TEST", "");
  lcd.setCursor(0, 1);
  lcd.print(detectedDeviceCount);
  lcd.print('/');
  lcd.print(savedDeviceCount);
  lcd.print(" MISSING ");
  lcd.print(missingNumber);

  delay(4000);

  nMsg("Ready", "SAVE or TEST");
}

//=====================================================================

void printDeviceList(uint16_t *array, uint8_t count) {

  for (uint8_t i = 0; i < count; i++) {

    uint8_t deviceNumber =
      array[i] & 0x3F;

    uint8_t deviceType =
      array[i] >> 6;
    Serial.print("  ");
    Serial.print(i + 1);
    Serial.print(": CAN ID ");
    Serial.print(deviceNumber);
    Serial.print("  Type ");
    Serial.println(deviceType);
  }
}
//=====================================================================

void printCountLine(uint8_t count, const char* label) {
  lcd.setCursor(0, 1);
  if (label != nullptr && *label != '\0') {
    lcd.print(label);
    lcd.print(' ');
  }
  lcd.print(count);
  lcd.print(" Devices");
}
//=====================================================================

//button handler
bool buttonPressed(int pin) {

  if (digitalRead(pin) != LOW) {
    return false;
  }
  delay(50); // i think its called a debounce?


  return digitalRead(pin) == LOW;
}

//=====================================================================

//absolutely genius function made by yours truly
void nMsg(const char* tcom, const char* bcom) {
  lcd.clear();
  lcd.print(tcom);
  if (bcom != nullptr && *bcom != '\0') {
    lcd.setCursor(0, 1);
    lcd.print(bcom);
  }
}