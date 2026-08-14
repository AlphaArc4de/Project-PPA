/*
 * ============================================================================
 *  PONTE LEVADIÇA AUTOMATIZADA — FIRMWARE ARDUINO NANO
 * ============================================================================
 *  Hardware: Arduino Nano (ATmega328P), 2x NEMA17 + driver DRV8825 (ou A4988/
 *            TMC2208/TMC2209 compatível), 1x HC-SR04, 2x fim de curso,
 *            1x botão de emergência.
 *
 *  Biblioteca externa necessária (instalar via Gerenciador de Bibliotecas
 *  do Arduino IDE): "AccelStepper" (por Mike McCauley).
 *
 *  Máquina de estados não bloqueante (uso de millis(), sem delay() na
 *  lógica principal).
 * ============================================================================
 */

// ============================================================================
// BIBLIOTECAS
// ============================================================================
#include <AccelStepper.h>

// ============================================================================
// CONFIGURAÇÕES E CONSTANTES AJUSTÁVEIS
// ============================================================================
// --- Sensor ---
const unsigned int DISTANCIA_DETECCAO_CM   = 30;    // limiar de detecção (cm) — CALIBRAR
const unsigned int LEITURAS_CONFIRMACAO    = 3;      // nº de leituras seguidas para confirmar detecção
const unsigned long INTERVALO_LEITURA_MS   = 60;     // intervalo entre leituras do HC-SR04 (não bloqueante)
const unsigned long ECHO_TIMEOUT_US        = 25000UL; // timeout do pulseIn (~25ms ~ 4m de alcance)

// --- Temporização de ciclo ---
const unsigned long TEMPO_PONTE_ABERTA_MS  = 30000UL; // 30 segundos (fixo, conforme especificação)
const unsigned long CHECAGEM_AREA_MS       = 1000UL;  // reavaliação da área livre durante espera em OPEN

// --- Motores (AccelStepper) ---
const long PASSOS_ABERTURA   = 320;   // CALIBRAR — nº de passos até fim de curso ABERTO (ver Seção 16 do relatório)
const long PASSOS_FECHAMENTO = 320;   // CALIBRAR — deve corresponder ao mesmo curso, sentido inverso
const float VELOCIDADE_MOTOR   = 400.0; // passos/s — CALIBRAR (Seção 16)
const float ACELERACAO_MOTOR   = 300.0; // passos/s^2 — CALIBRAR

// --- Segurança ---
const unsigned long TIMEOUT_MOVIMENTO_MS = 8000UL; // tempo máximo para completar abertura/fechamento

// ============================================================================
// DEFINIÇÃO DE PINOS
// ============================================================================
const uint8_t PIN_TRIG        = 2;
const uint8_t PIN_ECHO        = 3;

const uint8_t PIN_MOTOR_A_STEP = 4;
const uint8_t PIN_MOTOR_A_DIR  = 5;
const uint8_t PIN_MOTOR_B_STEP = 6;
const uint8_t PIN_MOTOR_B_DIR  = 7;
const uint8_t PIN_ENABLE       = 8;   // comum aos dois drivers (ativo em LOW)

const uint8_t PIN_FIM_CURSO_ABERTO  = 9;
const uint8_t PIN_FIM_CURSO_FECHADO = 10;
const uint8_t PIN_BOTAO_EMERGENCIA  = 11;

const uint8_t PIN_LED_STATUS = 12; // opcional

// ============================================================================
// ESTADOS DO SISTEMA
// ============================================================================
enum SystemState {
  ST_IDLE,
  ST_DETECTING,
  ST_OPENING,
  ST_OPEN,
  ST_CLOSING,
  ST_ERROR,
  ST_EMERGENCY_STOP
};

SystemState estadoAtual = ST_IDLE;

// ============================================================================
// OBJETOS GLOBAIS
// ============================================================================
// AccelStepper em modo DRIVER (2 pinos: STEP e DIR)
AccelStepper motorA(AccelStepper::DRIVER, PIN_MOTOR_A_STEP, PIN_MOTOR_A_DIR);
AccelStepper motorB(AccelStepper::DRIVER, PIN_MOTOR_B_STEP, PIN_MOTOR_B_DIR);

// ============================================================================
// VARIÁVEIS DE ESTADO
// ============================================================================
unsigned long ultimaLeituraSensorMs = 0;
unsigned int  contadorConfirmacao   = 0;

