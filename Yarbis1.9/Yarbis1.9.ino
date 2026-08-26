#include <BTS7960.h>
#include <Bluepad32.h>

//BTS6970
#define BTS1_L_EN   16
#define BTS1_R_EN   17
#define BTS1_L_PWM  18
#define BTS1_R_PWM   5

#define BTS2_L_EN   19
#define BTS2_R_EN   21
#define BTS2_L_PWM  22
#define BTS2_R_PWM  23

BTS7960 bts1(BTS1_L_EN, BTS1_R_EN, BTS1_L_PWM, BTS1_R_PWM); //motor izquierda
BTS7960 bts2(BTS2_L_EN, BTS2_R_EN, BTS2_L_PWM, BTS2_R_PWM); //motor derecha

ControllerPtr myControllers[BP32_MAX_GAMEPADS] = { nullptr };

static const uint16_t BUTTON_R2     = 0x0080;
static const uint16_t BUTTON_L2     = 0x0040;
static const uint16_t BUTTON_SQUARE = 0x0008;
static const uint16_t BUTTON_R1     = 0x0020;
static const uint16_t BUTTON_L1     = 0x0010;

//estado global de yarbis :v
int      currentSpeedPercent            = 50;  //50% o 100%. 50 por defecto
bool     motorsStoppedDueToNoController = false;
uint16_t prevButtons[BP32_MAX_GAMEPADS] = {0}; //para detección de flanco

//auxiliaress
void stopAllMotors() {
    bts1.Stop();
    bts2.Stop();
}

// vel: -255..+255
void setMotorBTS(BTS7960 &drv, int vel) {
    vel = constrain(vel, -255, 255);
    if      (vel > 0) drv.TurnRight((uint8_t) vel);
    else if (vel < 0) drv.TurnLeft ((uint8_t)-vel);
    else              drv.Stop();
}

bool anyControllerConnected() {
    for (auto ctl : myControllers)
        if (ctl && ctl->isConnected()) return true;
    return false;
}

int findControllerIndex(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; ++i)
        if (myControllers[i] == ctl) return i;
    return -1;
}

//callbacks bluepad32
void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;
            prevButtons[i]   = ctl->buttons(); //evita falsos flancos al conectar
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

//logica de control
void processGamepad(ControllerPtr ctl) {
    ctl->setColorLED(255, 0, 0);

    uint16_t btn          = ctl->buttons();
    int      idx          = findControllerIndex(ctl);
    if (idx < 0) idx      = 0;
    uint16_t newlyPressed = btn & (~prevButtons[idx]);

    //control de velociades. R1 sube 100 - L1 baja a 50
    if (newlyPressed & BUTTON_R1) currentSpeedPercent = 100;
    if (newlyPressed & BUTTON_L1) currentSpeedPercent = 50;

    prevButtons[idx] = btn;

    int actualSpeed = (255 * currentSpeedPercent) / 100;

    //R2 = adelante, L2 = atras
    int velY = 0;
    if      (btn & BUTTON_R2) velY =  actualSpeed;
    else if (btn & BUTTON_L2) velY = -actualSpeed;

    //zona muerta joystick
    int16_t rawX = ctl->axisX();
    if (abs(rawX) < 20) rawX = 0;
    int velX = -((rawX * 255) / 512);

    int velA = velY + velX;
    int velB = velY - velX;

    //cuadrado: giro en propio eje
    if (btn & BUTTON_SQUARE) {
        velA =  actualSpeed;
        velB = -actualSpeed;
    }

    setMotorBTS(bts1, constrain(velA, -255, 255));
    setMotorBTS(bts2, constrain(velB, -255, 255));
}

void processControllers() {
    for (auto ctl : myControllers)
        if (ctl && ctl->isConnected() && ctl->isGamepad())
            processGamepad(ctl);
}

//----------------setup y loop-----------------
void setup() {
    Serial.begin(115200);
    delay(200);

    bts1.Enable();
    bts2.Enable();

    const uint8_t* bd = BP32.localBdAddress();
    Serial.printf("Bluepad32 v%s\n", BP32.firmwareVersion());
    Serial.printf("BD: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bd[0], bd[1], bd[2], bd[3], bd[4], bd[5]);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();

    Serial.println("esperando el fokin joystick...");
}

void loop() {
    BP32.update();

    if (anyControllerConnected()) {
        processControllers();
        motorsStoppedDueToNoController = false;
    } else if (!motorsStoppedDueToNoController) {
        stopAllMotors();
        motorsStoppedDueToNoController = true;
        Serial.println("Sin controlador: motores detenidos.");
    }

    delay(10); //~100Hz, evita saturar el buffer interno de Bluepad32
}
