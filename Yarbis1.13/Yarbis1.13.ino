#include <BTS7960.h>
#include <Bluepad32.h>
#include <esp_task_wdt.h>

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

static const uint16_t BUTTON_SQUARE = 0x0008;
static const uint16_t BUTTON_R1     = 0x0020;
static const uint16_t BUTTON_L1     = 0x0010;

//Ramping config
#define RAMP_STEP 15  //incremento de velocidad por ciclo (170ms a vel maxima)

//Loop no bloqueante (optimización #1)
#define LOOP_INTERVAL_MS 10   //misma cadencia que el delay(10) original, ~100Hz
static uint32_t lastLoopMs = 0;

//PWM (optimización #3): frecuencia explícita para sacar el motor de la zona
//audible. OJO: la librería BTS7960 recibe vel como uint8_t (0-255), así que
//la RESOLUCIÓN sigue en 8 bits pase lo que pase acá — subirla de verdad
//requeriría bypassear la librería y manejar los pines PWM directo con LEDC,
//que es un cambio más grande y no lo metí sin poder probarlo contra tu
//hardware real. Esto solo sube la frecuencia portadora (elimina el zumbido).
//En tu core (esp32-bluepad32 4.1.0), analogWriteFrequency(freq) es GLOBAL
//—aplica a todos los canales PWM a la vez, no pin por pin.
#define PWM_FREQ_HZ 20000

//Watchdog del loop principal (optimización/robustez #2, ajustada):
//lo que hablamos era un timeout por "último dato recibido" para detectar si
//el stack Bluetooth queda diciendo isConnected()=true de forma falsa. Ese
//heurístico es frágil: si el piloto se queda quieto a propósito (jugada
//válida en Sumo), botones y ejes no cambian, y terminarías detectando un
//"desconectado" falso en pleno combate. En su lugar metí un watchdog de
//hardware real: si el loop() se cuelga más de WDT_TIMEOUT_S, la ESP32 se
//resetea sola en vez de quedar con los motores trabados en el último estado.
//Cubre el caso de firmware colgado sin falsos positivos por jugar quieto.
//En tu core (esp32-bluepad32 4.1.0) la firma es esp_task_wdt_init(segundos, panic).
#define WDT_TIMEOUT_S 2

//Curva expo del stick X (optimización #4): 0 = lineal puro, 1 = cúbica pura.
//Con 0.5 tenés más precisión cerca del centro sin perder el 100% al extremo.
#define EXPO_FACTOR 0.5f

//estado global de yarbis :v
int      currentSpeedPercent            = 50;  //50% o 100%. 50 por defecto
int      targetSpeedPercent             = 50;  //velocidad porcentual objetivo para ramping
int      currentRampVelY                = 0;   //velocidad actual con ramping en eje Y
bool     motorsStoppedDueToNoController = false;
uint16_t prevButtons[BP32_MAX_GAMEPADS] = {0}; //para detección de flanco

//auxiliares
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

//Optimización #2: mixing con normalización proporcional.
//Antes velA/velB se sumaban y se recortaban con constrain(), lo que a fondo
//con R2 (velY=255) anulaba cualquier corrección de rumbo con el stick. Ahora,
//si la suma se pasa de 255, se escalan los dos valores manteniendo la misma
//proporción entre ellos, así el giro sigue teniendo efecto real incluso
//empujando a máxima velocidad.
void mixAndDrive(int velY, int velX, bool spinInPlace, int actualSpeed) {
    int velA, velB;

    if (spinInPlace) {
        velA =  actualSpeed;
        velB = -actualSpeed;
    } else {
        velA = velY + velX;
        velB = velY - velX;

        int maxMag = max(abs(velA), abs(velB));
        if (maxMag > 255) {
            velA = (velA * 255) / maxMag;
            velB = (velB * 255) / maxMag;
        }
    }

    setMotorBTS(bts1, constrain(velA, -255, 255));
    setMotorBTS(bts2, constrain(velB, -255, 255));
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
            break;
        }
    }

    //Mejora #2 (existentes): si ya no queda ningún control conectado,
    //resetear el estado de rampa. Antes currentRampVelY podía quedar
    //"cargado" con el valor de antes del corte; el motor físico ya estaba
    //frenado por stopAllMotors(), pero el estado interno no coincidía, y al
    //reconectar la rampa arrancaba desde ese valor viejo en vez de 0.
    if (!anyControllerConnected()) {
        currentRampVelY     = 0;
        currentSpeedPercent = 50;
        targetSpeedPercent  = 50;
    }
}