unsigned long marcoTempoAbertoMs    = 0;
unsigned long marcoInicioMovimentoMs = 0;
unsigned long ultimaChecagemAreaMs  = 0;

bool modoDiagnostico = false;

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  Serial.begin(9600);
  Serial.println(F("[SISTEMA] Inicializando..."));

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  pinMode(PIN_ENABLE, OUTPUT);
  digitalWrite(PIN_ENABLE, HIGH); // drivers desabilitados no boot (ativo em LOW)

  pinMode(PIN_FIM_CURSO_ABERTO, INPUT_PULLUP);
  pinMode(PIN_FIM_CURSO_FECHADO, INPUT_PULLUP);
  pinMode(PIN_BOTAO_EMERGENCIA, INPUT_PULLUP);

  pinMode(PIN_LED_STATUS, OUTPUT);
  digitalWrite(PIN_LED_STATUS, LOW);

  motorA.setMaxSpeed(VELOCIDADE_MOTOR);
  motorA.setAcceleration(ACELERACAO_MOTOR);
  motorB.setMaxSpeed(VELOCIDADE_MOTOR);
  motorB.setAcceleration(ACELERACAO_MOTOR);

  // Homing simplificado: assume-se que o sistema é ligado com a ponte
  // fisicamente fechada. Se o fim de curso FECHADO já estiver ativo,
  // zera a posição lógica dos motores nesse ponto.
  if (digitalRead(PIN_FIM_CURSO_FECHADO) == LOW) {
    motorA.setCurrentPosition(0);
    motorB.setCurrentPosition(0);
    Serial.println(F("[SISTEMA] Homing concluido / fim de curso FECHADO confirmado"));
  } else {
    Serial.println(F("[AVISO] Fim de curso FECHADO nao ativo no boot. Verifique a posicao fisica da ponte antes de operar."));
  }

  Serial.println(F("[SISTEMA] Pronto. Estado: IDLE"));
}

// ============================================================================
// LOOP
// ============================================================================
void loop() {
  lerComandoSerialDiagnostico();

  // Segurança tem prioridade absoluta, em qualquer estado.
  if (checarEmergencia()) {
    if (estadoAtual != ST_EMERGENCY_STOP) {
      transicaoPara(ST_EMERGENCY_STOP);
    }
  }

  switch (estadoAtual) {
    case ST_IDLE:            tratarIdle();            break;
    case ST_DETECTING:       tratarDetecting();       break;
    case ST_OPENING:         tratarOpening();         break;
    case ST_OPEN:            tratarOpen();            break;
    case ST_CLOSING:         tratarClosing();         break;
    case ST_ERROR:           tratarError();           break;
    case ST_EMERGENCY_STOP:  tratarEmergencyStop();   break;
  }
}

// ============================================================================
// FUNÇÕES DE ESTADO
// ============================================================================
void tratarIdle() {
  digitalWrite(PIN_LED_STATUS, LOW);
  long distancia = lerDistanciaFiltradaNaoBloqueante();
  if (distancia > 0 && (unsigned long)distancia < DISTANCIA_DETECCAO_CM) {
    contadorConfirmacao++;
    if (contadorConfirmacao >= LEITURAS_CONFIRMACAO) {
      transicaoPara(ST_DETECTING);
    }
  } else {
    contadorConfirmacao = 0;
  }
}

void tratarDetecting() {
  long distancia = lerDistanciaFiltradaNaoBloqueante();
  if (distancia > 0 && (unsigned long)distancia < DISTANCIA_DETECCAO_CM) {
    Serial.print(F("[SENSOR] Embarcacao detectada (dist="));
    Serial.print(distancia);
    Serial.println(F(" cm)"));
    iniciarAbertura();
    transicaoPara(ST_OPENING);
  } else {
    // Falso alarme: leitura não se confirmou.
    contadorConfirmacao = 0;
    transicaoPara(ST_IDLE);
  }
}

void tratarOpening() {
  digitalWrite(PIN_LED_STATUS, HIGH);
  motorA.run();
  motorB.run();

  bool finCursoAberto = (digitalRead(PIN_FIM_CURSO_ABERTO) == LOW);
  bool motoresChegaram = (motorA.distanceToGo() == 0) && (motorB.distanceToGo() == 0);

  if (finCursoAberto || motoresChegaram) {
    motorA.stop();
    motorB.stop();
    motoresPararEDesabilitar();
    Serial.println(F("[SISTEMA] Ponte totalmente aberta"));
    marcoTempoAbertoMs = millis();
    ultimaChecagemAreaMs = millis();
    Serial.println(F("[TIMER] 30 segundos iniciados"));
    transicaoPara(ST_OPEN);
    return;
  }

  if (millis() - marcoInicioMovimentoMs > TIMEOUT_MOVIMENTO_MS) {
    Serial.println(F("[ERRO] Timeout de movimento durante abertura"));
    motoresPararEDesabilitar();
    transicaoPara(ST_ERROR);
  }
}

