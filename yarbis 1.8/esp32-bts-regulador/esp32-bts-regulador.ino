#include <BTS7960.h>
#include <Bluepad32.h>

// --- Pines BTS7960 ---
#define BTS1_L_EN   16
#define BTS1_R_EN   17
#define BTS1_L_PWM  18
#define BTS1_R_PWM   5

#define BTS2_L_EN   19
#define BTS2_R_EN   21
#define BTS2_L_PWM  22
#define BTS2_R_PWM  23

BTS7960 bts1(BTS1_L_EN, BTS1_R_EN, BTS1_L_PWM, BTS1_R_PWM);
BTS7960 bts2(BTS2_L_EN, BTS2_R_EN, BTS2_L_PWM, BTS2_R_PWM);

// --- Bluepad32 ---
ControllerPtr myControllers[BP32_MAX_GAMEPADS] = {};

static const uint16_t BTN_R2     = 0x0080;
static const uint16_t BTN_L2     = 0x0040;
static const uint16_t BTN_SQUARE = 0x0008;
static const uint16_t BTN_R1     = 0x0020;
static const uint16_t BTN_L1     = 0x0010;

uint16_t prevButtons[BP32_MAX_GAMEPADS] = {};
int      currentSpeedPercent            = 50;
bool     motorsSafeStop                 = false;

// --- Motores ---

void stopAllMotors() {
    bts1.stop();
    bts2.stop();
}

// vel: -255..+255
void setMotor(BTS7960 &drv, int vel) {
    vel = constrain(vel, -255, 255);
    if      (vel > 0) { drv.pwm =  vel; drv.front(); }
    else if (vel < 0) { drv.pwm = -vel; drv.back();  }
    else              { drv.stop(); }
}

// --- Utilidades de controlador ---

int findControllerIndex(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
        if (myControllers[i] == ctl) return i;
    return -1;
}

bool anyControllerConnected() {
    for (auto ctl : myControllers)
        if (ctl && ctl->isConnected()) return true;
    return false;
}

// --- Callbacks Bluepad32 ---

void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (!myControllers[i]) {
            myControllers[i] = ctl;
            prevButtons[i]   = ctl->buttons();
            Serial.printf("Control conectado, slot %d\n", i);
            return;
        }
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = nullptr;
            prevButtons[i]   = 0;
            Serial.printf("Control desconectado, slot %d\n", i);
            return;
        }
    }
}

// --- Lógica de mando ---

void processGamepad(ControllerPtr ctl) {
    ctl->setColorLED(255, 0, 0);

    uint16_t btn = ctl->buttons();
    int idx = findControllerIndex(ctl);
    if (idx < 0) idx = 0;

    // Detectar pulsación nueva (flanco de subida)
    uint16_t newPress = btn & ~prevButtons[idx];
    prevButtons[idx]  = btn;

    // R1/L1 alternan velocidad entre 50% y 100%
    if (newPress & BTN_R1) currentSpeedPercent = 100;
    if (newPress & BTN_L1) currentSpeedPercent = 50;

    int speed = (255 * currentSpeedPercent) / 100;

    // Tracción: R2 adelante, L2 atrás
    int velY = 0;
    if      (btn & BTN_R2) velY =  speed;
    else if (btn & BTN_L2) velY = -speed;

    // Dirección: joystick X con dead-zone
    int16_t rawX = ctl->axisX();
    int velX = (abs(rawX) < 20) ? 0 : -(rawX * 255) / 512;

    int velA = velY + velX;
    int velB = velY - velX;

    // SQUARE: giro en el lugar
    if (btn & BTN_SQUARE) {
        velA =  speed;
        velB = -speed;
    }

    setMotor(bts1, constrain(velA, -255, 255));
    setMotor(bts2, constrain(velB, -255, 255));
}

void processControllers() {
    for (auto ctl : myControllers)
        if (ctl && ctl->isConnected() && ctl->hasData() && ctl->isGamepad())
            processGamepad(ctl);
}

// --- Setup / Loop ---

void setup() {
    Serial.begin(115200);
    delay(200);

    bts1.begin(); bts1.enable();
    bts2.begin(); bts2.enable();

    const uint8_t* bd = BP32.localBdAddress();
    Serial.printf("Bluepad32 v%s\n", BP32.firmwareVersion());
    Serial.printf("BD Addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bd[0], bd[1], bd[2], bd[3], bd[4], bd[5]);
    Serial.printf("Velocidad inicial: %d%%\n", currentSpeedPercent);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();
    BP32.enableVirtualDevice(false);

    Serial.println("Esperando controlador...");
}

void loop() {
    if (BP32.update()) {
        processControllers();
        motorsSafeStop = false;
    } else if (!anyControllerConnected() && !motorsSafeStop) {
        Serial.println("Sin controlador: motores detenidos.");
        stopAllMotors();
        motorsSafeStop = true;
    }

    vTaskDelay(1); // ceder tick al watchdog
}
