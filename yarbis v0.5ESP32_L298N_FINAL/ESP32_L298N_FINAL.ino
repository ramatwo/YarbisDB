#include <Bluepad32.h>

// CONEXION CON L298N (puente H grande)
static const int AIN1_PIN = 16;   // IN1 (Motor A)
static const int AIN2_PIN = 17;   // IN2 (Motor A)
static const int PWMA_PIN = 18;   // ENA (PWM) -> canal 0

static const int BIN1_PIN = 19;   // IN3 (Motor B)
static const int BIN2_PIN = 21;   // IN4 (Motor B)
static const int PWMB_PIN = 22;   // ENB (PWM) -> canal 1

// PWM LEDC
static const int PWM_FREQ = 5000;  // 5 kHz
static const int PWM_RES  = 8;     // 8 bits (0–255)

// 4 joysticks maximo
ControllerPtr myControllers[BP32_MAX_GAMEPADS] = { nullptr };

// Máscaras de botones PS4
static const uint16_t BUTTON_R2 = 0x0080;
static const uint16_t BUTTON_L2 = 0x0040;
static const uint16_t BUTTON_SQUARE = 0x0008;

// ---------------------------------------------------------
// Ajusta un motor (ahora para L298N)
// pinIN1 / pinIN2 → direccional
// pwmChannel (ledc channel) → velocidad
// vel = -255..+255
// ---------------------------------------------------------
void setMotor(int pinIN1, int pinIN2, int pwmChannel, int vel) {
    if (vel > 0) {
        digitalWrite(pinIN1, HIGH);
        digitalWrite(pinIN2, LOW);
        ledcWrite(pwmChannel, constrain(vel, 0, 255));
    }
    else if (vel < 0) {
        digitalWrite(pinIN1, LOW);
        digitalWrite(pinIN2, HIGH);
        ledcWrite(pwmChannel, constrain(-vel, 0, 255));
    }
    else {
        // 0 -> coast (no PWM). Si querés braking activo usá HIGH/HIGH aquí.
        digitalWrite(pinIN1, LOW);
        digitalWrite(pinIN2, LOW);
        ledcWrite(pwmChannel, 0);
    }
}

// ---------------------------------------------------------
// Callback: control conectado
// ---------------------------------------------------------
void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == nullptr) {
            myControllers[i] = ctl;
            Serial.printf("** PS4 Conectado, índice = %d **\n", i);
            return;
        }
    }
}

// ---------------------------------------------------------
// Callback: control desconectado
// ---------------------------------------------------------
void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = nullptr;
            Serial.printf("** PS4 Desconectado, índice = %d **\n", i);
            return;
        }
    }
}

// ---------------------------------------------------------
// Procesa el mando: R2 = avanzar, L2 = retroceder, joystick X = giro
// ---------------------------------------------------------
void processGamepad(ControllerPtr ctl) {
    // --- VELOCIDAD FIJA PARA ADELANTE/ATRÁS ---
    const int speed = 255;   // 0–255
    ctl->setColorLED(255, 0, 0);

    // Leer estado de botones
    uint16_t btn = ctl->buttons();

    // Determinar velY según triggers (intercambiados)
    int velY = 0;
    if (btn & BUTTON_R2) {
        velY = -speed;    // R2 → adelante (nota: tu convención original)
    }
    else if (btn & BUTTON_L2) {
        velY = speed;     // L2 → atrás
    }

    // --- LECTURA JOYSTICK X PARA GIRO ---
    int16_t rawX = ctl->axisX();     // rango -512..+511
    if (abs(rawX) < 20) rawX = 0;    // dead-zone
    int velX = (rawX * 255) / 512;   // -255..+255
    velX = -velX;                    // invertir si lo deseas

    // --- CÁLCULO DIFERENCIAL TIPO “AUTO” ---
    int velA = velY + velX;   // motor A (izquierdo)
    int velB = velY - velX;   // motor B (derecho)

    // Si se aprieta SQUARE: giro en el lugar (360 / spin)
    if (btn & BUTTON_SQUARE) {
        // Spin clockwise: A adelante, B atrás (ajusta signos si invertidos físicamente)
        velA = speed;
        velB = -speed;
        // Si querés mantener el giro por un tiempo, podés usar un bloqueo temporal,
        // pero es mejor manejarlo por estados; aquí dejamos que el operador suelte.
    }

    // Constrain al rango válido
    velA = constrain(velA, -255, 255);
    velB = constrain(velB, -255, 255);

    // Enviar órdenes (pwm channels: 0 -> motor A, 1 -> motor B)
    setMotor(AIN1_PIN, AIN2_PIN, 0, velA);
    setMotor(BIN1_PIN, BIN2_PIN, 1, velB);
}

// ---------------------------------------------------------
// Recorre todos los controladores activos
// ---------------------------------------------------------
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
// Setup de Arduino (se llama una sola vez)
// -----------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    // Pines L298N como salida
    pinMode(AIN1_PIN, OUTPUT);
    pinMode(AIN2_PIN, OUTPUT);
    pinMode(PWMA_PIN, OUTPUT);
    pinMode(BIN1_PIN, OUTPUT);
    pinMode(BIN2_PIN, OUTPUT);
    pinMode(PWMB_PIN, OUTPUT);

    // Inicializar PWM (LEDC): canal 0 -> PWMA_PIN, canal 1 -> PWMB_PIN
    ledcSetup(0, PWM_FREQ, PWM_RES);
    ledcAttachPin(PWMA_PIN, 0);
    ledcSetup(1, PWM_FREQ, PWM_RES);
    ledcAttachPin(PWMB_PIN, 1);

    // Inicializar Bluepad32
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
// Loop principal de Arduino
// -----------------------------
void loop() {
    if (BP32.update()) {
        processControllers();
    }
    // Ceder 1 tick para evitar watchdog
    vTaskDelay(1);
}