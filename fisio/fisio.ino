#define BLYNK_TEMPLATE_ID "TMPL63aYrJmk0"
#define BLYNK_TEMPLATE_NAME "Terapi"

#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>
#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <WiFiManager.h>
#include <RBDdimmer.h>

// Blynk credentials
char auth[] = "ovrmaf7895-iPkp2mYD7GD73jLkgYs-L";

// LCD I2C (address 0x27, 16x2)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Button pins
#define BTN1 18
#define BTN2 17
#define BTN3 19

// Stepper motor pins
#define PUL_PIN 13   // STEP/PUL
#define DIR_PIN 12   // DIR
#define ENA_PIN 14   // ENA
#define RELAY_PIN 26
#define BUZZER_PIN 4

// Therapy relay pins
#define DIMMER_OUTPUT_PIN 16
#define DIMMER_ZEROCROSS_PIN 33
#define RELAY_COLD 27

// Stepper motor setup
AccelStepper stepper(AccelStepper::DRIVER, PUL_PIN, DIR_PIN);

// Dimmer setup
dimmerLamp heatDimmer(DIMMER_OUTPUT_PIN, DIMMER_ZEROCROSS_PIN);
const long STEPS_PER_REV = 6400;
const float MAX_SPEED = 1200.0;
const float THERAPY_SPEED = 2400.0;
const float STARTUP_SPEED = 300.0;
const float ACCEL = 1200.0;
const float STARTUP_ACCEL = 200.0;

// Variables
int state = 0;
int mode = 0; // 0=hangat, 1=dingin
int heatLevel = 0; // 0=HIGH, 1=MEDIUM, 2=LOW (only for hangat mode)
int angle = 0; // 0=40°, 1=50°, 2=60°
int duration = 0; // 0=5min, 1=10min, 2=15min
int selection = 0;
unsigned long startTime = 0;
unsigned long therapyTime = 0;
bool therapyActive = false;
bool remoteTherapyActive = false;

// Blynk virtual pin values
int blynkMode = 0;
int blynkHeatLevel = 0;
int blynkAngle = 0;
int blynkDuration = 0;

// Motor variables
int startPosition = 10;
int targetAngle = 40;
bool motorDirection = true;
unsigned long lastMotorMove = 0;
const unsigned long motorDelay = 2000;

// Countdown variables
int countdownValue = 0;
unsigned long countdownStart = 0;
bool countdownActive = false;

// LCD update variables
unsigned long lastLCDUpdate = 0;
const unsigned long lcdUpdateInterval = 1000;

// Blynk update variables
unsigned long lastBlynkUpdate = 0;
const unsigned long blynkUpdateInterval = 15000; // 15 seconds

// Screen variables
unsigned long finishScreenStart = 0;
bool finishScreenActive = false;
unsigned long cancelScreenStart = 0;
bool cancelScreenActive = false;

// Button variables
unsigned long lastButtonPress = 0;
unsigned long btn1HoldStart = 0;
bool btn1Holding = false;
const unsigned long debounceDelay = 300;
const unsigned long longPressDelay = 2000;

// Stepper functions
inline long degToSteps(float deg) {
  return lroundf((deg / 360.0f) * STEPS_PER_REV);
}

void gotoAngle(float deg) {
  float actualAngle = 90.0 - deg;
  stepper.moveTo(degToSteps(actualAngle));
}

// Non-blocking buzzer functions
void buzzerBeep() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(50000);
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerStart() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(100000);
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerFinish() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(200000);
  digitalWrite(BUZZER_PIN, LOW);
}

void buzzerCancel() {
  digitalWrite(BUZZER_PIN, HIGH);
  delayMicroseconds(100000);
  digitalWrite(BUZZER_PIN, LOW);
}

// Therapy control functions
void turnOffAllTherapy() {
  heatDimmer.setPower(0);        // Turn off dimmer
  digitalWrite(RELAY_COLD, LOW); // HIGH trigger - LOW = OFF
}

