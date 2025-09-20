#include <LiquidCrystal_I2C.h>

// LCD I2C (address 0x27, 16x2)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Button pins
#define BTN1 6
#define BTN2 7
#define BTN3 8

// Variables
int state = 0;
int mode = 0; // 0=panas, 1=dingin
int angle = 0; // 0=40°, 1=50°, 2=60°
int duration = 0; // 0=5min, 1=10min, 2=15min
int selection = 0; // current selection index
unsigned long startTime = 0;
unsigned long therapyTime = 0;
bool therapyActive = false;

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
      }
      state = 0;
      selection = 0;
      btn1Holding = false;
      lcd.clear();
      lcd.setCursor(3, 0);
      lcd.print("CANCELLED");
      delay(1500);
      lcd.clear();
      lcd.setCursor(4, 0);
      lcd.print("WELCOME");
      lcd.setCursor(2, 1);
      lcd.print("Press any key");
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
          state = 5;
          startCountdown();
        }
        break;
    }
  }
  
  // Handle therapy countdown and timer
  if (state == 5 && therapyActive) {
    updateTherapyTimer();
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
  for (int i = 3; i > 0; i--) {
    lcd.clear();
    lcd.setCursor(6, 0);
    lcd.print("START");
    lcd.setCursor(7, 1);
    lcd.print(i);
    delay(1000);
  }
  
  therapyActive = true;
  startTime = millis();
  therapyTime = (duration == 0) ? 300000 : (duration == 1) ? 600000 : 900000; // 5, 10, 15 min in ms
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TERAPI AKTIF");
}

void updateTherapyTimer() {
  unsigned long elapsed = millis() - startTime;
  unsigned long remaining = therapyTime - elapsed;
  
  if (remaining <= 0) {
    // Therapy finished
    therapyActive = false;
    lcd.clear();
    lcd.setCursor(3, 0);
    lcd.print("SELESAI");
    lcd.setCursor(1, 1);
    lcd.print("Terapi Selesai");
    delay(3000);
    state = 0;
    setup();
    return;
  }
  
  // Display remaining time
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
}