//logica de control
void processGamepad(ControllerPtr ctl) {
    uint16_t btn          = ctl->buttons();
    int      idx          = findControllerIndex(ctl);
    if (idx < 0) idx      = 0;
    uint16_t newlyPressed = btn & (~prevButtons[idx]);

    //control de velociades. R1 sube 100 - L1 baja a 50
    if (newlyPressed & BUTTON_R1) targetSpeedPercent = 100;
    if (newlyPressed & BUTTON_L1) targetSpeedPercent = 50;

    prevButtons[idx] = btn;

    //Ramping de velocidad porcentual (cambio gradual de 50% a 100%)
    if (currentSpeedPercent < targetSpeedPercent) {
        currentSpeedPercent = min(currentSpeedPercent + RAMP_STEP/3, targetSpeedPercent);
    } else if (currentSpeedPercent > targetSpeedPercent) {
        currentSpeedPercent = max(currentSpeedPercent - RAMP_STEP/3, targetSpeedPercent);
    }

    int actualSpeed = (255 * currentSpeedPercent) / 100;

    //Mejora #1 (existentes): gatillos R2/L2 analógicos en vez de todo-o-nada.
    //throttle()/brake() de Bluepad32 devuelven 0..1023 según cuánto apretás
    //el gatillo — antes "btn & BUTTON_R2" solo daba 0% o 100%, perdías toda
    //la modulación manual de potencia que da un gatillo analógico real.
    int rawThrottle = ctl->throttle(); //R2 analógico, 0..1023
    int rawBrake    = ctl->brake();    //L2 analógico, 0..1023
    int targetVelY  = ((rawThrottle - rawBrake) * actualSpeed) / 1023;

    //Ramping de velocidad en eje Y (aceleración gradual)
    if (currentRampVelY < targetVelY) {
        currentRampVelY = min(currentRampVelY + RAMP_STEP, targetVelY);
    } else if (currentRampVelY > targetVelY) {
        currentRampVelY = max(currentRampVelY - RAMP_STEP, targetVelY);
    }

    int velY = currentRampVelY;

    //zona muerta joystick + curva expo (optimización #4)
    int16_t rawX = ctl->axisX();
    if (abs(rawX) < 20) rawX = 0;
    float normX  = constrain(rawX / 512.0f, -1.0f, 1.0f);
    float curved = EXPO_FACTOR * normX * normX * normX + (1.0f - EXPO_FACTOR) * normX;
    int velX     = (int)(curved * 255.0f); //sentido izq/derecha ya invertido como pediste

    //Mejora #3 (existentes): LED refleja el modo de velocidad activo en vez
    //de quedar fijo en rojo siempre.
    if (currentSpeedPercent >= 100) ctl->setColorLED(255, 0, 0);    //rojo   = 100%
    else                             ctl->setColorLED(255, 140, 0); //ámbar  = 50%

    mixAndDrive(velY, velX, (btn & BUTTON_SQUARE) != 0, actualSpeed);
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

    //Optimización #3: en tu core (esp32-bluepad32 4.1.0) analogWriteFrequency()
    //toma un solo argumento — es una frecuencia GLOBAL para todos los canales
    //PWM, no por pin. Una sola llamada alcanza para los 4 pines de los BTS7960.
    analogWriteFrequency(PWM_FREQ_HZ);

    //Watchdog del loop principal — tu core usa la firma vieja de esp_task_wdt
    //(timeout en segundos + flag de panic), no la de struct de core 3.x.
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

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

    //Optimización #1: loop no bloqueante. Antes delay(10) frenaba TODO el
    //loop —incluida la próxima llamada a BP32.update()— 10ms por vuelta pase
    //lo que pase. Ahora BP32.update() se llama en cada vuelta del loop() sin
    //esperar, y la lógica de control sigue corriendo a ~100Hz igual que
    //antes, pero sin bloquear el procesador mientras tanto.
    uint32_t now = millis();
    if (now - lastLoopMs >= LOOP_INTERVAL_MS) {
        lastLoopMs = now;

        if (anyControllerConnected()) {
            processControllers();
            motorsStoppedDueToNoController = false;
        } else if (!motorsStoppedDueToNoController) {
            stopAllMotors();
            motorsStoppedDueToNoController = true;
            Serial.println("Sin controlador: motores detenidos.");
        }
    }

    esp_task_wdt_reset();
}