void activateTherapy() {
  turnOffAllTherapy(); // Safety: turn off all first
  
  if (mode == 0) { // Hangat mode
    int dimmerPower;
    if (heatLevel == 0) dimmerPower = 90;      // HIGH = 90%
    else if (heatLevel == 1) dimmerPower = 60; // MEDIUM = 60%
    else dimmerPower = 30;                     // LOW = 30%
    heatDimmer.setPower(dimmerPower);
  } else { // Dingin mode
    digitalWrite(RELAY_COLD, HIGH);            // HIGH trigger - HIGH = ON
  }
}

// Blynk connection callback
BLYNK_CONNECTED() {
  // Sync all V1-V5 values to Blynk
  Blynk.virtualWrite(V1, mode);
  Blynk.virtualWrite(V2, heatLevel);
  Blynk.virtualWrite(V3, angle);
  Blynk.virtualWrite(V4, duration);
  Blynk.virtualWrite(V5, 0);
  Blynk.virtualWrite(V6, "DEVICE READY");
}

// Blynk Virtual Pin handlers
BLYNK_WRITE(V1) {
  blynkMode = param.asInt();
}

BLYNK_WRITE(V2) {
  blynkHeatLevel = param.asInt();
}

BLYNK_WRITE(V3) {
  blynkAngle = param.asInt();
}

BLYNK_WRITE(V4) {
  blynkDuration = param.asInt();
}

BLYNK_WRITE(V5) {
  int startTherapy = param.asInt();
  if (startTherapy == 1 && !therapyActive && state == 0) {
    // Use stored Blynk values
    mode = blynkMode;
    heatLevel = blynkHeatLevel;
    angle = blynkAngle;
    duration = blynkDuration;
    if (angle == 0) targetAngle = 40;
    else if (angle == 1) targetAngle = 50;
    else targetAngle = 60;
    
    // Debug print for remote therapy
    Serial.print("Remote Therapy - angle: ");
    Serial.print(angle);
    Serial.print(", targetAngle: ");
    Serial.println(targetAngle);
    
    // Start remote therapy with countdown
    remoteTherapyActive = true;
    state = 5;
    startCountdown();
  } else if (startTherapy == 0 && therapyActive) {
    // Stop therapy
    stopTherapy();
  }
}



void stopTherapy() {
  therapyActive = false;
  remoteTherapyActive = false;
  
  stepper.setMaxSpeed(MAX_SPEED);
  gotoAngle(startPosition);
  
  digitalWrite(RELAY_PIN, LOW);
  turnOffAllTherapy();
  
  buzzerCancel();
  Blynk.virtualWrite(V5, 0);
  Blynk.virtualWrite(V6, "DEVICE READY");
  Blynk.logEvent("therapy_stopped", "Terapi dihentikan!");
  
  state = 0;
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("FISIOTERAPI");
  lcd.setCursor(1, 1);
  lcd.print("Tekan tombol...");
}

void setup() {
  Serial.begin(9600);
  // Therapy setup
  heatDimmer.begin(NORMAL_MODE, ON);
  pinMode(RELAY_COLD, OUTPUT);
  turnOffAllTherapy();
  lcd.init();
  lcd.backlight();
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  
  // Stepper and relay setup
  stepper.setEnablePin(ENA_PIN);
  stepper.setPinsInverted(false, false, true);
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);
  
  
  
  // WiFi Manager setup
  WiFiManager wm;
  
  // Show WiFi status on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Setup...");
  
  // Set AP name for configuration
  bool res = wm.autoConnect("FISIOTERAPI-SETUP");
  
  if (!res) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    lcd.setCursor(0, 1);
    lcd.print("Restarting...");
    delay(3000);
    ESP.restart();
  }
  

  // Untuk NIM
  //delay(1000);
  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print("ATSAR");
  // lcd.setCursor(0, 1);
  // lcd.print("NIM : 90001");
  // delay(5000);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print("Starting Blynk");
  delay(1000);
  lcd.clear();
  
  Blynk.config(auth);
  Blynk.connect();
  
  // Move motor to start position on startup with slow smooth movement (CCW)
  digitalWrite(RELAY_PIN, HIGH);
  stepper.enableOutputs();
  stepper.setMaxSpeed(STARTUP_SPEED);
  stepper.setAcceleration(STARTUP_ACCEL);
  // Set initial position at 0 degrees (actual physical position) for CCW movement to start position
  stepper.setCurrentPosition(degToSteps(90.0 - 0));
  gotoAngle(startPosition);
  
  // Wait for motor to reach start position before showing welcome
  while(stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  // Reset to normal speed after reaching start position
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);
  
  // Welcome screen
  delay(1000);
  digitalWrite(RELAY_PIN, LOW);
  lcd.setCursor(2, 0);
  lcd.print("FISIOTERAPI");
  lcd.setCursor(1, 1);
  lcd.print("Tekan tombol...");
}

