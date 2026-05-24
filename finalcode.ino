#include <RTClib.h>
#include <BluetoothSerial.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <LiquidCrystal_I2C.h>

RTC_DS3231 rtc;
BluetoothSerial SerialBT;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Forward declarations
void printMenu(Stream &output);
void printAdminMenu(Stream &output);
void updateLCD();
void centerPrint(String text, uint8_t row);
void processBluetoothInput();

// SYSTEM SETTINGS
int userAlarmHour = 6;
int userAlarmMinute = 0;
int scheduledRunSeconds = 60; // Default 1 min (60s)
int retryCount = 3; // Default 3
int retryIntervalMinutes = 1; // Default 1 min
int alarmIntervalDays = 2; // Default 2 days
bool isDurationInMinutes = false;

// SYSTEM FLAGS
bool isAutoMode = true;
bool scheduleMode = false;
bool motorState = false;
bool isScheduledRunActive = false;
bool retryInProgress = false;
bool manualMotorState = false;
bool btMenuActive = false;
bool serialMenuActive = true;
bool continuousMode = false;
bool adminMode = false;
bool wasBluetoothConnected = false;
bool rtcAvailable = true;
bool isAlternatingDisplay = false; // For LCD alternation
bool retryWaitDisplay = false; // Track RETRY WAIT vs RETRY X/3

// BUTTON DEBOUNCE FLAGS
bool isResetButtonDebouncing = false;

// SYSTEM TIMERS
unsigned long scheduledRunStartTime = 0;
unsigned long retryStartTime = 0;
unsigned long lastSecondUpdate = 0;
unsigned long resetBtnPressStartTime = 0;
unsigned long lastStatusUpdate = 0;
unsigned long stateCheckStartTime = 0;
unsigned long lastRetryWaterCheck = 0;
unsigned long lastChangeTime = 0; // For LCD temporary display
unsigned long startupDisplayTime = 0; // For ENARXI startup
unsigned long lastScheduledEventTime = 0; // For LCD alternation

// SCHEDULE TRACKING
DateTime lastScheduledRun(2025, 5, 28, 6, 0, 0); // 2 days before compile time

// RETRY STATE
int currentRetry = 0;

// LCD EVENT TRACKING
String lastChangeMessage = ""; // Store button/BT change
String currentScheduledEvent = ""; // Current Scheduled Mode event

// GPIO PINS
const int DRY_RUN_SENSOR_PIN = 27;
const int FLOAT_SENSOR_PIN = 14; // Updated to GPIO 14
const int MOTOR_PIN = 23;
const int SCHEDULE_BUTTON = 16; // ON/OFF switch
const int AUTO_MANUAL_BUTTON = 17; // ON/OFF switch
const int RESET_BUTTON = 18; // Push button
const int LED_MODE_MANUAL = 19; // Manual Mode LED
const int LED_MODE_SCHEDULED = 33; // Scheduled Mode LED
const int LED_WATER_PIN = 25;
const int LED_TANK_FULL_PIN = 4;
const int LED_MOTOR_PIN = 5;

// BUTTON DEBOUNCE
bool lastScheduleBtnState = HIGH;
bool lastAutoModeState = HIGH;
bool lastResetBtnState = HIGH;

// PREV_STATE
bool prevMotorState = false;
bool prevWaterState = false;
bool prevTankFullState = false; // Default to tank not full
bool prevScheduleState = false;
bool prevModeState = true;

// STATE TRACKING
bool lastStableWaterState = false;
bool lastStableTankState = false; // Default to tank not full
bool lastStableMotorState = false; // For motor state tracking
bool stateStable = false;

// MENU STATE
int serialMenuState = 0;
String serialInput = "";
int btMenuState = 0;
String btInput = "";
String prevInput = ""; // Track previous input for option 11/12
const String ADMIN_PASSWORD = "Enarxi0000";

const unsigned long BUTTON_PRESS_HOLD = 3000; // 3 seconds
const unsigned long STATUS_UPDATE_INTERVAL = 60000UL; // 60s
const unsigned long STATE_CONFIRMATION_DELAY = 5000UL; // 5 seconds
const unsigned long RETRY_WATER_CHECK_INTERVAL = 5000UL; // 5s for water check
const unsigned long LCD_CHANGE_DURATION = 5000UL; // 5s for temp display
const unsigned long STARTUP_DISPLAY_DURATION = 5000UL; // 5s for ENARXI
const unsigned long LCD_ALTERNATE_DURATION = 5000UL; // 5s for Scheduled alternation

// SYSTEM HELPERS
bool isValidInteger(String input) {
  if (input.length() == 0) return false;
  for (size_t i = 0; i < input.length(); i++) {
    if (i == 0 && input[i] == '-') continue;
    if (!isDigit(input[i])) return false;
  }
  return true;
}

void printStatus(Stream &output, DateTime now, bool waterPresent, bool tankFull) {
  String timeStr = rtcAvailable ? 
    String((now.hour() < 10 ? "0" : "") + String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute())) : 
    "00:00";
  String modeStr = isAutoMode ? (scheduleMode ? "SCHEDULED" : "AUTO") : "MANUAL";
  String status = String("Time: ") + timeStr +
                  " | Water: " + String(waterPresent ? "YES" : "NO") +
                  " | Tank: " + String(tankFull ? "YES" : "NO") +
                  " | Motor: " + String(motorState ? "ON" : "OFF") +
                  " | Mode: " + modeStr;
  output.println(status);
}

void updateMotorState(bool newState) {
  if (motorState != newState) {
    motorState = newState;
    digitalWrite(MOTOR_PIN, motorState ? HIGH : LOW);
    digitalWrite(LED_MOTOR_PIN, motorState ? HIGH : LOW);
  }
}

