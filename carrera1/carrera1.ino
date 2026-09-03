// ================================================================
// SEGUIDOR DE BORDE INTERNO (LÍNEA ROJA) - ARDUINO NANO
// ================================================================

// Pines Digitales de los 4 Sensores Sigue-Líneas
const int pinSig1 = 2; // D2 (Extremo Izquierdo)
const int pinSig2 = 3; // D3 (Medio Izquierdo)
const int pinSig3 = 4; // D4 (Medio Derecho)
const int pinSig4 = 5; // D5 (Extremo Derecho)

// Estados de Color
const int BLANCO = LOW;
const int NEGRO  = HIGH;

// Pines de Motores
const int M1_RPWM = 6;  // Motor Izquierdo (Avance)
const int M1_LPWM = 11; // Motor Izquierdo (Atras)
const int M2_RPWM = 9;  // Motor Derecho (Avance)
const int M2_LPWM = 10; // Motor Derecho (Atras)

// Velocidades
const int VEL_BASE = 80;
const int VEL_GIRO = 90;

// Estado del giro especial
bool girandoCurva = false;


void setup() {

  Serial.begin(9600);

  // Configuración Sensores
  pinMode(pinSig1, INPUT);
  pinMode(pinSig2, INPUT);
  pinMode(pinSig3, INPUT);
  pinMode(pinSig4, INPUT);

  // Configuración Salidas de Motores
  pinMode(M1_RPWM, OUTPUT);
  pinMode(M1_LPWM, OUTPUT);
  pinMode(M2_RPWM, OUTPUT);
  pinMode(M2_LPWM, OUTPUT);

  pararMotores();

  Serial.println("--- Seguidor de Borde Interno Inicializado ---");
}


void loop() {

  // Lectura de los 4 Sensores
  int L1 = digitalRead(pinSig1); // Extremo Izq
  int L2 = digitalRead(pinSig2); // Medio Izq
  int L3 = digitalRead(pinSig3); // Medio Der
  int L4 = digitalRead(pinSig4); // Extremo Der


  // Diagnóstico Serial
  Serial.print("SL: [ ");
  Serial.print(L1 == NEGRO ? "N " : "B ");
  Serial.print(L2 == NEGRO ? "N " : "B ");
  Serial.print(L3 == NEGRO ? "N " : "B ");
  Serial.print(L4 == NEGRO ? "N " : "B ");
  Serial.print("] -> ");


  // ================================================================
  // DETECCIÓN DE LA CURVA
  // ================================================================
  //
  // Combinación:
  // [ B, N, B, B ]
  //
  // Esto indica que el robot llegó a la curva cerrada.
  //

  if (!girandoCurva &&
      L1 == BLANCO &&
      L2 == NEGRO &&
      L3 == BLANCO &&
      L4 == BLANCO) {

    girandoCurva = true;

    Serial.println("CURVA DETECTADA -> INICIANDO GIRO");
  }


  // ================================================================
  // GIRO SOBRE EL PROPIO EJE
  // ================================================================

  if (girandoCurva) {

    // Cuando volvemos a encontrar:
    // [ N, N, B, B ]
    //
    // significa que el robot volvió a quedar
    // correctamente ubicado sobre el borde.

    if (L1 == NEGRO &&
        L2 == NEGRO &&
        L3 == BLANCO &&
        L4 == BLANCO) {

      girandoCurva = false;

      motor1Adelante(VEL_BASE);
      motor2Adelante(VEL_BASE);

      Serial.println("CURVA TERMINADA -> RECTO");

    }

    else {

      // ============================================================
      // GIRO SOBRE EL PROPIO EJE
      // ============================================================
      //
      // Motor izquierdo: ATRÁS
      // Motor derecho:   ADELANTE
      //
      // Esto hace que el robot gire prácticamente sobre su centro.
      //

      motor1Atras(VEL_GIRO);
      motor2Adelante(VEL_GIRO);

      Serial.println("GIRANDO SOBRE EL EJE");
    }
  }


  // ================================================================
  // LÓGICA NORMAL DE BORDE INTERNO
  // ================================================================

  else {

    // CASO 1: Centrado Perfecto en la frontera [ N, N, B, B ]
    if (L1 == NEGRO &&
        L2 == NEGRO &&
        L3 == BLANCO &&
        L4 == BLANCO) {

      motor1Adelante(VEL_BASE);
      motor2Adelante(VEL_BASE);

      Serial.println("CENTRADO EN BORDE -> RECTO");
    }


    // CASO 2: Ligera desviación hacia la calle blanca [ N, B, B, B ]
    else if (L1 == NEGRO &&
             L2 == BLANCO &&
             L3 == BLANCO &&
             L4 == BLANCO) {

      motor1Atras(VEL_GIRO / 2);
      motor2Adelante(VEL_GIRO);

      Serial.println("CORRECCIÓN SUAVE -> IZQUIERDA");
    }


    // CASO 3: Totalmente en la calle blanca [ B, B, B, B ]
    else if (L1 == BLANCO &&
             L2 == BLANCO &&
             L3 == BLANCO &&
             L4 == BLANCO) {

      motor1Atras(VEL_BASE);
      motor2Adelante(VEL_GIRO);

      Serial.println("CORRECCIÓN FUERTE -> IZQUIERDA");
    }


    // CASO 4: Ligera desviación hacia el centro negro [ N, N, N, B ]
    else if (L1 == NEGRO &&
             L2 == NEGRO &&
             L3 == NEGRO &&
             L4 == BLANCO) {

      motor1Adelante(VEL_GIRO);
      motor2Adelante(VEL_BASE / 2);

      Serial.println("CORRECCIÓN SUAVE -> DERECHA");
    }


    // CASO 5: Totalmente en el centro negro [ N, N, N, N ]
    else if (L1 == NEGRO &&
             L2 == NEGRO &&
             L3 == NEGRO &&
             L4 == NEGRO) {

      motor1Adelante(VEL_GIRO);
      motor2Atras(VEL_BASE / 2);

      Serial.println("CORRECCIÓN FUERTE -> DERECHA");
    }


    // CASO EXTRA: Cualquier otra combinación intermedia
    else {

      motor1Adelante(VEL_BASE - 20);
      motor2Adelante(VEL_BASE - 20);

      Serial.println("MANTENER TRAYECTORIA");
    }
  }

  delay(15);
}


// ================================================================
// FUNCIONES AUXILIARES DE CONTROL DE MOTORES
// ================================================================

void motor1Adelante(int velocidad) {
  analogWrite(M1_RPWM, velocidad);
  analogWrite(M1_LPWM, 0);
}

void motor1Atras(int velocidad) {
  analogWrite(M1_RPWM, 0);
  analogWrite(M1_LPWM, velocidad);
}

void motor2Adelante(int velocidad) {
  analogWrite(M2_RPWM, velocidad);
  analogWrite(M2_LPWM, 0);
}

void motor2Atras(int velocidad) {
  analogWrite(M2_RPWM, 0);
  analogWrite(M2_LPWM, velocidad);
}

void pararMotores() {
  analogWrite(M1_RPWM, 0);
  analogWrite(M1_LPWM, 0);
  analogWrite(M2_RPWM, 0);
  analogWrite(M2_LPWM, 0);
}