void loop() {
  stepper.run(); // PRIORITY: Always run motor first
  Blynk.run();
  
  bool btn1 = !digitalRead(BTN1);
  bool btn2 = !digitalRead(BTN2);
  bool btn3 = !digitalRead(BTN3);
  
  // Handle BTN3 for stop/cancel
  if (btn3 && ((state >= 1 && state <= 4) || (state == 6 && therapyActive))) {
    if (state == 6 && therapyActive) {
      if (remoteTherapyActive) {
        // Stop remote therapy via button
        therapyActive = false;
        remoteTherapyActive = false;
        
        stepper.setMaxSpeed(MAX_SPEED);
        gotoAngle(startPosition);
        
        // Wait for motor to reach start position
        while(stepper.distanceToGo() != 0) {
          stepper.run();
        }
        
        digitalWrite(RELAY_PIN, LOW);
        turnOffAllTherapy();
        stepper.disableOutputs();
        
        // Update Blynk and send notification
        Blynk.virtualWrite(V5, 0); // Reset start button
        Blynk.virtualWrite(V6, "DEVICE READY"); // Reset to ready
        Blynk.logEvent("therapy_stopped", "Terapi dihentikan melalui tombol fisik!");
      } else {
        therapyActive = false;
        stepper.setMaxSpeed(MAX_SPEED);
        gotoAngle(startPosition);
        
        // Wait for motor to reach start position
        while(stepper.distanceToGo() != 0) {
          stepper.run();
        }
        
        digitalWrite(RELAY_PIN, LOW);
        turnOffAllTherapy();
        
        // Update Blynk for manual therapy stop
        Blynk.virtualWrite(V5, 0);
        Blynk.virtualWrite(V6, "DEVICE READY");
        Blynk.logEvent("therapy_stopped", "Terapi dihentikan melalui tombol fisik!");
      }
    }
    buzzerCancel();
    state = 7;
    selection = 0;
    cancelScreenActive = true;
    cancelScreenStart = millis();
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("DIBATAL");
    lcd.setCursor(1, 1);
    lcd.print("Kembali menu");
    return;
  }
  
  // Normal button handling (only if not remote therapy)
  if (!remoteTherapyActive && (btn1 || btn2 || btn3) && (millis() - lastButtonPress > debounceDelay)) {
    lastButtonPress = millis();
    buzzerBeep();
    
    switch(state) {
      case 0: // Welcome
        if (btn1 || btn2 || btn3) {
          state = 1;
          selection = 0;
          showModeSelection();
        }
        break;
        
      case 1: // Mode selection
        if (btn2) {
          selection = (selection + 1) % 2; // Cycle 0->1->0
          showModeSelection();
        }
        else if (btn1) {
          mode = selection;
          if (mode == 0) { // Hangat mode - go to heat level selection
            state = 2;
            selection = 0;
            showHeatLevelSelection();
          } else { // Dingin mode - skip heat level, go to angle
            state = 3;
            selection = 0;
            showAngleSelection();
          }
        }
        break;
        
      case 2: // Heat level selection (only for hangat mode)
        if (btn2) {
          selection = (selection + 1) % 3; // Cycle 0->1->2->0
          showHeatLevelSelection();
        }
        else if (btn1) {
          heatLevel = selection;
          state = 3;
          selection = 0;
          showAngleSelection();
        }
        break;
        
      case 3: // Angle selection
        if (btn2) {
          selection = (selection + 1) % 3; // Cycle 0->1->2->0
          showAngleSelection();
        }
        else if (btn1) {
          angle = selection;
          state = 4;
          selection = 0;
          showDurationSelection();
        }
        break;
        
      case 4: // Duration selection
        if (btn2) {
          selection = (selection + 1) % 3; // Cycle 0->1->2->0
          showDurationSelection();
        }
        else if (btn1) {
          duration = selection;
          if (angle == 0) targetAngle = 40;
          else if (angle == 1) targetAngle = 50;
          else targetAngle = 60;
          
          // Debug print for manual therapy
          Serial.print("Manual Therapy - angle: ");
          Serial.print(angle);
          Serial.print(", targetAngle: ");
          Serial.println(targetAngle);
          
          // Sync manual control values to Blynk
          Blynk.virtualWrite(V1, mode);
          Blynk.virtualWrite(V2, heatLevel);
          Blynk.virtualWrite(V3, angle);
          Blynk.virtualWrite(V4, duration);
          Blynk.virtualWrite(V5, 1); // Show therapy active
          
          state = 5;
          startCountdown();
        }
        break;
    }
  }
  
  stepper.run();
  
  if (state == 5 && countdownActive) {
    handleCountdown();
  }
  
  if (state == 6 && therapyActive) {
    updateTherapyTimer();
    controlMotor();
    
    // Update Blynk timer every 15 seconds (semua terapi)
    if (millis() - lastBlynkUpdate >= blynkUpdateInterval) {
      updateBlynkTimer();
      lastBlynkUpdate = millis();
    }
  }
  
  if (state == 8 && finishScreenActive) {
    handleFinishScreen();
  }
  
  if (state == 7 && cancelScreenActive) {
    handleCancelScreen();
  }
}