void printStableState(bool waterPresent, bool tankFull, bool motorOn) {
  String state = String(waterPresent ? "Water Found" : "No Water") + ", " +
                 String(tankFull ? "Tank Full" : "Tank Not Full") + ", " +
                 String(motorOn ? "Motor ON" : "Motor OFF");
  Serial.println(state);
  SerialBT.println("Water " + state);
}

void resetSystem() {
  userAlarmHour = 6;
  userAlarmMinute = 0;
  scheduledRunSeconds = 60;
  retryCount = 3;
  retryIntervalMinutes = 1;
  alarmIntervalDays = 2;
  isDurationInMinutes = false;

  isAutoMode = true;
  scheduleMode = false;
  motorState = false;
  manualMotorState = false;
  isScheduledRunActive = false;
  retryInProgress = false;
  continuousMode = false;
  adminMode = false;
  btMenuActive = false;
  serialMenuActive = true;
  wasBluetoothConnected = false;
  isAlternatingDisplay = false;
  currentScheduledEvent = "";
  retryWaitDisplay = false;

  scheduledRunStartTime = 0;
  retryStartTime = 0;
  lastSecondUpdate = 0;
  lastStatusUpdate = 0;
  stateCheckStartTime = 0;
  lastRetryWaterCheck = 0;
  lastChangeTime = 0;
  lastScheduledEventTime = 0;
  lastChangeMessage = "";
  startupDisplayTime = millis();

  serialMenuState = 0;
  btMenuState = 0;
  serialInput = "";
  btInput = "";
  prevInput = "";

  updateMotorState(false);
  digitalWrite(LED_MODE_MANUAL, LOW);
  digitalWrite(LED_MODE_SCHEDULED, LOW);
  digitalWrite(LED_WATER_PIN, LOW);
  digitalWrite(LED_TANK_FULL_PIN, LOW);
  digitalWrite(LED_MOTOR_PIN, LOW);

  lastScheduledRun = DateTime(2025, 5, 28, 6, 0, 0);
  currentRetry = 0;

  lastStableTankState = false;
  prevTankFullState = false;

  Serial.println("System Reset");
  SerialBT.println("System Reset");
  lastChangeMessage = "SYSTEM RESET";
  lastChangeTime = millis();
  printMenu(Serial);
}

// ADDON SETUP
void initializeAddons() {
  pinMode(LED_MODE_MANUAL, OUTPUT);
  pinMode(LED_MODE_SCHEDULED, OUTPUT);
  pinMode(LED_WATER_PIN, OUTPUT);
  pinMode(LED_TANK_FULL_PIN, OUTPUT);
  pinMode(LED_MOTOR_PIN, OUTPUT);
  digitalWrite(LED_MODE_MANUAL, LOW);
  digitalWrite(LED_MODE_SCHEDULED, LOW);
  digitalWrite(LED_WATER_PIN, LOW);
  digitalWrite(LED_TANK_FULL_PIN, LOW);
  digitalWrite(LED_MOTOR_PIN, LOW);

  lcd.begin();
  lcd.backlight();
  centerPrint("ENARXI", 0);
  centerPrint("WATER REFILL SYS", 1);
  startupDisplayTime = millis();
}

void updateAddons(bool waterPresent, bool tankFull) {
  digitalWrite(LED_MODE_MANUAL, (!isAutoMode) ? HIGH : LOW);
  digitalWrite(LED_MODE_SCHEDULED, scheduleMode ? HIGH : LOW);
  digitalWrite(LED_WATER_PIN, waterPresent ? HIGH : LOW);
  digitalWrite(LED_TANK_FULL_PIN, tankFull ? HIGH : LOW);
  digitalWrite(LED_MOTOR_PIN, motorState ? HIGH : LOW);
}

void centerPrint(String text, uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                "); // Clear row
  int padding = (16 - text.length()) / 2;
  if (padding < 0) padding = 0;
  String paddedText = String("                ").substring(0, padding) + text.substring(0, 16 - padding);
  lcd.setCursor(0, row);
  lcd.print(paddedText);
}

