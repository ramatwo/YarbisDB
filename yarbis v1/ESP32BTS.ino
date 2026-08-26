#include <BTS7960.h>
#include <Bluepad32.h>

//MAPEO DE PINES
//BTS1 - motor izquierdo
#define BTS1_L_EN    16
#define BTS1_R_EN    17
#define BTS1_L_PWM   18
#define BTS1_R_PWM   5

//BTS2 - motor derecho
#define BTS2_L_EN    19
#define BTS2_R_EN    21
#define BTS2_L_PWM   22
#define BTS2_R_PWM   23

//Objetos para cada driver BTS (izquierda y derecha)
BTS7960 bts1(BTS1_L_EN, BTS1_R_EN, BTS1_L_PWM, BTS1_R_PWM);
BTS7960 bts2(BTS2_L_EN, BTS2_R_EN, BTS2_L_PWM, BTS2_R_PWM);

// --- Bluepad32 (mando) ---
ControllerPtr myControllers[BP32_MAX_GAMEPADS] = { nullptr };

// Máscaras
static const uint16_t BUTTON_R2 = 0x0080;
static const uint16_t BUTTON_L2 = 0x0040;
static const uint16_t BUTTON_SQUARE = 0x0008;
static const unit16_t BUTTON_R1 = 0x0020
static const unit16_t BUTTON_L1 = : 0x0010
// Estado para detener motores si no hay controladores
bool motorsStoppedDueToNoController = false;

// --- Funciones auxiliares ---

// Detiene ambos motores (coast)
void stopAllMotors() {
    bts1.stop();
    bts2.stop();
}

// Enviar velocidad a un BTS7960
// vel: -255 .. +255
void setMotorBTS(BTS7960 &drv, int vel) {
    vel = constrain(vel, -255, 255);
    if (vel > 0) {
        drv.pwm = vel;   // 0..255
        drv.front();     // adelante
    }
    else if (vel < 0) {
        drv.pwm = -vel;  // convertir a positivo
        drv.back();      // atrás
    }
    else {
        drv.stop();      // coast
    }
}

// Comprueba si hay al menos un controlador conectado
bool anyControllerConnected() {
    for (auto ctl : myControllers) {
        if (ctl && ctl->isConnected()) return true;
    }
    return false;
}

// ---------------------------------------------------------
// Callbacks Bluepad32
// ---------------------------------------------------------
void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;
            Serial.printf("** Control conectado, índice = %d **\n", i);
            return;
        }
    }
}

void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = nullptr;
            Serial.printf("** Control desconectado, índice = %d **\n", i);
            return;
        }
    }
}

// ---------------------------------------------------------
// Procesa el mando principal (usamos el primer gamepad con data)
// ---------------------------------------------------------
void processGamepad(ControllerPtr ctl) {
    // Velocidad base
    const int speed = 255; // 0..255
    ctl->setColorLED(255, 0, 0);

    // Botones
    uint16_t btn = ctl->buttons();

    int velY = 0; // adelante/atrás
    if (btn & BUTTON_R2) {
        velY = speed; // R2 -> adelante (misma convención tuya)
    }
    else if (btn & BUTTON_L2) {
        velY = -speed;  // L2 -> atrás
    }

    // Joystick X para giro
    int16_t rawX = ctl->axisX(); // -512..+511
    if (abs(rawX) < 20) rawX = 0; // dead-zone
    int velX = (rawX * 255) / 512; // -255..+255
    velX = -velX; // invertir si es necesario

    int velA = velY + velX; // motor A (izquierdo)
    int velB = velY - velX; // motor B (derecho)

    // Giro en el lugar con SQUARE
    if (btn & BUTTON_SQUARE) {
        velA = speed;
        velB = -speed;
    }

   
    velA = constrain(velA, -255, 255);
    velB = constrain(velB, -255, 255);


    setMotorBTS(bts1, velA);
    setMotorBTS(bts2, velB);
}


void processControllers() {
    for (auto ctl : myControllers) {
        if (ctl && ctl->isConnected() && ctl->hasData()) {
            if (ctl->isGamepad()) {
                processGamepad(ctl);
            }
        }
    }
}

// -----------------------------
// Setup
// -----------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    
    bts1.begin();
    bts2.begin();
    bts1.enable();
    bts2.enable();

    //conexion bluepad
    Serial.printf("Bluepad32 v%s\n", BP32.firmwareVersion());
    const uint8_t* bd = BP32.localBdAddress();
    Serial.printf("BD Addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  bd[0], bd[1], bd[2], bd[3], bd[4], bd[5]);

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();
    BP32.enableVirtualDevice(false);

    Serial.println("Esperando DualShock 4...");
}

// -----------------------------
// Loop principal
// -----------------------------
void loop() {
    if (BP32.update()) {
        
        processControllers();
        motorsStoppedDueToNoController = false;
    }
    else {
        
        if (!anyControllerConnected()) {
            if (!motorsStoppedDueToNoController) {
                Serial.println("Ningun controlador conectado: deteniendo motores por seguridad.");
                stopAllMotors();
                motorsStoppedDueToNoController = true;
            }
        }
    }

    // Ceder 1 tick para evitar watchdog
    vTaskDelay(1);
}