void tratarOpen() {
  if (millis() - marcoTempoAbertoMs >= TEMPO_PONTE_ABERTA_MS) {
    if (millis() - ultimaChecagemAreaMs >= CHECAGEM_AREA_MS) {
      ultimaChecagemAreaMs = millis();
      long distancia = lerDistanciaFiltradaNaoBloqueante();
      bool areaLivre = !(distancia > 0 && (unsigned long)distancia < DISTANCIA_DETECCAO_CM);
      if (areaLivre) {
        Serial.println(F("[SISTEMA] Iniciando fechamento"));
        iniciarFechamento();
        transicaoPara(ST_CLOSING);
      } else {
        Serial.println(F("[SISTEMA] Area ainda ocupada, aguardando liberacao para fechar"));
      }
    }
  }
}

void tratarClosing() {
  motorA.run();
  motorB.run();

  bool finCursoFechado = (digitalRead(PIN_FIM_CURSO_FECHADO) == LOW);
  bool motoresChegaram = (motorA.distanceToGo() == 0) && (motorB.distanceToGo() == 0);

  if (finCursoFechado || motoresChegaram) {
    motorA.stop();
    motorB.stop();
    motorA.setCurrentPosition(0);
    motorB.setCurrentPosition(0);
    motoresPararEDesabilitar();
    Serial.println(F("[SISTEMA] Ponte fechada"));
    transicaoPara(ST_IDLE);
    return;
  }

  if (millis() - marcoInicioMovimentoMs > TIMEOUT_MOVIMENTO_MS) {
    Serial.println(F("[ERRO] Timeout de movimento durante fechamento"));
    motoresPararEDesabilitar();
    transicaoPara(ST_ERROR);
  }
}

void tratarError() {
  // Estado travado propositalmente. Requer comando serial "R" para reset manual.
  digitalWrite(PIN_LED_STATUS, (millis() / 250) % 2); // pisca LED como alerta
}

void tratarEmergencyStop() {
  motoresPararEDesabilitar();
  digitalWrite(PIN_LED_STATUS, (millis() / 100) % 2); // pisca rápido
  // Só sai deste estado com reset manual explícito (comando "R"),
  // e apenas se o botão de emergência já não estiver mais pressionado.
}

void transicaoPara(SystemState novoEstado) {
  estadoAtual = novoEstado;
}

// ============================================================================
// FUNÇÕES DE MOVIMENTO
// ============================================================================
void iniciarAbertura() {
  digitalWrite(PIN_ENABLE, LOW); // habilita drivers
  motorA.moveTo(PASSOS_ABERTURA);
  motorB.moveTo(PASSOS_ABERTURA);
  marcoInicioMovimentoMs = millis();
  Serial.println(F("[MOTOR] Abrindo ponte..."));
  Serial.println(F("[SISTEMA] Iniciando abertura"));
}

void iniciarFechamento() {
  digitalWrite(PIN_ENABLE, LOW);
  motorA.moveTo(motorA.currentPosition() - PASSOS_FECHAMENTO);
  motorB.moveTo(motorB.currentPosition() - PASSOS_FECHAMENTO);
  marcoInicioMovimentoMs = millis();
}

void motoresPararEDesabilitar() {
  motorA.stop();
  motorB.stop();
  digitalWrite(PIN_ENABLE, HIGH); // desabilita drivers (reduz aquecimento em repouso)
}

// ============================================================================
// FUNÇÕES DE SEGURANÇA
// ============================================================================
bool checarEmergencia() {
  return (digitalRead(PIN_BOTAO_EMERGENCIA) == LOW);
}