void updateLCD() {
  static String lastLine0 = "";
  static String lastLine1 = "";
  static bool lastAlternatingState = false;
  static String lastScheduledEvent = "";

  DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  String timeStr = rtcAvailable ? 
    String((now.hour() < 10 ? "0" : "") + String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute())) : 
    "00:00";

  String line0, line1;

  // Startup display
  if (millis() - startupDisplayTime < STARTUP_DISPLAY_DURATION) {
    line0 = "ENARXI";
    line1 = "WATER REFILL SYS";
  }
  // Temporary event messages
  else if (millis() - lastChangeTime < LCD_CHANGE_DURATION && lastChangeMessage != "") {
    line0 = scheduleMode ? 
      String(userAlarmHour < 10 ? "0" : "") + String(userAlarmHour) + ":" + 
      String(userAlarmMinute < 10 ? "0" : "") + String(userAlarmMinute) : timeStr;
    line1 = lastChangeMessage.substring(0, 16);
  }
  // Scheduled Mode
  else if (scheduleMode) {
    String alarmStr = String(userAlarmHour < 10 ? "0" : "") + String(userAlarmHour) + ":" + 
                      String(userAlarmMinute < 10 ? "0" : "") + String(userAlarmMinute) + "A";
    line0 = timeStr + "     " + alarmStr; // 5 spaces for equal spacing (16 chars total)
    if (isScheduledRunActive) {
      line1 = "SCH RUN START";
      isAlternatingDisplay = false;
      retryWaitDisplay = false;
    } else if (retryInProgress && currentRetry > 0 && currentRetry <= retryCount) {
      isAlternatingDisplay = true;
      if (millis() - lastScheduledEventTime >= LCD_ALTERNATE_DURATION) {
        retryWaitDisplay = !retryWaitDisplay;
        lastScheduledEventTime = millis();
      }
      line1 = retryWaitDisplay ? "RETRY WAIT" : "RETRY " + String(currentRetry) + "/" + String(retryCount);
    } else {
      line1 = motorState ? "SCH MOTOR ON" : "SCH MOTOR OFF";
      isAlternatingDisplay = false;
      retryWaitDisplay = false;
    }
    // Handle alternation for scheduled events (non-retry)
    if (isAlternatingDisplay && !retryInProgress && millis() - lastScheduledEventTime >= LCD_ALTERNATE_DURATION) {
      currentScheduledEvent = (currentScheduledEvent == "") ? lastChangeMessage : "";
      lastScheduledEventTime = millis();
      line1 = currentScheduledEvent != "" ? currentScheduledEvent.substring(0, 16) : line1;
    }
  }
  // Auto or Manual Mode
  else {
    line0 = timeStr;
    line1 = isAutoMode ? 
      (motorState ? "AUTO MOTOR ON" : "AUTO MOTOR OFF") : 
      (manualMotorState ? "MANUAL MOTOR ON" : "MANUAL MOTOR OFF");
    isAlternatingDisplay = false;
    retryWaitDisplay = false;
  }

  // Update LCD only if content has changed
  if (line0 != lastLine0 || line1 != lastLine1 || isAlternatingDisplay != lastAlternatingState || currentScheduledEvent != lastScheduledEvent) {
    lcd.setCursor(0, 0);
    lcd.print("                ");
    lcd.setCursor(0, 0);
    lcd.print(line0.substring(0, 16));
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(line1.substring(0, 16));
    lastLine0 = line0;
    lastLine1 = line1;
    lastAlternatingState = isAlternatingDisplay;
    lastScheduledEvent = currentScheduledEvent;
  }
}

