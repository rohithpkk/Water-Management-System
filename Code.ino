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
bool processInitiatedPrinted = false;
bool adminMode = false;
bool wasBluetoothConnected = false;
bool rtcAvailable = true;
bool isAlternatingDisplay = false; // For LCD alternation

// BUTTON DEBOUNCE FLAGS
bool isAutoButtonDebouncing = false;
bool isScheduleButtonDebouncing = false;
bool isResetButtonDebouncing = false;

// SYSTEM TIMERS
unsigned long scheduledRunStartTime = 0;
unsigned long retryStartTime = 0;
unsigned long lastSecondUpdate = 0;
unsigned long autoModePressStartTime = 0;
unsigned long scheduleBtnPressStartTime = 0;
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
const int DRY_RUN_SENSOR_PIN = 5;
const int FLOAT_SENSOR_PIN = 4;
const int MOTOR_PIN = 23;
const int SCHEDULE_BUTTON = 16;
const int AUTO_MANUAL_BUTTON = 17;
const int RESET_BUTTON = 18;
const int LED_MODE_RED = 19; // Tricolor: Manual (Red)
const int LED_MODE_GREEN = 32; // Auto (Green)
const int LED_MODE_BLUE = 33; // Scheduled (Blue)
const int LED_WATER_PIN = 25;
const int LED_TANK_FULL_PIN = 26;
const int LED_MOTOR_PIN = 27;

// BUTTON DEBOUNCE
bool lastScheduleBtnState = HIGH;
bool lastAutoModeState = HIGH;
bool lastResetBtnState = HIGH;

// PREV_STATE
bool prevMotorState = false;
bool prevWaterState = false;
bool prevTankFullState = false;
bool prevScheduleState = false;
bool prevModeState = true;

// STATE TRACKING
bool lastStableWaterState = false;
bool lastStableTankState = false;
bool lastStableMotorState = false;
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
  String modeStr = isAutoMode ? (scheduleMode ? "SCHEDULED RUN MODE" : "AUTOMATIC MODE") : "MANUAL MODE";
  String status = String("Time: ") + timeStr +
                  " | Water Flow: " + String(waterPresent ? "YES" : "NO") +
                  " | Tank Full: " + String(tankFull ? "YES" : "NO") +
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
  SerialBT.println("💧 " + state);
  Serial.println("💧 " + state); // Mirror BT
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
  manualMotorState = false; // Ensure Manual Mode motor is OFF
  isScheduledRunActive = false;
  retryInProgress = false;
  continuousMode = false;
  adminMode = false;
  btMenuActive = false;
  serialMenuActive = true;
  processInitiatedPrinted = false;
  wasBluetoothConnected = false;
  isAlternatingDisplay = false; // Reset LCD alternation
  currentScheduledEvent = ""; // Clear event

  scheduledRunStartTime = 0;
  retryStartTime = 0;
  lastSecondUpdate = 0;
  autoModePressStartTime = 0;
  scheduleBtnPressStartTime = 0;
  resetBtnPressStartTime = 0;
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

  updateMotorState(false); // Ensure motor is OFF
  digitalWrite(LED_MODE_RED, LOW);
  digitalWrite(LED_MODE_GREEN, HIGH);
  digitalWrite(LED_MODE_BLUE, LOW);
  digitalWrite(LED_WATER_PIN, LOW);
  digitalWrite(LED_TANK_FULL_PIN, LOW);
  digitalWrite(LED_MOTOR_PIN, LOW);

  lastScheduledRun = DateTime(2025, 5, 28, 6, 0, 0);
  currentRetry = 0;

  Serial.println("System Reset");
  SerialBT.println("🔄 System Reset");
  Serial.println("🔄 System Reset"); // Mirror BT
  lastChangeMessage = "System Reset";
  lastChangeTime = millis();
  printMenu(Serial);
}