// ============================================================================
// FUNÇÕES DO SENSOR
// ============================================================================
// Leitura não bloqueante: só executa uma nova leitura física a cada
// INTERVALO_LEITURA_MS. pulseIn() ainda bloqueia por até ECHO_TIMEOUT_US
// (tipicamente poucos ms), o que é aceitável e não compromete a resposta
// do sistema a botões/fins de curso, que são lidos em cada iteração do loop.
long lerDistanciaFiltradaNaoBloqueante() {
  static long ultimaLeituraValida = -1;

  if (millis() - ultimaLeituraSensorMs < INTERVALO_LEITURA_MS) {
    return ultimaLeituraValida;
  }
  ultimaLeituraSensorMs = millis();

  long amostras[3];
  uint8_t validas = 0;
  for (uint8_t i = 0; i < 3; i++) {
    long d = medirDistanciaCm();
    if (d > 0) {
      amostras[validas++] = d;
    }
  }

  if (validas == 0) {
    // Leitura inválida (timeout de echo em todas as tentativas) —
    // não conta nem como presença nem como ausência.
    return -1;
  }

  // Ordenação simples (mediana) para as amostras válidas.
  for (uint8_t i = 0; i < validas; i++) {
    for (uint8_t j = i + 1; j < validas; j++) {
      if (amostras[j] < amostras[i]) {
        long tmp = amostras[i];
        amostras[i] = amostras[j];
        amostras[j] = tmp;
      }
    }
  }
  long mediana = amostras[validas / 2];
  ultimaLeituraValida = mediana;
  return mediana;
}

long medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  unsigned long duracao = pulseIn(PIN_ECHO, HIGH, ECHO_TIMEOUT_US);
  if (duracao == 0) {
    return -1; // timeout — leitura inválida (Seção "correção sobre sensores")
  }
  // Velocidade do som ~343 m/s -> ~0.0343 cm/us; ida e volta => /2
  long distanciaCm = (long)(duracao * 0.0343 / 2.0);
  return distanciaCm;
}

// ============================================================================
// FUNÇÕES DE DIAGNÓSTICO (Serial Monitor, 9600 baud)
// ============================================================================
void lerComandoSerialDiagnostico() {
  if (!Serial.available()) return;
  char c = Serial.read();

  switch (c) {
    case 'S': {
      long d = medirDistanciaCm();
      Serial.print(F("[DIAG] Distancia atual: "));
      Serial.print(d);
      Serial.println(F(" cm"));
      break;
    }
    case 'A': {
      digitalWrite(PIN_ENABLE, LOW);
      motorA.move(50);
      while (motorA.distanceToGo() != 0) motorA.run();
      digitalWrite(PIN_ENABLE, HIGH);
      Serial.println(F("[DIAG] Motor A avancou 50 passos"));
      break;
    }
    case 'B': {
      digitalWrite(PIN_ENABLE, LOW);
      motorB.move(50);
      while (motorB.distanceToGo() != 0) motorB.run();
      digitalWrite(PIN_ENABLE, HIGH);
      Serial.println(F("[DIAG] Motor B avancou 50 passos"));
      break;
    }
    case 'L': {
      Serial.print(F("[DIAG] FimCursoAberto="));
      Serial.print(digitalRead(PIN_FIM_CURSO_ABERTO) == LOW ? F("ATIVO") : F("livre"));
      Serial.print(F(" FimCursoFechado="));
      Serial.print(digitalRead(PIN_FIM_CURSO_FECHADO) == LOW ? F("ATIVO") : F("livre"));
      Serial.print(F(" Emergencia="));
      Serial.println(digitalRead(PIN_BOTAO_EMERGENCIA) == LOW ? F("ATIVA") : F("livre"));
      break;
    }
    case 'E': {
      Serial.print(F("[DIAG] Estado atual: "));
      Serial.println(nomeDoEstado(estadoAtual));
      break;
    }
    case 'R': {
      if (digitalRead(PIN_BOTAO_EMERGENCIA) == HIGH) {
        Serial.println(F("[DIAG] Reset manual solicitado. Retornando para IDLE."));
        contadorConfirmacao = 0;
        transicaoPara(ST_IDLE);
      } else {
        Serial.println(F("[DIAG] Reset negado: botao de emergencia ainda pressionado."));
      }
      break;
    }
    default:
      break;
  }
}

const __FlashStringHelper* nomeDoEstado(SystemState s) {
  switch (s) {
    case ST_IDLE:           return F("IDLE");
    case ST_DETECTING:      return F("DETECTING");
    case ST_OPENING:        return F("OPENING");
    case ST_OPEN:           return F("OPEN");
    case ST_CLOSING:        return F("CLOSING");
    case ST_ERROR:          return F("ERROR");
    case ST_EMERGENCY_STOP: return F("EMERGENCY_STOP");
    default:                return F("DESCONHECIDO");
  }
}