// SYSTEM SETUP
void setup() {
  Serial.begin(115200);
  SerialBT.begin("Water Refilling System");

  pinMode(DRY_RUN_SENSOR_PIN, INPUT_PULLUP);
  pinMode(FLOAT_SENSOR_PIN, INPUT_PULLUP);
  pinMode(SCHEDULE_BUTTON, INPUT_PULLUP);
  pinMode(AUTO_MANUAL_BUTTON, INPUT_PULLUP);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  if (!rtc.begin()) {
    Serial.println("RTC Not found!");
    SerialBT.println("RTC Not Found");
    rtcAvailable = false;
    lastChangeMessage = "RTC NOT FOUND";
    lastChangeTime = millis();
  } else {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  initializeAddons();

  Serial.println("Starting System...");
  SerialBT.println("Starting System...");
  lastChangeMessage = "SYSTEM STARTED";
  lastChangeTime = millis();
  printMenu(Serial);
  serialMenuActive = true;

  lastStableTankState = false;
  prevTankFullState = false;

  xTaskCreatePinnedToCore(bluetoothTask, "BT Task", 8192, NULL, 1, NULL, 0);
}

// CORE LOGIC
void loop() {
  handleSerialInput();
  processBluetoothInput();
  handleAutoManualButton();
  handleScheduleOrManualButton();
  handleResetButton();

  DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
  bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == LOW; // Active-low sensor

  static bool lastScheduledRunActive = false;
  static bool lastRetryInProgress = false;
  static bool lastContinuousMode = false;

  if (isAutoMode) {
    if (scheduleMode) {
      if (!rtcAvailable) {
        scheduleMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        retryWaitDisplay = false;
        Serial.println("Scheduled Mode Disabled - RTC Not Found");
        SerialBT.println("Scheduled Mode Disabled - RTC Not Found");
        lastChangeMessage = "SCH MODE DISABLED";
        lastChangeTime = millis();
      } else {
        bool isAlarmTime = (now.hour() == userAlarmHour && now.minute() == userAlarmMinute && now.second() <= 15);
        bool isIntervalElapsed = (now.unixtime() >= lastScheduledRun.unixtime() + (alarmIntervalDays * 86400UL));

        if (!isScheduledRunActive && !retryInProgress && !continuousMode && isAlarmTime && isIntervalElapsed) {
          isScheduledRunActive = true;
          scheduledRunStartTime = millis();
          currentRetry = 0;
          updateMotorState(true); // Motor ON regardless of water
          Serial.println("Scheduled Run STARTED");
          SerialBT.println("Scheduled Run STARTED");
          lastChangeMessage = "SCH RUN START";
          currentScheduledEvent = "SCH RUN START";
          isAlternatingDisplay = true;
          retryWaitDisplay = false;
          lastScheduledEventTime = millis();
          lastChangeTime = millis();
          lastScheduledRunActive = true;
        }

        if (isScheduledRunActive) {
          unsigned long elapsed = millis() - scheduledRunStartTime;
          if (millis() - lastSecondUpdate >= 1000) {
            Serial.print("Run Time: ");
            Serial.print(elapsed / 1000);
            Serial.println(" sec");
            lastSecondUpdate = millis();
          }

          if (elapsed >= scheduledRunSeconds * 1000UL) {
            isScheduledRunActive = false;
            lastScheduledRun = now;
            currentScheduledEvent = "";
            isAlternatingDisplay = false;
            retryWaitDisplay = false;
            if (waterPresent && !tankFull) {
              continuousMode = true;
              updateMotorState(true);
              Serial.println("Run Complete");
              SerialBT.println("Run Complete");
              lastChangeMessage = "RUN COMPLETE";
              lastChangeTime = millis();
            } else {
              continuousMode = false;
              updateMotorState(false);
              retryInProgress = retryCount > 0;
              retryStartTime = millis();
              currentRetry = 1;
              Serial.println("Run Complete");
              SerialBT.println("Run Complete");
              lastChangeMessage = "RUN COMPLETE";
              lastChangeTime = millis();
              if (retryInProgress && !lastRetryInProgress) {
                Serial.println("Starting Retries");
                SerialBT.println("Starting Retries");
                lastChangeMessage = "STARTING RETRIES";
                lastChangeTime = millis();
                isAlternatingDisplay = true;
                retryWaitDisplay = true;
              }
            }
            lastScheduledRunActive = false;
            lastRetryInProgress = retryInProgress;
          }
        }

        if (continuousMode && (!waterPresent || tankFull)) {
          continuousMode = false;
          updateMotorState(false);
          currentScheduledEvent = "";
          isAlternatingDisplay = false;
          retryWaitDisplay = false;
          Serial.println("Motor STOPPED");
          SerialBT.println("Motor STOPPED");
          lastChangeMessage = "MOTOR STOPPED";
          lastChangeTime = millis();
          lastContinuousMode = false;
        }

        if (retryInProgress) {
          if (millis() - lastSecondUpdate >= 1000) {
            Serial.print("Retry Wait: ");
            Serial.print((millis() - retryStartTime) / 1000);
            Serial.println(" sec");
            lastSecondUpdate = millis();
          }

          if (millis() - lastRetryWaterCheck >= RETRY_WATER_CHECK_INTERVAL) {
            if (waterPresent && !tankFull) {
              Serial.println("Water Detected - Starting Motor");
              SerialBT.println("Water Detected - Starting Motor");
              lastChangeMessage = "WATER DETECTED";
              currentScheduledEvent = "";
              lastChangeTime = millis();
              lastRetryWaterCheck = millis();
              updateMotorState(true);
              retryInProgress = false;
              continuousMode = true;
              isAlternatingDisplay = false;
              retryWaitDisplay = false;
            } else {
              lastRetryWaterCheck = millis();
            }
          }

          if (retryInProgress && (millis() - retryStartTime) >= retryIntervalMinutes * 60000UL) {
            Serial.println("Retry " + String(currentRetry) + " Started");
            SerialBT.println("Retry " + String(currentRetry) + " Started");
            lastChangeMessage = "RETRY " + String(currentRetry) + "/" + String(retryCount);
            currentScheduledEvent = "RETRY " + String(currentRetry) + "/" + String(retryCount);
            isAlternatingDisplay = true;
            retryWaitDisplay = false;
            lastScheduledEventTime = millis();
            lastChangeTime = millis();
            if (waterPresent && !tankFull) {
              updateMotorState(true);
              retryInProgress = false;
              continuousMode = true;
              currentScheduledEvent = "";
              isAlternatingDisplay = false;
              retryWaitDisplay = false;
              Serial.println("Retry " + String(currentRetry) + " Finished");
              Serial.println("Motor ON");
              SerialBT.println("Retry " + String(currentRetry) + " Finished");
              SerialBT.println("Motor ON");
              lastChangeMessage = "WATER DETECTED";
              lastChangeTime = millis();
            } else {
              Serial.println("Retry " + String(currentRetry) + " Finished");
              Serial.println("Motor OFF");
              SerialBT.println("Retry " + String(currentRetry) + " Finished");
              SerialBT.println("Motor OFF");
              lastChangeMessage = "RETRY FAILED";
              currentScheduledEvent = "";
              isAlternatingDisplay = false;
              retryWaitDisplay = false;
              lastChangeTime = millis();
              currentRetry++;
              retryStartTime = millis();
              lastRetryWaterCheck = millis();
              if (currentRetry <= retryCount) {
                Serial.println("Retry Wait Started");
                SerialBT.println("Retry Wait Started");
                lastChangeMessage = "RETRY WAIT";
                isAlternatingDisplay = true;
                retryWaitDisplay = true;
                lastChangeTime = millis();
              } else {
                retryInProgress = false;
                updateMotorState(false);
                currentScheduledEvent = "";
                isAlternatingDisplay = false;
                retryWaitDisplay = false;
                Serial.println("Retries FAILED");
                SerialBT.println("Retries FAILED");
                lastChangeMessage = "RETRIES FAILED";
                lastChangeTime = millis();
              }
            }
            lastRetryInProgress = retryInProgress;
          }
        }
      }
    } else {
      bool newMotorState = (waterPresent && !tankFull);
      if (newMotorState != motorState) {
        updateMotorState(newMotorState);
        lastChangeMessage = newMotorState ? "AUTO MOTOR ON" : "AUTO MOTOR OFF";
        lastChangeTime = millis();
      }
    }
  } else {
    if (manualMotorState != motorState) {
      updateMotorState(manualMotorState);
      lastChangeMessage = manualMotorState ? "MANUAL MOTOR ON" : "MANUAL MOTOR OFF";
      lastChangeTime = millis();
    }
  }

  if (waterPresent != lastStableWaterState || tankFull != lastStableTankState || motorState != lastStableMotorState) {
    stateCheckStartTime = millis();
    stateStable = false;
    if (tankFull != lastStableTankState) {
      lastChangeMessage = scheduleMode ? "TANK CHANGE" : (tankFull ? "TANK FULL" : "TANK NOT FULL");
      lastChangeTime = millis();
    }
  } else if (!stateStable && (millis() - stateCheckStartTime) >= STATE_CONFIRMATION_DELAY) {
    stateStable = true;
    printStableState(waterPresent, tankFull, motorState);
  }

  if (millis() - lastStatusUpdate >= STATUS_UPDATE_INTERVAL ||
      motorState != prevMotorState || waterPresent != prevWaterState ||
      tankFull != prevTankFullState ||
      scheduleMode != prevScheduleState ||
      isAutoMode != prevModeState) {
    printStatus(Serial, now, waterPresent, tankFull);
    lastStatusUpdate = millis();
  }

  prevMotorState = motorState;
  prevWaterState = waterPresent;
  prevTankFullState = tankFull;
  prevScheduleState = scheduleMode;
  prevModeState = isAutoMode;
  lastStableWaterState = waterPresent;
  lastStableTankState = tankFull;
  lastStableMotorState = motorState;

  updateAddons(waterPresent, tankFull);
  updateLCD();
}

// BUTTON HANDLERS
void handleAutoManualButton() {
  bool state = digitalRead(AUTO_MANUAL_BUTTON);
  if (lastAutoModeState != state) {
    if (state == LOW) { // Switch ON (Auto Mode)
      isAutoMode = true;
      scheduleMode = false;
      manualMotorState = false;
      continuousMode = false;
      isScheduledRunActive = false;
      retryInProgress = false;
      isAlternatingDisplay = false;
      currentScheduledEvent = "";
      retryWaitDisplay = false;
      updateMotorState(false);
      digitalWrite(LED_MODE_MANUAL, LOW);
      digitalWrite(LED_MODE_SCHEDULED, LOW);
      Serial.println("Auto Mode Switch ON");
      SerialBT.println("Auto Mode Switch ON");
      lastChangeMessage = "AUTO MODE ON";
      lastChangeTime = millis();
    } else { // Switch OFF (Manual Mode)
      isAutoMode = false;
      scheduleMode = false;
      manualMotorState = false;
      continuousMode = false;
      isScheduledRunActive = false;
      retryInProgress = false;
      isAlternatingDisplay = false;
      currentScheduledEvent = "";
      retryWaitDisplay = false;
      updateMotorState(false);
      digitalWrite(LED_MODE_MANUAL, HIGH);
      digitalWrite(LED_MODE_SCHEDULED, LOW);
      Serial.println("Manual Mode Switch ON");
      SerialBT.println("Manual Mode Switch ON");
      lastChangeMessage = "MANUAL MODE ON";
      lastChangeTime = millis();
    }
  }
  lastAutoModeState = state;
}

void handleScheduleOrManualButton() {
  bool state = digitalRead(SCHEDULE_BUTTON);
  if (lastScheduleBtnState != state) {
    if (isAutoMode) {
      if (state == LOW) { // Switch ON (Scheduled Mode)
        scheduleMode = true;
        if (!rtcAvailable) {
          scheduleMode = false;
          isAlternatingDisplay = false;
          currentScheduledEvent = "";
          retryWaitDisplay = false;
          Serial.println("Scheduled Mode Disabled - RTC Not Found");
          SerialBT.println("Scheduled Mode Disabled - RTC Not Found");
          lastChangeMessage = "SCH MODE DISABLED";
          lastChangeTime = millis();
          digitalWrite(LED_MODE_SCHEDULED, LOW);
        } else {
          Serial.println("Schedule Switch ON");
          SerialBT.println("Schedule Switch ON");
          lastChangeMessage = "SCHEDULED ON";
          lastChangeTime = millis();
          digitalWrite(LED_MODE_SCHEDULED, HIGH);
        }
      } else { // Switch OFF (Auto Mode, no Scheduled)
        scheduleMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        retryWaitDisplay = false;
        Serial.println("Schedule Switch OFF");
        Serial.println("Mode: AUTOMATIC MODE");
        SerialBT.println("Schedule Switch OFF");
        SerialBT.println("Mode: AUTOMATIC MODE");
        lastChangeMessage = "SCHEDULED OFF";
        lastChangeTime = millis();
        digitalWrite(LED_MODE_SCHEDULED, LOW);
      }
    } else {
      if (state == LOW) { // Switch ON (Manual Motor ON)
        manualMotorState = true;
        updateMotorState(true);
        Serial.println("Manual Motor Switch ON");
        SerialBT.println("Manual Motor Switch ON");
        lastChangeMessage = "MANUAL MOTOR ON";
        lastChangeTime = millis();
      } else { // Switch OFF (Manual Motor OFF)
        manualMotorState = false;
        updateMotorState(false);
        Serial.println("Manual Motor Switch OFF");
        SerialBT.println("Manual Motor Switch OFF");
        lastChangeMessage = "MANUAL MOTOR OFF";
        lastChangeTime = millis();
      }
    }
  }
  lastScheduleBtnState = state;
}

void handleResetButton() {
  bool state = digitalRead(RESET_BUTTON);
  if (state != lastResetBtnState) {
    if (state == LOW && !isResetButtonDebouncing) { // Button pressed
      isResetButtonDebouncing = true;
      resetBtnPressStartTime = millis();
      Serial.println("Process Initiated");
      SerialBT.println("Process Initiated");
      lastChangeMessage = "PROCESS INITIATED";
      lastChangeTime = millis();
    } else if (state == HIGH && isResetButtonDebouncing) { // Button released
      if (millis() - resetBtnPressStartTime >= BUTTON_PRESS_HOLD) {
        resetSystem();
      } else {
        Serial.println("Reset Cancelled: Hold for 3 seconds");
        SerialBT.println("Reset Cancelled: Hold for 3 seconds");
        lastChangeMessage = "RESET CANCELLED";
        lastChangeTime = millis();
      }
      isResetButtonDebouncing = false;
    }
  }
  lastResetBtnState = state;
}

// INPUT HANDLERS
void handleSerialInput() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (serialInput.length() > 0) {
        processMenuInput(serialInput, Serial);
        serialInput = "";
      }
    } else {
      serialInput += ch;
    }
  }
}

