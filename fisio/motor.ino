#include <AccelStepper.h>

// Pin
#define PUL_PIN 2   // STEP/PUL
#define DIR_PIN 3   // DIR
#define ENA_PIN 4   // ENA (aktif-LOW pada DM542)

// Konfigurasi mekanik
const long STEPS_PER_REV   = 6400;              // 1 putaran penuh (sesuaikan microstep)
const long HALF_REV_STEPS  = STEPS_PER_REV / 2; // 180° = 3200 jika 6400/rev

// Kinematika
const float MAX_SPEED = 1200.0;   // steps/s (sesuaikan kemampuan motor/driver)
const float ACCEL     = 1200.0;   // steps/s^2

// Jeda berhenti di ujung
const unsigned long DWELL_MS = 5000; // 5 detik

AccelStepper stepper(AccelStepper::DRIVER, PUL_PIN, DIR_PIN);

// State machine
enum State { MOVING_TO_180, DWELL_180, MOVING_TO_0, DWELL_0 };
State state = MOVING_TO_180;
unsigned long dwellStartMs = 0;

inline long degToSteps(float deg) {
  return lroundf((deg / 360.0f) * STEPS_PER_REV);
}

void gotoAngle(float deg) {
  stepper.moveTo(degToSteps(deg));
}

void setup() {
  // Atur enable pin di AccelStepper (biar bisa enable/disable lewat library)
  stepper.setEnablePin(ENA_PIN);
  // DM542 ENA aktif-LOW → invert enable = true
  stepper.setPinsInverted(false, false, true); // (dir, step, enable)

  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);

  stepper.enableOutputs(); // akan mengeluarkan LOW ke ENA (aktif)

  // Mulai gerak ke 180°
  gotoAngle(180.0f);
  state = MOVING_TO_180;
}

void loop() {
  // Wajib dipanggil terus agar gerak non-blocking
  stepper.run();

  switch (state) {
    case MOVING_TO_180:
      if (stepper.distanceToGo() == 0) { // sudah sampai 180°
        dwellStartMs = millis();
        state = DWELL_180;
      }
      break;

    case DWELL_180:
      if (millis() - dwellStartMs >= DWELL_MS) {
        gotoAngle(0.0f);          // balik ke 0°
        state = MOVING_TO_0;
      }
      break;

    case MOVING_TO_0:
      if (stepper.distanceToGo() == 0) { // sudah sampai 0°
        dwellStartMs = millis();
        state = DWELL_0;
      }
      break;

    case DWELL_0:
      if (millis() - dwellStartMs >= DWELL_MS) {
        gotoAngle(180.0f);         // lagi ke 180°
        state = MOVING_TO_180;
      }
      break;
  }
}