// ADDON SETUP
void initializeAddons() {
  pinMode(LED_MODE_RED, OUTPUT);
  pinMode(LED_MODE_GREEN, OUTPUT);
  pinMode(LED_MODE_BLUE, OUTPUT);
  pinMode(LED_WATER_PIN, OUTPUT);
  pinMode(LED_TANK_FULL_PIN, OUTPUT);
  pinMode(LED_MOTOR_PIN, OUTPUT);
  digitalWrite(LED_MODE_RED, LOW);
  digitalWrite(LED_MODE_GREEN, HIGH); // Auto mode
  digitalWrite(LED_MODE_BLUE, LOW);
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
  digitalWrite(LED_MODE_RED, (!isAutoMode) ? HIGH : LOW); // Manual
  digitalWrite(LED_MODE_GREEN, (isAutoMode && !scheduleMode) ? HIGH : LOW); // Auto
  digitalWrite(LED_MODE_BLUE, scheduleMode ? HIGH : LOW); // Scheduled
  digitalWrite(LED_WATER_PIN, waterPresent ? HIGH : LOW);
  digitalWrite(LED_TANK_FULL_PIN, tankFull ? HIGH : LOW);
  digitalWrite(LED_MOTOR_PIN, motorState ? HIGH : LOW);
}

void centerPrint(String text, uint8_t row) {
  lcd.setCursor(0, row);
  lcd.print("                "); // Clear line
  int padding = (16 - text.length()) / 2;
  if (padding < 0) padding = 0; // Handle long text
  String paddedText = String("                ").substring(0, padding) + text.substring(0, 16 - padding);
  lcd.setCursor(0, row);
  lcd.print(paddedText);
}

void updateLCD() {
  lcd.clear();
  DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  String timeStr = rtcAvailable ? 
    String((now.hour() < 10 ? "0" : "") + String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute())) : 
    "00:00";

  if (millis() - startupDisplayTime < STARTUP_DISPLAY_DURATION) {
    centerPrint("ENARXI", 0);
    centerPrint("WATER REFILL SYS", 1);
    return;
  }

  if (!rtcAvailable && millis() - lastChangeTime < LCD_CHANGE_DURATION && lastChangeMessage == "RTC Not Found") {
    centerPrint(timeStr, 0);
    centerPrint("RTC Not Found", 1);
    return;
  }

  if (millis() - lastChangeTime < LCD_CHANGE_DURATION && lastChangeMessage != "") {
    centerPrint(scheduleMode ? "A " + String(userAlarmHour < 10 ? "0" : "") + String(userAlarmHour) + ":" +
                String(userAlarmMinute < 10 ? "0" : "") + String(userAlarmMinute) : timeStr, 0);
    centerPrint(lastChangeMessage.substring(0, 16), 1);
    return;
  }

  String alarmStr = String(userAlarmHour < 10 ? "0" : "") + String(userAlarmHour) + ":" +
                    String(userAlarmMinute < 10 ? "0" : "") + String(userAlarmMinute);
  String statusStr = (isDurationInMinutes ? String(scheduledRunSeconds / 60) + "M" : String(scheduledRunSeconds) + "S") + "-" +
                     String(retryCount) + "-" +
                     String(retryIntervalMinutes) + "M-" +
                     String(alarmIntervalDays) + "D";

  if (scheduleMode) {
    centerPrint("A " + alarmStr, 0);
    if (isAlternatingDisplay && millis() - lastScheduledEventTime >= LCD_ALTERNATE_DURATION) {
      currentScheduledEvent = (currentScheduledEvent == "") ? lastChangeMessage : "";
      lastScheduledEventTime = millis();
    }
    if (isAlternatingDisplay && currentScheduledEvent != "") {
      centerPrint(currentScheduledEvent.substring(0, 16), 1);
    } else {
      centerPrint(statusStr.substring(0, 16), 1);
    }
  } else {
    centerPrint(timeStr, 0);
    String modeStr = isAutoMode ? "AUTO" : "MANUAL";
    centerPrint(modeStr + " MOTOR " + String(motorState ? "ON" : "OFF"), 1);
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
    SerialBT.println("❌🕒 RTC Not Found");
    Serial.println("❌🕒 RTC Not Found");
    rtcAvailable = false;
    lastChangeMessage = "RTC Not Found";
    lastChangeTime = millis();
  } else {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  initializeAddons();

  Serial.println("Starting System...");
  SerialBT.println("✅ Starting System...");
  Serial.println("✅ Starting System...");
  lastChangeMessage = "System Started";
  lastChangeTime = millis();
  printMenu(Serial);
  serialMenuActive = true;

  xTaskCreatePinnedToCore(bluetoothTask, "BT Task", 4096, NULL, 1, NULL, 0);
}