void processBluetoothInput() {
  // Only process input, connection handling is in bluetoothTask
  while (SerialBT.available()) {
    char ch = SerialBT.read();
    if (ch == '\n' || ch == '\r') {
      if (btInput.length() > 0) {
        processMenuInput(btInput, SerialBT);
        btInput = "";
      }
    } else {
      btInput += ch;
    }
  }
}

void bluetoothTask(void *pv) {
  for (;;) {
    bool isConnected = SerialBT.hasClient();
    if (isConnected && !wasBluetoothConnected) {
      Serial.println("Bluetooth Connected");
      SerialBT.println("Bluetooth Connected");
      lastChangeMessage = "BT CONNECTED";
      lastChangeTime = millis();
      printMenu(SerialBT);
      DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
      bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
      bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == LOW;
      printStableState(waterPresent, tankFull, motorState);
      btMenuActive = true;
      wasBluetoothConnected = true;
    } else if (!isConnected && wasBluetoothConnected) {
      Serial.println("Bluetooth Disconnected");
      SerialBT.println("Bluetooth Disconnected");
      lastChangeMessage = "BT DISCONNECTED";
      lastChangeTime = millis();
      wasBluetoothConnected = false;
      btMenuActive = false;
    }

    while (SerialBT.available()) {
      char ch = SerialBT.read();
      if (ch == '\n' || ch == '\r') {
        if (btInput.length() > 0) {
          processMenuInput(btInput, SerialBT);
          btInput = "";
        }
      } else {
        btInput += ch;
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS); // Reduced delay for responsiveness
  }
}

void processMenuInput(String input, Stream &output) {
  input.trim();
  if (input == "M" || input == "m") {
    printMenu(output);
    if (&output == &SerialBT) {
      DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
      bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
      bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == LOW;
      printStableState(waterPresent, tankFull, motorState);
      btMenuActive = true;
    } else {
      serialMenuActive = true;
    }
    int &state = (&output == &SerialBT) ? btMenuState : serialMenuState;
    state = 0;
    lastChangeMessage = "MENU DISPLAYED";
    lastChangeTime = millis();
    return;
  }

  int &state = (&output == &SerialBT) ? btMenuState : serialMenuState;

  if (adminMode && state == 0) {
    if (input == "21") {
      output.println("Enter Date and Time (DD/MM/YYYY,HH:MM:SS): ");
      Serial.println("Enter Date and Time (DD/MM/YYYY,HH:MM:SS): ");
      state = 22;
      lastChangeMessage = "SET RTC";
      lastChangeTime = millis();
      return;
    } else if (input == "22") {
      isAutoMode = false;
      scheduleMode = false;
      isScheduledRunActive = false;
      retryInProgress = false;
      continuousMode = false;
      isAlternatingDisplay = false;
      currentScheduledEvent = "";
      retryWaitDisplay = false;
      manualMotorState = !manualMotorState;
      updateMotorState(manualMotorState);
      digitalWrite(LED_MODE_MANUAL, HIGH);
      digitalWrite(LED_MODE_SCHEDULED, LOW);
      String msg = "Switched to Manual Mode, Motor " + String(manualMotorState ? "ON" : "OFF");
      output.println(msg);
      Serial.println(msg);
      lastChangeMessage = "MANUAL MOTOR " + String(manualMotorState ? "ON" : "OFF");
      lastChangeTime = millis();
      return;
    } else if (input == "23") {
      if (!isAutoMode) {
        isAutoMode = true;
        scheduleMode = true;
      } else {
        scheduleMode = !scheduleMode;
      }
      if (scheduleMode && !rtcAvailable) {
        scheduleMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        retryWaitDisplay = false;
        output.println("Scheduled Mode Disabled - RTC Not Found");
        Serial.println("Scheduled Mode Disabled - RTC Not Found");
        lastChangeMessage = "SCH MODE DISABLED";
        lastChangeTime = millis();
      } else {
        isScheduledRunActive = false;
        retryInProgress = false;
        continuousMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        retryWaitDisplay = false;
        updateMotorState(false);
        digitalWrite(LED_MODE_MANUAL, LOW);
        digitalWrite(LED_MODE_SCHEDULED, scheduleMode ? HIGH : LOW);
        String msg = "Scheduled Mode " + String(scheduleMode ? "ON" : "OFF");
        output.println(msg);
        Serial.println(msg);
        lastChangeMessage = "SCHEDULED " + String(scheduleMode ? "ON" : "OFF");
        lastChangeTime = millis();
      }
      return;
    } else if (input == "24") {
      resetSystem();
      return;
    }
  }

  switch (state) {
    case 0: {
      if (input == "1") {
        output.println("Enter Alarm Time (HH:MM): ");
        Serial.println("Enter Alarm Time (HH:MM): ");
        state = 1;
        lastChangeMessage = "SET ALARM TIME";
        lastChangeTime = millis();
      } else if (input == "2") {
        output.println("Options: 11 (Seconds), 12 (Minutes)");
        Serial.println("Options: 11 (Seconds), 12 (Minutes)");
        state = 2;
        lastChangeMessage = "SET DURATION";
        lastChangeTime = millis();
      } else if (input == "3") {
        output.println("Enter Number of Retries (0 to 5): ");
        Serial.println("Enter Number of Retries (0 to 5): ");
        state = 3;
        lastChangeMessage = "SET RETRIES";
        lastChangeTime = millis();
      } else if (input == "4") {
        output.println("Enter Retry Interval (Minutes): ");
        Serial.println("Enter Retry Interval (Minutes): ");
        state = 4;
        lastChangeMessage = "SET RETRY INTERVAL";
        lastChangeTime = millis();
      } else if (input == "5") {
        output.println("Enter Alarm Interval (Days): ");
        Serial.println("Enter Alarm Interval (Days): ");
        state = 5;
        lastChangeMessage = "SET INTERVAL";
        lastChangeTime = millis();
      } else if (input == "6") {
        DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
        bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
        bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == LOW;
        printStatus(output, now, waterPresent, tankFull);
        // Display status on LCD
        String timeStr = rtcAvailable ? String((now.hour() < 10 ? "0" : "") + String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute())) : "00:00";
        String status1 = String("Time: ") + timeStr + " W:" + (waterPresent ? "YES" : "NO");
        String status2 = String("Tank: ") + (tankFull ? "YES" : "NO") + " M:" + (motorState ? "ON" : "OFF");
        lastChangeMessage = status1 + "|" + status2; // Store for LCD display
        lastChangeTime = millis();
      } else if (input == "7") {
        if (adminMode) {
          printAdminMenu(output);
        } else {
          output.println("Enter Admin Password: ");
          Serial.println("Enter Admin Password: ");
          state = 7;
          lastChangeMessage = "ENTER PASSWORD";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid Option");
        Serial.println("Invalid Option");
        SerialBT.println("Invalid Option");
        lastChangeMessage = "INVALID OPTION";
        lastChangeTime = millis();
      }
      break;
    }
    case 1: {
      int hh, mm;
      if (sscanf(input.c_str(), "%d:%d", &hh, &mm) == 2 && hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
        userAlarmHour = hh;
        userAlarmMinute = mm;
        String msg = "Alarm Time set to: " + String(hh) + ":" + (mm < 10 ? "0" : "") + String(mm);
        output.println(msg);
        Serial.println(msg);
        lastChangeMessage = "ALARM SET";
        lastChangeTime = millis();
      } else {
        output.println("Invalid input. Use HH:MM");
        Serial.println("Invalid input. Use HH:MM");
        SerialBT.println("Invalid input. Use HH:MM");
        lastChangeMessage = "INVALID ALARM";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 2: {
      if (input == "11") {
        output.println("Enter Run Duration (Seconds): ");
        Serial.println("Enter Run Duration (Seconds): ");
        state = 11;
        prevInput = input;
        lastChangeMessage = "SET SECONDS";
        lastChangeTime = millis();
      } else if (input == "12") {
        output.println("Enter Run Duration (Minutes): ");
        Serial.println("Enter Run Duration (Minutes): ");
        state = 12;
        prevInput = input;
        lastChangeMessage = "SET MINUTES";
        lastChangeTime = millis();
      } else {
        output.println("Invalid option. Use 11 or 12");
        Serial.println("Invalid option. Use 11 or 12");
        SerialBT.println("Invalid option. Use 11 or 12");
        lastChangeMessage = "INVALID OPTION";
        lastChangeTime = millis();
      }
      break;
    }
    case 11: {
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 1 && value <= 60) {
          scheduledRunSeconds = value;
          isDurationInMinutes = false;
          String msg = "Scheduled: " + String(value) + " seconds";
          output.println(msg);
          Serial.println(msg);
          lastChangeMessage = "SET " + String(value) + " SEC";
          lastChangeTime = millis();
        } else {
          output.println("Invalid input. Use 1 to 60");
          Serial.println("Invalid input. Use 1 to 60");
          SerialBT.println("Invalid input. Use 1 to 60");
          lastChangeMessage = "INVALID SECONDS";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input. Use 1 to 60");
        Serial.println("Invalid input. Use 1 to 60");
        SerialBT.println("Invalid input. Use 1 to 60");
        lastChangeMessage = "INVALID SECONDS";
        lastChangeTime = millis();
      }
      state = 0;
      prevInput = "";
      break;
    }
    case 12: {
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 1 && value <= 5) {
          scheduledRunSeconds = value * 60;
          isDurationInMinutes = true;
          String msg = "Scheduled: " + String(value) + " minutes";
          output.println(msg);
          Serial.println(msg);
          lastChangeMessage = "SET " + String(value) + " MIN";
          lastChangeTime = millis();
        } else {
          output.println("Invalid input. Use 1 to 5");
          Serial.println("Invalid input. Use 1 to 5");
          SerialBT.println("Invalid input. Use 1 to 5");
          lastChangeMessage = "INVALID MINUTES";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input. Use 1 to 5");
        Serial.println("Invalid input. Use 1 to 5");
        SerialBT.println("Invalid input. Use 1 to 5");
        lastChangeMessage = "INVALID MINUTES";
        lastChangeTime = millis();
      }
      state = 0;
      prevInput = "";
      break;
    }
    case 3: {
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 0 && value <= 5) {
          retryCount = value;
          String msg = "Retries: " + String(value);
          output.println(msg);
          Serial.println(msg);
          lastChangeMessage = "SET " + String(value) + " RETRIES";
          lastChangeTime = millis();
        } else {
          output.println("Invalid input. Use 0 to 5");
          Serial.println("Invalid input. Use 0 to 5");
          SerialBT.println("Invalid input. Use 0 to 5");
          lastChangeMessage = "INVALID RETRIES";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input");
        Serial.println("Invalid input");
        SerialBT.println("Invalid input");
        lastChangeMessage = "INVALID RETRIES";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 4: {
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 1 && value <= 60) {
          retryIntervalMinutes = value;
          String msg = "Retry interval: " + String(value) + " minutes";
          output.println(msg);
          Serial.println(msg);
          lastChangeMessage = "SET " + String(value) + " MIN RETRY";
          lastChangeTime = millis();
        } else {
          output.println("Invalid input. Use 1 to 60");
          Serial.println("Invalid input. Use 1 to 60");
          SerialBT.println("Invalid input. Use 1 to 60");
          lastChangeMessage = "INVALID RETRY INT";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input");
        Serial.println("Invalid input");
        SerialBT.println("Invalid input");
        lastChangeMessage = "INVALID RETRY INT";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 5: {
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value == 1 || value == 2) {
          alarmIntervalDays = value;
          String msg = "Scheduled interval: " + String(value) + " days";
          output.println(msg);
          Serial.println(msg);
          lastChangeMessage = "SET " + String(value) + " DAYS";
          lastChangeTime = millis();
        } else {
          output.println("Invalid input. Use 1 or 2");
          Serial.println("Invalid input. Use 1 or 2");
          SerialBT.println("Invalid input. Use 1 or 2");
          lastChangeMessage = "INVALID DAYS";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input");
        Serial.println("Invalid input");
        SerialBT.println("Invalid input");
        lastChangeMessage = "INVALID DAYS";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 7: {
      if (adminMode) {
        printAdminMenu(output);
      } else if (input == ADMIN_PASSWORD) {
        adminMode = true;
        output.println("Admin access granted");
        Serial.println("Admin access granted");
        lastChangeMessage = "ADMIN GRANTED";
        lastChangeTime = millis();
        printAdminMenu(output);
      } else {
        output.println("Incorrect password");
        Serial.println("Incorrect password");
        SerialBT.println("Incorrect password");
        lastChangeMessage = "WRONG PASSWORD";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 22: {
      int day, month, year, hh, mm, ss;
      if (sscanf(input.c_str(), "%d/%d/%d,%d:%d:%d", &day, &month, &year, &hh, &mm, &ss) == 6 &&
          day >= 1 && day <= 31 && month >= 1 && month <= 12 &&
          year >= 2000 && year <= 2099 && hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59) {
        if (rtcAvailable) {
          rtc.adjust(DateTime(year, month, day, hh, mm, ss));
          output.println("RTC Updated");
          Serial.println("RTC Updated");
          lastChangeMessage = "RTC UPDATED";
          lastChangeTime = millis();
        } else {
          output.println("RTC Not Available");
          Serial.println("RTC Not Available");
          SerialBT.println("RTC Not Available");
          lastChangeMessage = "RTC NOT AVAILABLE";
          lastChangeTime = millis();
        }
      } else {
        output.println("Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        Serial.println("Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        SerialBT.println("Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        lastChangeMessage = "INVALID RTC";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
  }
}

void printMenu(Stream &output) {
  output.println("\n=== Settings Menu ===");
  output.printf("1. Set Alarm Time (HH:MM) [%02d:%02d]\n", userAlarmHour, userAlarmMinute);
  if (isDurationInMinutes) {
    output.printf("2. Set Scheduled Run Duration (Minutes) [%d min]\n", scheduledRunSeconds / 60);
  } else {
    output.printf("2. Set Scheduled Run Duration (Seconds) [%d sec]\n", scheduledRunSeconds);
  }
  output.printf("3. Set Number of Retries [%d]\n", retryCount);
  output.printf("4. Set Scheduled Retry Interval (Minutes) [%d min]\n", retryIntervalMinutes);
  output.printf("5. Set Scheduled Interval (Days) [%d days]\n", alarmIntervalDays);
  output.println("6. Show Status");
  output.println("7. Admin Menu");
  output.println("M. Show Menu");
  output.println("====");
  if (&output == &SerialBT) {
    Serial.println("\n=== Settings Menu ===");
    Serial.printf("1. Set Alarm Time (HH:MM) [%02d:%02d]\n", userAlarmHour, userAlarmMinute);
    if (isDurationInMinutes) {
      Serial.printf("2. Set Scheduled Run Duration (Minutes) [%d min]\n", scheduledRunSeconds / 60);
    } else {
      Serial.printf("2. Set Scheduled Run Duration (Seconds) [%d sec]\n", scheduledRunSeconds);
    }
    Serial.printf("3. Set Number of Retries [%d]\n", retryCount);
    Serial.printf("4. Set Scheduled Retry Interval (Minutes) [%d min]\n", retryIntervalMinutes);
    Serial.printf("5. Set Scheduled Interval (Days) [%d days]\n", alarmIntervalDays);
    Serial.println("6. Show Status");
    Serial.println("7. Admin Menu");
    Serial.println("M. Show Menu");
    Serial.println("====");
  }
}

void printAdminMenu(Stream &output) {
  output.println("\n=== Admin Menu ===");
  output.println("21. Set RTC Time and Date");
  output.println("22. Manual Mode - Motor ON/OFF");
  output.println("23. Scheduled Mode ON/OFF");
  output.println("24. Reset System");
  output.println("M. Show Menu");
  output.println("====");
  if (&output == &SerialBT) {
    Serial.println("\n=== Admin Menu ===");
    Serial.println("21. Set RTC Time and Date");
    Serial.println("22. Manual Mode - Motor ON/OFF");
    Serial.println("23. Scheduled Mode ON/OFF");
    Serial.println("24. Reset System");
    Serial.println("M. Show Menu");
    Serial.println("====");
  }
}