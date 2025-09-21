#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>

// LCD I2C (address 0x27, 16x2)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Button pins
#define BTN1 6
#define BTN2 7
#define BTN3 8

// Stepper motor pins
#define PUL_PIN 2   // STEP/PUL
#define DIR_PIN 3   // DIR
#define ENA_PIN 4   // ENA (aktif-LOW pada DM542)
#define RELAY_PIN 10

// Stepper motor setup
AccelStepper stepper(AccelStepper::DRIVER, PUL_PIN, DIR_PIN);
const long STEPS_PER_REV = 6400;
const float MAX_SPEED = 1200.0;
const float THERAPY_SPEED = 2400.0; // Faster speed for therapy swing
const float ACCEL = 1200.0;

// Variables
int state = 0;
int mode = 0; // 0=panas, 1=dingin
int angle = 0; // 0=40°, 1=50°, 2=60°
int duration = 0; // 0=5min, 1=10min, 2=15min
int selection = 0; // current selection index
unsigned long startTime = 0;
unsigned long therapyTime = 0;
bool therapyActive = false;

// Motor variables
int startPosition = 80; // Start position
int targetAngle = 40; // Target angle based on user selection
bool motorDirection = true; // true = going to target, false = returning to start
unsigned long lastMotorMove = 0;
const unsigned long motorDelay = 2000; // 2 seconds between movements

// Countdown variables
int countdownValue = 0;
unsigned long countdownStart = 0;
bool countdownActive = false;

// LCD update variables (separate from motor timing)
unsigned long lastLCDUpdate = 0;
const unsigned long lcdUpdateInterval = 1000; // Update LCD every 1 second

// Finish screen variables
unsigned long finishScreenStart = 0;
bool finishScreenActive = false;

// Cancel screen variables
unsigned long cancelScreenStart = 0;
bool cancelScreenActive = false;

// Stepper functions
inline long degToSteps(float deg) {
  return lroundf((deg / 360.0f) * STEPS_PER_REV);
}

void gotoAngle(float deg) {
  stepper.moveTo(degToSteps(deg));
}

// Button variables
unsigned long lastButtonPress = 0;
unsigned long btn1HoldStart = 0;
bool btn1Holding = false;
const unsigned long debounceDelay = 300;
const unsigned long longPressDelay = 2000;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  
  // Stepper and relay setup
  stepper.setEnablePin(ENA_PIN);
  stepper.setPinsInverted(false, false, true); // DM542 ENA aktif-LOW
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay off initially
  
  // Welcome screen
  lcd.setCursor(4, 0);
  lcd.print("WELCOME");
  lcd.setCursor(2, 1);
  lcd.print("Press any key");
}

void loop() {
  // Simple button reading
  bool btn1 = !digitalRead(BTN1);
  bool btn2 = !digitalRead(BTN2);
  bool btn3 = !digitalRead(BTN3);
  
  // Handle BTN1 long press for cancel (only in menu states and therapy)
  if (btn1 && ((state >= 1 && state <= 3) || (state == 5 && therapyActive))) {
    if (!btn1Holding) {
      btn1HoldStart = millis();
      btn1Holding = true;
    }
    else if (millis() - btn1HoldStart >= longPressDelay) {
      // Long press - cancel
      if (state == 5 && therapyActive) {
        therapyActive = false;
        digitalWrite(RELAY_PIN, LOW); // Turn off relay
        stepper.disableOutputs(); // Disable stepper
        stepper.setMaxSpeed(MAX_SPEED); // Reset to normal speed
      }
      state = 6; // Cancel screen state
      selection = 0;
      btn1Holding = false;
      cancelScreenActive = true;
      cancelScreenStart = millis();
      lcd.clear();
      lcd.setCursor(3, 0);
      lcd.print("CANCELLED");
      return;
    }
  }
  else {
    btn1Holding = false;
  }
  
  // Normal button handling with debounce
  if ((btn1 || btn2 || btn3) && (millis() - lastButtonPress > debounceDelay)) {
    lastButtonPress = millis();
    
    Serial.print("Buttons: ");
    Serial.print(btn1); Serial.print(" ");
    Serial.print(btn2); Serial.print(" ");
    Serial.println(btn3);
    
    switch(state) {
      case 0: // Welcome
        if (btn1 || btn2 || btn3) {
          state = 1;
          selection = 0;
          showModeSelection();
        }
        break;
        
      case 1: // Mode selection
        if (btn2 && selection > 0) {
          selection--;
          showModeSelection();
        }
        else if (btn3 && selection < 1) {
          selection++;
          showModeSelection();
        }
        else if (btn1) {
          mode = selection;
          state = 2;
          selection = 0;
          showAngleSelection();
        }
        break;
        
      case 2: // Angle selection
        if (btn2 && selection > 0) {
          selection--;
          showAngleSelection();
        }
        else if (btn3 && selection < 2) {
          selection++;
          showAngleSelection();
        }
        else if (btn1) {
          angle = selection;
          state = 3;
          selection = 0;
          showDurationSelection();
        }
        break;
        
      case 3: // Duration selection
        if (btn2 && selection > 0) {
          selection--;
          showDurationSelection();
        }
        else if (btn3 && selection < 2) {
          selection++;
          showDurationSelection();
        }
        else if (btn1) {
          duration = selection;
          // Set target angle based on user selection
          targetAngle = (angle == 0) ? 40 : (angle == 1) ? 50 : 60;
          state = 4; // Countdown state
          startCountdown();
        }
        break;
    }
  }
  
  // Always run stepper motor
  stepper.run();
  
  // Handle countdown
  if (state == 4 && countdownActive) {
    handleCountdown();
  }
  
  // Handle therapy countdown and timer
  if (state == 5 && therapyActive) {
    updateTherapyTimer();
    controlMotor();
  }
  
  // Handle finish screen
  if (state == 7 && finishScreenActive) {
    handleFinishScreen();
  }
  
  // Handle cancel screen
  if (state == 6 && cancelScreenActive) {
    handleCancelScreen();
  }
}

void showModeSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Mode Terapi:");
  lcd.setCursor(5, 1);
  lcd.print(selection == 0 ? "PANAS" : "DINGIN");
}

void showAngleSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Kemiringan:");
  lcd.setCursor(6, 1);
  if (selection == 0) lcd.print("40°");
  else if (selection == 1) lcd.print("50°");
  else lcd.print("60°");
}

void showDurationSelection() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Durasi Terapi:");
  lcd.setCursor(6, 1);
  if (selection == 0) lcd.print("5 min");
  else if (selection == 1) lcd.print("10 min");
  else lcd.print("15 min");
}

void showStartScreen() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print((mode == 0) ? "Panas " : "Dingin ");
  lcd.print((angle == 0) ? "40°" : (angle == 1) ? "50°" : "60°");
  lcd.print(" ");
  lcd.print((duration == 0) ? "5m" : (duration == 1) ? "10m" : "15m");
  lcd.setCursor(5, 1);
  lcd.print(selection == 0 ? "START" : "BACK");
}

void startCountdown() {
  // Activate relay and enable stepper
  digitalWrite(RELAY_PIN, HIGH); // Turn on relay
  stepper.enableOutputs(); // Enable stepper
  gotoAngle(startPosition); // Move to start position (80°)
  
  countdownValue = 3;
  countdownStart = millis();
  countdownActive = true;
  
  lcd.clear();
  lcd.setCursor(6, 0);
  lcd.print("START");
  lcd.setCursor(7, 1);
  lcd.print(countdownValue);
}

void updateTherapyTimer() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  
  if (remaining <= 0) {
    // Therapy finished
    therapyActive = false;
    digitalWrite(RELAY_PIN, LOW); // Turn off relay
    stepper.disableOutputs(); // Disable stepper
    stepper.setMaxSpeed(MAX_SPEED); // Reset to normal speed
    state = 7; // Finish screen state
    finishScreenActive = true;
    finishScreenStart = millis();
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print("SELESAI");
    lcd.setCursor(1, 1);
    lcd.print("Terapi Selesai");
    return;
  }
  
  // Update LCD display only every second (separate from motor timing)
  if (millis() - lastLCDUpdate >= lcdUpdateInterval) {
    int minutes = remaining / 60000;
    int seconds = (remaining % 60000) / 1000;
    
    lcd.setCursor(0, 1);
    lcd.print("Sisa: ");
    if (minutes < 10) lcd.print("0");
    lcd.print(minutes);
    lcd.print(":");
    if (seconds < 10) lcd.print("0");
    lcd.print(seconds);
    lcd.print(" 1:STOP");
    
    lastLCDUpdate = millis();
  }
}

void handleFinishScreen() {
  if (millis() - finishScreenStart >= 3000) {
    finishScreenActive = false;
    state = 0;
    stepper.setMaxSpeed(MAX_SPEED);
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("WELCOME");
    lcd.setCursor(2, 1);
    lcd.print("Press any key");
  }
}

void handleCancelScreen() {
  if (millis() - cancelScreenStart >= 1500) {
    cancelScreenActive = false;
    state = 0;
    stepper.setMaxSpeed(MAX_SPEED);
    lcd.clear();
    lcd.setCursor(4, 0);
    lcd.print("WELCOME");
    lcd.setCursor(2, 1);
    lcd.print("Press any key");
  }
}

void controlMotor() {
  if (millis() - lastMotorMove >= motorDelay && stepper.distanceToGo() == 0) {
    if (motorDirection) {
      // Move to target angle
      gotoAngle(targetAngle);
      motorDirection = false;
    } else {
      // Return to start position
      gotoAngle(startPosition);
      motorDirection = true;
    }
    lastMotorMove = millis();
  }
}

void handleCountdown() {
  if (millis() - countdownStart >= lcdUpdateInterval) {
    countdownValue--;
    if (countdownValue > 0) {
      lcd.setCursor(7, 1);
      lcd.print(countdownValue);
      countdownStart = millis();
    } else {
      // Start therapy
      countdownActive = false;
      therapyActive = true;
      state = 5;
      startTime = millis();
      lastMotorMove = millis();
      lastLCDUpdate = millis();
      motorDirection = true;
      therapyTime = (duration == 0) ? 300000 : (duration == 1) ? 600000 : 900000;
      
      // Set faster speed for therapy
      stepper.setMaxSpeed(THERAPY_SPEED);
      
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("TERAPI AKTIF");
    }
  }
}