void updateBlynkTimer() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  
  if (remaining > 0) {
    int minutes = remaining / 60000;
    int seconds = (remaining % 60000) / 1000;
    
    String timeStr = "";
    if (minutes < 10) timeStr += "0";
    timeStr += String(minutes);
    timeStr += ":";
    if (seconds < 10) timeStr += "0";
    timeStr += String(seconds);
    
    Blynk.virtualWrite(V6, timeStr);
  }
}



void showModeSelection() {
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Pilih Mode");
  lcd.setCursor(4, 1);
  lcd.print(selection == 0 ? "HANGAT" : "DINGIN");
}

void showHeatLevelSelection() {
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("Level Hangat");
  lcd.setCursor(4, 1);
  if (selection == 0) lcd.print("TINGGI");
  else if (selection == 1) lcd.print("SEDANG");
  else lcd.print("RENDAH");
}

void showAngleSelection() {
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Sudut Terapi");
  lcd.setCursor(6, 1);
  if (selection == 0) lcd.print("40°");
  else if (selection == 1) lcd.print("50°");
  else lcd.print("60°");
}

void showDurationSelection() {
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Waktu Terapi");
  lcd.setCursor(5, 1);
  if (selection == 0) lcd.print("5 min");
  else if (selection == 1) lcd.print("10 min");
  else lcd.print("15 min");
}

void startCountdown() {
  digitalWrite(RELAY_PIN, HIGH);
  stepper.enableOutputs();
  gotoAngle(startPosition);
  activateTherapy();
  
  countdownValue = 3;
  countdownStart = millis();
  countdownActive = true;
  
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("MEMULAI");
  lcd.setCursor(6, 1);
  lcd.print("3");
}