// CORE LOGIC
void loop() {
  handleSerialInput();
  handleBluetoothMenu();
  handleAutoManualButton();
  handleScheduleOrManualButton();
  handleResetButton();

  DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
  bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
  bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == HIGH;

  static bool lastScheduledRunActive = false;
  static bool lastRetryInProgress = false;
  static bool lastContinuousMode = false;

  if (isAutoMode) {
    if (scheduleMode) {
      if (!rtcAvailable) {
        scheduleMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        Serial.println("Scheduled Mode Disabled - RTC Not Found");
        SerialBT.println("❌ Scheduled Mode Disabled - RTC Not Found");
        Serial.println("❌ Scheduled Mode Disabled - RTC Not Found");
        lastChangeMessage = "Sch Mode Disabled";
        lastChangeTime = millis();
      } else {
        bool isAlarmTime = (now.hour() == userAlarmHour && now.minute() == userAlarmMinute && now.second() <= 15);
        bool isIntervalElapsed = (now.unixtime() >= lastScheduledRun.unixtime() + (alarmIntervalDays * 86400UL));

        if (!isScheduledRunActive && !retryInProgress && !continuousMode && isAlarmTime && isIntervalElapsed) {
          isScheduledRunActive = true;
          scheduledRunStartTime = millis();
          currentRetry = 0;
          updateMotorState(true); // Start motor regardless of sensors
          Serial.println("Scheduled Run STARTED");
          SerialBT.println("⏰ Scheduled Run STARTED");
          Serial.println("⏰ Scheduled Run STARTED");
          lastChangeMessage = "Sch Run STARTED";
          currentScheduledEvent = "Sch Run STARTED";
          isAlternatingDisplay = true;
          lastScheduledEventTime = millis();
          lastChangeTime = millis();
          lastScheduledRunActive = true;
        }

        if (isScheduledRunActive) {
          updateMotorState(true); // Keep motor ON during run time
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
            if (waterPresent && !tankFull) {
              continuousMode = true;
              updateMotorState(true);
              Serial.println("Run Complete");
              SerialBT.println("✅ Run Complete");
              Serial.println("✅ Run Complete");
              lastChangeMessage = "Run Complete";
              lastChangeTime = millis();
            } else {
              continuousMode = false;
              updateMotorState(false);
              retryInProgress = retryCount > 0;
              retryStartTime = millis();
              currentRetry = 1;
              Serial.println("Run Complete");
              SerialBT.println("🛑 Run Complete");
              Serial.println("🛑 Run Complete");
              lastChangeMessage = "Run Complete";
              lastChangeTime = millis();
              if (retryInProgress && !lastRetryInProgress) {
                Serial.println("Starting Retries");
                SerialBT.println("🔄 Starting Retries");
                Serial.println("🔄 Starting Retries");
                lastChangeMessage = "Starting Retries";
                lastChangeTime = millis();
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
          Serial.println("Motor STOPPED");
          SerialBT.println("🛑 Motor STOPPED");
          Serial.println("🛑 Motor STOPPED");
          lastChangeMessage = "Motor STOPPED";
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

          if (millis() - lastRetryWaterCheck >= RETRY_WATER_CHECK_INTERVAL && waterPresent) {
            Serial.println("Water Detected");
            SerialBT.println("💧 Water Detected");
            Serial.println("💧 Water Detected");
            lastChangeMessage = "Water Detected";
            currentScheduledEvent = "Water Detected";
            isAlternatingDisplay = true;
            lastScheduledEventTime = millis();
            lastChangeTime = millis();
            lastRetryWaterCheck = millis();
          }

          if ((millis() - retryStartTime) >= retryIntervalMinutes * 60000UL) {
            Serial.println("Retry " + String(currentRetry) + " Started");
            SerialBT.println("🔄 Retry " + String(currentRetry) + " Started");
            Serial.println("🔄 Retry " + String(currentRetry) + " Started");
            lastChangeMessage = "Retry " + String(currentRetry);
            currentScheduledEvent = "Retry " + String(currentRetry);
            isAlternatingDisplay = true;
            lastScheduledEventTime = millis();
            lastChangeTime = millis();
            if (waterPresent && !tankFull) {
              updateMotorState(true);
              retryInProgress = false;
              continuousMode = true;
              currentScheduledEvent = "";
              isAlternatingDisplay = false;
              Serial.println("Retry " + String(currentRetry) + " Finished");
              Serial.println("Motor ON");
              SerialBT.println("✅ Retry " + String(currentRetry) + " Finished");
              SerialBT.println("✅ Motor ON");
              Serial.println("✅ Retry " + String(currentRetry) + " Finished");
              Serial.println("✅ Motor ON");
              lastChangeMessage = "Retry SUCCESS";
              lastChangeTime = millis();
            } else {
              Serial.println("Retry " + String(currentRetry) + " Finished");
              Serial.println("Motor OFF");
              SerialBT.println("❌ Retry " + String(currentRetry) + " Finished");
              SerialBT.println("❌ Motor OFF");
              Serial.println("❌ Retry " + String(currentRetry) + " Finished");
              Serial.println("❌ Motor OFF");
              lastChangeMessage = "Retry FAILED";
              currentScheduledEvent = "";
              isAlternatingDisplay = false;
              lastChangeTime = millis();
              currentRetry++;
              retryStartTime = millis();
              lastRetryWaterCheck = millis();
              Serial.println("Retry Wait Started");
              SerialBT.println("⏳ Retry Wait Started");
              Serial.println("⏳ Retry Wait Started");
              lastChangeMessage = "Retry Wait Started";
              lastChangeTime = millis();
              if (currentRetry > retryCount) {
                retryInProgress = false;
                updateMotorState(false);
                currentScheduledEvent = "";
                isAlternatingDisplay = false;
                Serial.println("Retries FAILED");
                SerialBT.println("⛔ Retries FAILED");
                Serial.println("⛔ Retries FAILED");
                lastChangeMessage = "Retries FAILED";
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
      }
    }
  } else {
    if (manualMotorState != motorState) {
      updateMotorState(manualMotorState);
    }
  }

  if (waterPresent != lastStableWaterState || tankFull != lastStableTankState || motorState != lastStableMotorState) {
    stateCheckStartTime = millis();
    stateStable = false;
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
  if (lastAutoModeState == HIGH && state == LOW && !isAutoButtonDebouncing) {
    isAutoButtonDebouncing = true;
    autoModePressStartTime = millis();
    Serial.println("Process Initiated");
    SerialBT.println("🟢 Process Initiated");
    Serial.println("🟢 Process Initiated");
    lastChangeMessage = "Process Initiated";
    lastChangeTime = millis();
  }
  if (lastAutoModeState == LOW && state == HIGH && isAutoButtonDebouncing) {
    if (millis() - autoModePressStartTime >= BUTTON_PRESS_HOLD) {
      isAutoMode = !isAutoMode;
      scheduleMode = false;
      manualMotorState = false;
      continuousMode = false;
      isScheduledRunActive = false;
      retryInProgress = false;
      isAlternatingDisplay = false;
      currentScheduledEvent = "";
      updateMotorState(false);
      String modeMsg = isAutoMode ? "Mode: AUTOMATIC MODE" : "Mode: MANUAL MODE";
      Serial.println(modeMsg);
      SerialBT.println("🔄 " + modeMsg);
      Serial.println("🔄 " + modeMsg);
      lastChangeMessage = isAutoMode ? "Auto Mode ON" : "Manual Mode ON";
      lastChangeTime = millis();
    }
    isAutoButtonDebouncing = false;
    processInitiatedPrinted = false;
  }
  lastAutoModeState = state;
}

void handleScheduleOrManualButton() {
  bool state = digitalRead(SCHEDULE_BUTTON);
  if (lastScheduleBtnState == HIGH && state == LOW && !isScheduleButtonDebouncing) {
    isScheduleButtonDebouncing = true;
    scheduleBtnPressStartTime = millis();
    Serial.println("Process Initiated");
    SerialBT.println("🟡 Process Started");
    Serial.println("🟡 Process Started");
    lastChangeMessage = "Process Started";
    lastChangeTime = millis();
  }
  if (lastScheduleBtnState == LOW && state == HIGH && isScheduleButtonDebouncing) {
    if (millis() - scheduleBtnPressStartTime >= BUTTON_PRESS_HOLD) {
      if (isAutoMode) {
        scheduleMode = !scheduleMode;
        if (scheduleMode && !rtcAvailable) {
          scheduleMode = false;
          isAlternatingDisplay = false;
          currentScheduledEvent = "";
          Serial.println("Scheduled Mode Disabled - RTC Not Found");
          SerialBT.println("❌ Scheduled Mode Disabled - RTC Not Found");
          Serial.println("❌ Scheduled Mode Disabled - RTC Not Found");
          lastChangeMessage = "Sch Mode Disabled";
          lastChangeTime = millis();
        } else if (scheduleMode) {
          Serial.println("SCHEDULED RUN MODE ON");
          SerialBT.println("🗓️ SCHEDULED RUN MODE ON");
          Serial.println("🗓️ SCHEDULED RUN MODE ON");
          lastChangeMessage = "Scheduled ON";
          lastChangeTime = millis();
        } else {
          isAlternatingDisplay = false;
          currentScheduledEvent = "";
          Serial.println("SCHEDULED RUN MODE OFF");
          Serial.println("Mode: AUTOMATIC MODE");
          SerialBT.println("🏁 SCHEDULED RUN MODE OFF");
          SerialBT.println("🔄 Mode: AUTOMATIC MODE");
          Serial.println("🏁 SCHEDULED RUN MODE OFF");
          Serial.println("🔄 Mode: AUTOMATIC MODE");
          lastChangeMessage = "Scheduled OFF";
          lastChangeTime = millis();
        }
      } else {
        manualMotorState = !manualMotorState;
        updateMotorState(manualMotorState);
        String motorMsg = manualMotorState ? "Manual Motor ON" : "Manual Motor OFF";
        Serial.println(motorMsg);
        SerialBT.println("⚙️ " + motorMsg);
        Serial.println("⚙️ " + motorMsg);
        lastChangeMessage = motorMsg;
        lastChangeTime = millis();
      }
    }
    isScheduleButtonDebouncing = false;
    processInitiatedPrinted = false;
  }
  lastScheduleBtnState = state;
}

void handleResetButton() {
  bool state = digitalRead(RESET_BUTTON);
  if (lastResetBtnState == HIGH && state == LOW && !isResetButtonDebouncing) {
    isResetButtonDebouncing = true;
    resetBtnPressStartTime = millis();
    Serial.println("Process Initiated");
    SerialBT.println("🟣 Process Initiated");
    Serial.println("🟣 Process Initiated");
    lastChangeMessage = "Process Initiated";
    lastChangeTime = millis();
  }
  if (lastResetBtnState == LOW && state == HIGH && isResetButtonDebouncing) {
    if (millis() - resetBtnPressStartTime >= BUTTON_PRESS_HOLD) {
      resetSystem();
    }
    isResetButtonDebouncing = false;
    processInitiatedPrinted = false;
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

void handleBluetoothMenu() {
  bool isConnected = SerialBT.hasClient();
  if (isConnected && !wasBluetoothConnected) {
    Serial.println("Bluetooth Connected");
    SerialBT.println("📶 Bluetooth Connected");
    Serial.println("📶 Bluetooth Connected");
    lastChangeMessage = "BT Connected";
    lastChangeTime = millis();
    printMenu(SerialBT);
    DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
    bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
    bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == HIGH;
    printStableState(waterPresent, tankFull, motorState);
    btMenuActive = true;
    wasBluetoothConnected = true;
  } else if (!isConnected && wasBluetoothConnected) {
    Serial.println("Bluetooth Disconnected");
    SerialBT.println("🔌 Bluetooth Disconnected");
    Serial.println("🔌 Bluetooth Disconnected");
    lastChangeMessage = "BT Disconnected";
    lastChangeTime = millis();
    wasBluetoothConnected = false;
    btMenuActive = false;
  }
}

void bluetoothTask(void *pv) {
  for (;;) {
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
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void processMenuInput(String input, Stream &output) {
  input.trim();
  if (input == "M" || input == "m") {
    printMenu(output);
    if (&output == &SerialBT) {
      DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
      bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
      bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == HIGH;
      printStableState(waterPresent, tankFull, motorState);
      btMenuActive = true;
    } else {
      serialMenuActive = true;
    }
    int &state = (&output == &SerialBT) ? btMenuState : serialMenuState;
    state = 0;
    lastChangeMessage = "Menu Displayed";
    lastChangeTime = millis();
    return;
  }

  int &state = (&output == &SerialBT) ? btMenuState : serialMenuState;

  if (adminMode && state == 0) {
    if (input == "21") {
      output.println("Enter Date and Time (DD/MM/YYYY,HH:MM:SS): ");
      Serial.println("Enter Date and Time (DD/MM/YYYY,HH:MM:SS): ");
      state = 22;
      lastChangeMessage = "Set RTC";
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
      manualMotorState = !manualMotorState;
      updateMotorState(manualMotorState);
      digitalWrite(LED_MODE_RED, HIGH);
      digitalWrite(LED_MODE_GREEN, LOW);
      digitalWrite(LED_MODE_BLUE, LOW);
      String msg = "Switched to Manual Mode, Motor " + String(manualMotorState ? "ON" : "OFF");
      output.println("✅ " + msg);
      Serial.println("⚙️ " + msg);
      lastChangeMessage = "Manual Motor " + String(manualMotorState ? "ON" : "OFF");
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
        output.println("❌ Scheduled Mode Disabled - RTC Not Found");
        Serial.println("❌ Scheduled Mode Disabled - RTC Not Found");
        lastChangeMessage = "Sch Mode Disabled";
        lastChangeTime = millis();
      } else {
        isScheduledRunActive = false;
        retryInProgress = false;
        continuousMode = false;
        isAlternatingDisplay = false;
        currentScheduledEvent = "";
        updateMotorState(false);
        digitalWrite(LED_MODE_RED, LOW);
        digitalWrite(LED_MODE_GREEN, scheduleMode ? LOW : HIGH);
        digitalWrite(LED_MODE_BLUE, scheduleMode ? HIGH : LOW);
        String msg = "Scheduled Mode " + String(scheduleMode ? "ON" : "OFF");
        output.println("✅ " + msg);
        Serial.println("🗂 " + msg);
        lastChangeMessage = msg;
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
        lastChangeMessage = "Set Alarm Time";
        lastChangeTime = millis();
      } else if (input == "2") {
        output.println("Options: 11 (Seconds), 12 (Minutes)");
        Serial.println("Options: 11 (Seconds), 12 (Minutes)");
        state = 2;
        lastChangeMessage = "Set Duration";
        lastChangeTime = millis();
      } else if (input == "3") {
        output.println("Enter Number of Retries (0 to 5): ");
        Serial.println("Enter Number of Retries (0 to 5): ");
        state = 3;
        lastChangeMessage = "Set Retries";
        lastChangeTime = millis();
      } else if (input == "4") {
        output.println("Enter Retry Interval (Minutes): ");
        Serial.println("Enter Retry Interval (Minutes): ");
        state = 4;
        lastChangeMessage = "Set Retry Interval";
        lastChangeTime = millis();
      } else if (input == "5") {
        output.println("Enter Alarm Interval (Days): ");
        Serial.println("Enter Alarm Interval (Days): ");
        state = 5;
        lastChangeMessage = "Set Interval";
        lastChangeTime = millis();
      } else if (input == "6") {
        DateTime now = rtcAvailable ? rtc.now() : DateTime(2000, 1, 1, 0, 0, 0);
        bool waterPresent = digitalRead(DRY_RUN_SENSOR_PIN) == LOW;
        bool tankFull = digitalRead(FLOAT_SENSOR_PIN) == HIGH;
        printStatus(output, now, waterPresent, tankFull);
        lastChangeMessage = "Status Displayed";
        lastChangeTime = millis();
      } else if (input == "7") {
        if (adminMode) {
          printAdminMenu(output);
        } else {
          output.println("Enter Admin Password: ");
          Serial.println("Enter Admin Password: ");
          state = 7;
          lastChangeMessage = "Enter Password";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid Option");
        Serial.println("⚖️ Invalid Option");
        SerialBT.println("⚖️ Invalid Option");
        lastChangeMessage = "Invalid Option";
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
        output.println("✅ " + msg);
        Serial.println("✅ " + msg);
        lastChangeMessage = "Alarm Set";
        lastChangeTime = millis();
      } else {
        output.println("⚖️ Invalid input. Use HH:MM");
        Serial.println("⚖️ Invalid input. Use HH:MM");
        SerialBT.println("⚖️ Invalid input. Use HH:MM");
        lastChangeMessage = "Invalid Alarm";
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
        lastChangeMessage = "Set Seconds";
        lastChangeTime = millis();
      } else if (input == "12") {
        output.println("Enter Run Duration (Minutes): ");
        Serial.println("Enter Run Duration (Minutes): ");
        state = 12;
        prevInput = input;
        lastChangeMessage = "Set Minutes";
        lastChangeTime = millis();
      } else {
        output.println("⚖️ Invalid option. Use 11 or 12");
        Serial.println("⚖️ Invalid option. Use 11 or 12");
        SerialBT.println("⚖️ Invalid option. Use 11 or 12");
        lastChangeMessage = "Invalid Option";
        lastChangeTime = millis();
      }
      break;
    }
    case 11: { // Seconds
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 1 && value <= 60) {
          scheduledRunSeconds = value;
          isDurationInMinutes = false;
          String msg = "Scheduled: " + String(value) + " seconds";
          output.println("✅ " + msg);
          Serial.println("✅ " + msg);
          lastChangeMessage = "Set " + String(value) + " sec";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ Invalid input. Use 1 to 60");
          Serial.println("⚖️ Invalid input. Use 1 to 60");
          SerialBT.println("⚖️ Invalid input. Use 1 to 60");
          lastChangeMessage = "Invalid Seconds";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input. Use 1 to 60");
        Serial.println("⚖️ Invalid input. Use 1 to 60");
        SerialBT.println("⚖️ Invalid input. Use 1 to 60");
        lastChangeMessage = "Invalid Seconds";
        lastChangeTime = millis();
      }
      state = 0;
      prevInput = "";
      break;
    }
    case 12: { // Minutes
      if (isValidInteger(input)) {
        int value = input.toInt();
        if (value >= 1 && value <= 5) {
          scheduledRunSeconds = value * 60;
          isDurationInMinutes = true;
          String msg = "Scheduled: " + String(value) + " minutes";
          output.println("✅ " + msg);
          Serial.println("✅ " + msg);
          lastChangeMessage = "Set " + String(value) + " min";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ Invalid input. Use 1 to 5");
          Serial.println("⚖️ Invalid input. Use 1 to 5");
          SerialBT.println("⚖️ Invalid input. Use 1 to 5");
          lastChangeMessage = "Invalid Minutes";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input. Use 1 to 5");
        Serial.println("⚖️ Invalid input. Use 1 to 5");
        SerialBT.println("⚖️ Invalid input. Use 1 to 5");
        lastChangeMessage = "Invalid Minutes";
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
          output.println("✅ " + msg);
          Serial.println("✅ " + msg);
          lastChangeMessage = "Set " + String(value) + " retries";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ Invalid input. Use 0 to 5");
          Serial.println("⚖️ Invalid input. Use 0 to 5");
          SerialBT.println("⚖️ Invalid input. Use 0 to 5");
          lastChangeMessage = "Invalid Retries";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input");
        Serial.println("⚖️ Invalid input");
        SerialBT.println("⚖️ Invalid input");
        lastChangeMessage = "Invalid Retries";
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
          output.println("✅ " + msg);
          Serial.println("✅ " + msg);
          lastChangeMessage = "Set " + String(value) + " min retry";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ Invalid input. Use 1 to 60");
          Serial.println("⚖️ Invalid input. Use 1 to 60");
          SerialBT.println("⚖️ Invalid input. Use 1 to 60");
          lastChangeMessage = "Invalid Retry Int";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input");
        Serial.println("⚖️ Invalid input");
        SerialBT.println("⚖️ Invalid input");
        lastChangeMessage = "Invalid Retry Int";
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
          output.println("✅ " + msg);
          Serial.println("✅ " + msg);
          lastChangeMessage = "Set " + String(value) + " days";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ Invalid input. Use 1 or 2");
          Serial.println("⚖️ Invalid input. Use 1 or 2");
          SerialBT.println("⚖️ Invalid input. Use 1 or 2");
          lastChangeMessage = "Invalid Days";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input");
        Serial.println("⚖️ Invalid input");
        SerialBT.println("⚖️ Invalid input");
        lastChangeMessage = "Invalid Days";
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
        output.println("✅ Admin access granted");
        Serial.println("✅ Admin access granted");
        lastChangeMessage = "Admin Granted";
        lastChangeTime = millis();
        printAdminMenu(output);
      } else {
        output.println("❌ Incorrect password");
        Serial.println("❌ Incorrect password");
        SerialBT.println("⚖️ Incorrect password");
        lastChangeMessage = "Wrong Password";
        lastChangeTime = millis();
      }
      state = 0;
      break;
    }
    case 22: { // Admin: Set RTC
      int day, month, year, hh, mm, ss;
      if (sscanf(input.c_str(), "%d/%d/%d,%d:%d:%d", &day, &month, &year, &hh, &mm, &ss) == 6 &&
          day >= 1 && day <= 31 && month >= 1 && month <= 12 &&
          year >= 2000 && year <= 2099 && hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59 && ss >= 0 && ss <= 59) {
        if (rtcAvailable) {
          rtc.adjust(DateTime(year, month, day, hh, mm, ss));
          output.println("✅ RTC Updated");
          Serial.println("✅ RTC Updated");
          lastChangeMessage = "RTC Updated";
          lastChangeTime = millis();
        } else {
          output.println("⚖️ RTC Not Available");
          Serial.println("⚖️ RTC Not Available");
          SerialBT.println("⚖️ Invalid input. Use DD/MM/YYYY,HH:MM:SS");
          lastChangeMessage = "RTC Not Available";
          lastChangeTime = millis();
        }
      } else {
        output.println("⚖️ Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        Serial.println("⚖️ Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        SerialBT.println("⚖️ Invalid input. Use DD/MM/YYYY,HH:MM:SS");
        lastChangeMessage = "Invalid RTC";
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