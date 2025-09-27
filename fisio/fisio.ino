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

// Dimmer and cold relay pins
#define DIMMER_OUTPUT_PIN 16
#define DIMMER_ZEROCROSS_PIN 33
#define RELAY_COLD 27

// Stepper motor setup
AccelStepper stepper(AccelStepper::DRIVER, PUL_PIN, DIR_PIN);

// Dimmer setup
dimmerLamp dimmer(DIMMER_OUTPUT_PIN, DIMMER_ZEROCROSS_PIN);
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
int startPosition = 0;   // Vertical position (0 degrees)
int targetAngle = 40;
bool motorDirection = true;  // true = to target angle, false = back to 0
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
const unsigned long debounceDelay = 300;

// Stepper functions
inline long degToSteps(float deg) {
  return lroundf((deg / 360.0f) * STEPS_PER_REV);
}

void gotoAngle(float deg) {
  stepper.moveTo(degToSteps(deg));
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
  dimmer.setPower(0);        // Turn off dimmer
  digitalWrite(RELAY_COLD, LOW);  // HIGH trigger - LOW = OFF
}

void activateTherapy() {
  turnOffAllTherapy(); // Safety: turn off all first
  
  if (mode == 0) { // Hangat mode
    int dimmerValue = (heatLevel == 0) ? 90 : (heatLevel == 1) ? 60 : 30;
    dimmer.setPower(dimmerValue);
  } else { // Dingin mode
    digitalWrite(RELAY_COLD, HIGH);  // HIGH trigger - HIGH = ON
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
    targetAngle = (angle == 0) ? 40 : (angle == 1) ? 50 : 60;
    
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
  
  // Wait for motor to reach start position
  while(stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  stepper.disableOutputs();
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
  // Dimmer and cold relay setup
  dimmer.begin(NORMAL_MODE, ON);
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
  digitalWrite(RELAY_PIN, HIGH);
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
  
  // WiFi connected, start Blynk
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print("Starting Blynk");
  delay(1000);
  lcd.clear();
  
  Blynk.config(auth);
  Blynk.connect();
  
  // Set motor position to start position (vertical = 0 degrees)
  stepper.setCurrentPosition(degToSteps(startPosition));
  stepper.disableOutputs();
  
  // Welcome screen
  delay(1000);
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
        
        stepper.disableOutputs();
        turnOffAllTherapy();
        
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
        
        stepper.disableOutputs();
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
  if (!remoteTherapyActive && (btn1 || btn2) && (millis() - lastButtonPress > debounceDelay)) {
    lastButtonPress = millis();
    buzzerBeep();
    
    switch(state) {
      case 0: // Welcome
        if (btn1 || btn2) {
          state = 1;
          selection = 0;
          showModeSelection();
        }
        break;
        
      case 1: // Mode selection
        if (btn2) {
          selection = (selection + 1) % 2;
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
          selection = (selection + 1) % 3;
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
          selection = (selection + 1) % 3;
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
          selection = (selection + 1) % 3;
          showDurationSelection();
        }
        else if (btn1) {
          duration = selection;
          targetAngle = (angle == 0) ? 40 : (angle == 1) ? 50 : 60;
          
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
    
    // Update Blynk status periodically
    if (millis() - lastBlynkUpdate > blynkUpdateInterval) {
      lastBlynkUpdate = millis();
      updateBlynkStatus();
    }
  }
  
  if (state == 7 && cancelScreenActive) {
    if (millis() - cancelScreenStart > 2000) {
      cancelScreenActive = false;
      state = 0;
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("FISIOTERAPI");
      lcd.setCursor(1, 1);
      lcd.print("Tekan tombol...");
    }
  }
  
  if (finishScreenActive) {
    if (millis() - finishScreenStart > 3000) {
      finishScreenActive = false;
      state = 0;
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("FISIOTERAPI");
      lcd.setCursor(1, 1);
      lcd.print("Tekan tombol...");
    }
  }
}

void startCountdown() {
  countdownValue = 5;
  countdownStart = millis();
  countdownActive = true;
  lcd.clear();
  lcd.setCursor(5, 0);
  lcd.print("MULAI");
  lcd.setCursor(7, 1);
  lcd.print(countdownValue);
}

void handleCountdown() {
  if (millis() - countdownStart >= 1000) {
    countdownValue--;
    countdownStart = millis();
    
    if (countdownValue > 0) {
      lcd.setCursor(7, 1);
      lcd.print(countdownValue);
    } else {
      countdownActive = false;
      startTherapy();
    }
  }
}

void startTherapy() {
  therapyActive = true;
  startTime = millis();
  therapyTime = (duration == 0) ? 300000 : (duration == 1) ? 600000 : 900000; // 5, 10, 15 min
  
  digitalWrite(RELAY_PIN, HIGH);
  stepper.enableOutputs();
  stepper.setMaxSpeed(THERAPY_SPEED);
  
  activateTherapy();
  buzzerStart();
  
  state = 6;
  motorDirection = false;
  gotoAngle(targetAngle);
  
  Blynk.virtualWrite(V6, "TERAPI AKTIF");
  Blynk.logEvent("therapy_started", "Terapi dimulai!");
  
  updateTherapyDisplay();
}

void updateTherapyTimer() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  
  if (remaining <= 0) {
    finishTherapy();
    return;
  }
  
  if (millis() - lastLCDUpdate > lcdUpdateInterval) {
    lastLCDUpdate = millis();
    updateTherapyDisplay();
  }
}

void updateTherapyDisplay() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  int minutes = remaining / 60000;
  int seconds = (remaining % 60000) / 1000;
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(mode == 0 ? "HANGAT" : "DINGIN");
  lcd.setCursor(8, 0);
  lcd.print(targetAngle);
  lcd.print("deg");
  lcd.setCursor(4, 1);
  if (minutes < 10) lcd.print("0");
  lcd.print(minutes);
  lcd.print(":");
  if (seconds < 10) lcd.print("0");
  lcd.print(seconds);
}

void controlMotor() {
  if (stepper.distanceToGo() == 0 && millis() - lastMotorMove > motorDelay) {
    lastMotorMove = millis();
    
    if (motorDirection) {
      gotoAngle(targetAngle); // Move to target angle (40°/50°/60°)
      motorDirection = false;
    } else {
      gotoAngle(startPosition);  // Move back to 0 degrees (vertical)
      motorDirection = true;
    }
  }
}

void finishTherapy() {
  therapyActive = false;
  remoteTherapyActive = false;
  
  stepper.setMaxSpeed(MAX_SPEED);
  gotoAngle(startPosition);
  
  while(stepper.distanceToGo() != 0) {
    stepper.run();
  }
  
  stepper.disableOutputs();
  turnOffAllTherapy();
  stepper.disableOutputs();
  
  buzzerFinish();
  
  Blynk.virtualWrite(V5, 0);
  Blynk.virtualWrite(V6, "TERAPI SELESAI");
  Blynk.logEvent("therapy_finished", "Terapi selesai!");
  
  finishScreenActive = true;
  finishScreenStart = millis();
  
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("SELESAI");
  lcd.setCursor(2, 1);
  lcd.print("Terapi selesai");
}

void showModeSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode:");
  lcd.setCursor(0, 1);
  lcd.print(selection == 0 ? ">Hangat  Dingin" : " Hangat >Dingin");
}

void showHeatLevelSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Level Panas:");
  lcd.setCursor(0, 1);
  String levels[] = {">HIGH MED LOW", " HIGH>MED LOW", " HIGH MED>LOW"};
  lcd.print(levels[selection]);
}

void showAngleSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Sudut:");
  lcd.setCursor(0, 1);
  String angles[] = {">40  50  60", " 40 >50  60", " 40  50 >60"};
  lcd.print(angles[selection]);
}

void showDurationSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Durasi (menit):");
  lcd.setCursor(0, 1);
  String durations[] = {">5  10  15", " 5 >10  15", " 5  10 >15"};
  lcd.print(durations[selection]);
}

void updateBlynkStatus() {
  if (therapyActive) {
    unsigned long elapsed = millis() - startTime;
    unsigned long remaining = therapyTime - elapsed;
    int minutes = remaining / 60000;
    int seconds = (remaining % 60000) / 1000;
    
    String status = "TERAPI: ";
    if (minutes < 10) status += "0";
    status += String(minutes) + ":";
    if (seconds < 10) status += "0";
    status += String(seconds);
    
    Blynk.virtualWrite(V6, status);
  }
}