void handleCountdown() {
  if (millis() - countdownStart >= 1000) {
    countdownValue--;
    if (countdownValue <= 0) {
      countdownActive = false;
      therapyActive = true;
      startTime = millis();
      therapyTime = (duration == 0) ? 300000UL : (duration == 1) ? 600000UL : 900000UL;
      stepper.setMaxSpeed(THERAPY_SPEED);
      
      // Initialize motor variables for both manual and remote therapy
      motorDirection = true;
      lastMotorMove = millis();
      gotoAngle(startPosition); // Start from initial position
      
      // Update Blynk timer saat mulai terapi (semua terapi)
      int minutes = therapyTime / 60000;
      int seconds = (therapyTime % 60000) / 1000;
      String timeStr = "";
      if (minutes < 10) timeStr += "0";
      timeStr += String(minutes);
      timeStr += ":";
      if (seconds < 10) timeStr += "0";
      timeStr += String(seconds);
      Blynk.virtualWrite(V6, timeStr);
      lastBlynkUpdate = millis();
      
      buzzerStart();
      state = 6;
      lcd.clear();
      lcd.setCursor(2, 0);
      if (remoteTherapyActive) {
        lcd.print("Remote ");
        if (mode == 0) {
          lcd.print((heatLevel == 0) ? "T" : (heatLevel == 1) ? "S" : "R");
          lcd.print(" ");
          lcd.print((angle == 0) ? "40°" : (angle == 1) ? "50°" : "60°");
        } else {
          lcd.print("Dingin ");
          lcd.print((angle == 0) ? "40°" : (angle == 1) ? "50°" : "60°");
        }
      } else {
        if (mode == 0) {
          lcd.print("Hangat ");
          lcd.print((heatLevel == 0) ? "T" : (heatLevel == 1) ? "S" : "R");
          lcd.print(" ");
          lcd.print((angle == 0) ? "40°" : (angle == 1) ? "50°" : "60°");
        } else {
          lcd.print("Dingin ");
          lcd.print((angle == 0) ? "40°" : (angle == 1) ? "50°" : "60°");
        }
      }
    } else {
      countdownStart = millis();
      lcd.setCursor(6, 1);
      lcd.print(countdownValue);
      
      // Update Blynk countdown (semua terapi)
      String timeStr = "00:0" + String(countdownValue);
      Blynk.virtualWrite(V6, timeStr);
    }
  }
}

void updateTherapyTimer() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  
  if (remaining <= 0) {
    therapyActive = false;
    stepper.setMaxSpeed(MAX_SPEED);
    gotoAngle(startPosition);
    
    // Wait for motor to reach start position
    while(stepper.distanceToGo() != 0) {
      stepper.run();
    }
    
    digitalWrite(RELAY_PIN, LOW);
    turnOffAllTherapy();
    buzzerFinish();
    
    // Send Blynk notification and update for all therapy
    Blynk.logEvent("therapy_finish", "Terapi fisioterapi telah selesai!");
    Blynk.virtualWrite(V5, 0);
    Blynk.virtualWrite(V6, "DEVICE READY");
    remoteTherapyActive = false;
    
    state = 8;
    finishScreenActive = true;
    finishScreenStart = millis();
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("SELESAI");
    lcd.setCursor(1, 1);
    lcd.print("Terapi selesai");
    return;
  }
  
  if (millis() - lastLCDUpdate >= lcdUpdateInterval) {
    int minutes = remaining / 60000;
    int seconds = (remaining % 60000) / 1000;
    
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(3, 1);
    lcd.print("Sisa ");
    if (minutes < 10) lcd.print("0");
    lcd.print(minutes);
    lcd.print(":");
    if (seconds < 10) lcd.print("0");
    lcd.print(seconds);
    
    lastLCDUpdate = millis();
  }
}

void controlMotor() {
  if (millis() - lastMotorMove >= motorDelay && stepper.distanceToGo() == 0) {
    if (motorDirection) {
      // CCW movement from start position (10°) to target angle (40°/50°/60°)
      Serial.print("Moving to targetAngle: ");
      Serial.println(targetAngle);
      gotoAngle(targetAngle);
      motorDirection = false;
    } else {
      // CW movement back to start position (10°)
      Serial.print("Moving to startPosition: ");
      Serial.println(startPosition);
      gotoAngle(startPosition);
      motorDirection = true;
    }
    lastMotorMove = millis();
  }
}

void handleFinishScreen() {
  if (millis() - finishScreenStart >= 3000) {
    finishScreenActive = false;
    stepper.disableOutputs();
    state = 0;
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print("FISIOTERAPI");
    lcd.setCursor(1, 1);
    lcd.print("Tekan tombol...");
  }
}

void handleCancelScreen() {
  if (millis() - cancelScreenStart >= 1500) {
    cancelScreenActive = false;
    stepper.disableOutputs();
    state = 0;
    lcd.clear();
    lcd.setCursor(2, 0);
    lcd.print("FISIOTERAPI");
    lcd.setCursor(1, 1);
    lcd.print("Tekan tombol......");
  }
}

