#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <math.h>

WiFiMulti wifiMulti;
WiFiUDP udp;

// ==============================================================================
// INOVA TECH — SMART DRIVER (FIRMWARE ESP32 DUAL-CORE DE ALTA PRECISÃO)
// ==============================================================================
// Monitoramento biométrico de postura e combate à fadiga / sono ao volante.
// Arquitetura Dual-Core (FreeRTOS):
// - CORE 1: Sensor IR, Acelerômetro GY-521 (MPU-6050), Buzzer e Relé Duplo HL-52S.
// - CORE 0: Conectividade Wi-Fi e Telemetria IoT (Local via UDP + Nuvem via HTTPS).
//
// Protocolo de rede (zero configuração de IP):
// 1. A ESP32 envia o JSON de telemetria por UDP broadcast na porta 3002.
// 2. O servidor Node.js responde por UDP com {"srv":"INOVATECH","cmds":[...]}.
// 3. A ESP32 aprende o IP do servidor pela resposta e passa a enviar direto (unicast).
// 4. Sem servidor local por 5s, envia por HTTPS para a nuvem Render (fallback).
// Os comandos da Central (TEST_ALARM, SILENCIAR, CALIBRAR...) chegam nessas respostas.
// ==============================================================================

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO DE REDES WI-FI (MULTI-WIFI AUTOMÁTICO) & SERVIDOR
// ------------------------------------------------------------------------------
// 1. Rede Residencial:
const char* WIFI_CASA_SSID = "GARCIA 7G";
const char* WIFI_CASA_PASS = "MMarcos12@";

// 2. Roteador Redmi Note 15 5G:
const char* WIFI_REDMI_SSID = "REDMI Note 15 5G";
const char* WIFI_REDMI_PASS = "Garcia2026";

// 3. iPhone de contingência:
const char* WIFI_IPHONE_SSID = "iPhone de Matheus";
const char* WIFI_IPHONE_PASS = "12345678910";

// Servidor na nuvem (usado somente quando nenhum servidor local responde):
const char* BACKEND_URL_CLOUD = "https://inovatech-tcc.onrender.com/api/telemetry";
const bool  ENVIAR_NUVEM_SEM_SERVIDOR_LOCAL = true;

const uint16_t UDP_PORTA_SERVIDOR = 3002; // Porta onde o server.js escuta
const uint16_t UDP_PORTA_LOCAL    = 3003; // Porta onde a ESP32 recebe as respostas

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DOS PINOS DE HARDWARE
// ------------------------------------------------------------------------------
const uint8_t PINO_SENSOR_IR = 32; // GPIO 32 - Sensor IR de obstáculo
const uint8_t PINO_BUZZER    = 25; // GPIO 25 - Buzzer sonoro ativo
const uint8_t PINO_RELE_1    = 18; // GPIO 18 - Relé Safe-Stop (Tração)
const uint8_t PINO_RELE_2    = 19; // GPIO 19 - Relé Pisca-Alerta Veicular
const uint8_t PINO_RELE_GND  = 13; // GPIO 13 (D13) - GND Virtual Relé
const uint8_t PINO_LED_REDE  = 2;  // GPIO 2 - LED azul da placa (status da conexão sem cabo)

// Acelerômetro GY-521 (I2C + Alimentação Virtual)
const uint8_t PINO_MPU_SDA   = 21; // GPIO 21 - Linha SDA I2C
const uint8_t PINO_MPU_SCL   = 22; // GPIO 22 - Linha SCL I2C
const uint8_t PINO_MPU_VCC   = 23; // GPIO 23 - VCC Virtual (3.3V HIGH)
const uint8_t PINO_MPU_GND   = 16; // GPIO 16 (RX2) - GND Virtual (0V LOW)

// Endereço I2C padrão do MPU-6050
const uint8_t MPU6050_ADDR   = 0x68;

// Níveis Lógicos
const int NIVEL_CABECA_BAIXA     = LOW;
const int NIVEL_BUZZER_LIGADO    = HIGH;
const int NIVEL_BUZZER_DESLIGADO = LOW;
const int NIVEL_RELE_LIGADO      = LOW;  // HL-52S é Active LOW
const int NIVEL_RELE_DESLIGADO   = HIGH;

// ------------------------------------------------------------------------------
// 3. CALIBRAÇÃO TEMPORAL E ANGULAR
// ------------------------------------------------------------------------------
// Filtro Temporal de Segurança: 3.5 segundos de tolerância com 3 bips progressivos
const unsigned long LIMIAR_CABECA_BAIXA_MS = 3500;

// Acelerômetro: ângulo medido em relação à postura calibrada (cabeça erguida).
// Funciona em qualquer orientação de montagem do GY-521 na haste dos óculos.
const float LIMIAR_INCLINACAO_GRAUS = 32.0;  // Cabeça caída além disso = cabeça baixa
const float PESCADA_VARIACAO_GRAUS  = 15.0;  // Queda em 100 ms (~150°/s) = pescada brusca
const float PESCADA_INCLINACAO_MIN  = 25.0;  // ...desde que termine a pelo menos 25°
const bool  PESCADA_DISPARA_ALARME  = true;  // Pescada dispara o alarme na hora
const unsigned long ALARME_MINIMO_PESCADA_MS = 2000; // Alarme de pescada dura no mínimo 2s

// Sensor IR = reserva do acelerômetro. Só é confiável se leu "cabeça erguida" no último
// minuto: sem alimentação ele lê LOW direto e seguraria o alarme ligado para sempre.
const unsigned long IR_SINAL_VALIDO_MS = 60000;

const unsigned long DURACAO_MAX_TESTE_MS     = 30000; // Teste remoto se desliga sozinho em 30s
const unsigned long INTERVALO_TELEMETRIA_MS  = 200;   // 5 pacotes/s em operação normal
const unsigned long INTERVALO_TELEMETRIA_ALERTA_MS = 100; // 10 pacotes/s durante alertas
const unsigned long TIMEOUT_SERVIDOR_LOCAL_MS = 5000;
const unsigned long INTERVALO_NUVEM_MS        = 1000;

// ------------------------------------------------------------------------------
// 4. ESTRUTURA DE TELEMETRIA COMPARTILHADA ENTRE OS CORES
// ------------------------------------------------------------------------------
struct DadosTelemetria {
  int sensorIr;
  unsigned long tempoCabecaBaixaMs;
  int buzzer;
  int rele1;
  int rele2;
  float pitch;
  float roll;
  float inclinacao;
  bool pescadaDetectada;
  bool mpuOnline;
  bool mpuCalibrado;
  bool testeAtivo;
  char estado[24];
  bool alarmeAtivo;
  bool irConfiavel;
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, 0, 0.0, 0.0, 0.0, false, false, false, false, "NORMAL", false, false};
portMUX_TYPE mutexDados = portMUX_INITIALIZER_UNLOCKED;

// Comandos recebidos pela rede (Core 0) e executados no Core 1
const uint8_t CMD_TESTE_ALARME = 0x01;
const uint8_t CMD_SILENCIAR    = 0x02;
const uint8_t CMD_TOGGLE_RELE1 = 0x04;
const uint8_t CMD_TOGGLE_RELE2 = 0x08;
const uint8_t CMD_CALIBRAR     = 0x10;
volatile uint8_t comandosRemotos = 0;
portMUX_TYPE mutexComandos = portMUX_INITIALIZER_UNLOCKED;

// Status da rede (escrito pelo Core 0, exibido no LED pelo Core 1):
// pisca rápido = sem comunicação · pisca devagar = via nuvem (Render) · aceso = servidor do PC
const uint8_t REDE_SEM_COMUNICACAO = 0;
const uint8_t REDE_NUVEM           = 1;
const uint8_t REDE_SERVIDOR_LOCAL  = 2;
volatile uint8_t statusRede = REDE_SEM_COMUNICACAO;

// Variáveis de Estado no Core 1
unsigned long tempoInicioCabecaBaixa = 0;
bool cabecaEstavaBaixa               = false;
bool alarmeAtivo                     = false;
unsigned long alarmeMinimoAte        = 0;
bool testeAlarmeAtivo                = false;
unsigned long inicioTesteAlarme      = 0;
bool testeManualRele1                = false;
bool testeManualRele2                = false;
unsigned long ultimoIrLivreMs        = 0; // Última vez que o IR leu "cabeça erguida"

// Acelerômetro
struct LeituraMPU {
  bool valida;
  float pitch;
  float roll;
  float inclinacao;
  bool pescada;
};

bool mpuDetectado             = false;
bool mpuCalibrado             = false;
unsigned long ultimaTentativaMpu = 0;
float gFiltX = 0, gFiltY = 0, gFiltZ = 1;  // Vetor gravidade filtrado (em g)
float gRefX = 0, gRefY = 0, gRefZ = 1;     // Vetor de referência (cabeça erguida)
float pitchRef = 0, rollRef = 0;
float historicoInclinacao[10] = {0};       // 10 amostras x 10 ms = janela de 100 ms
uint8_t indiceHistorico = 0;

// Protótipos de Funções
void ativarAlarmes();
void desativarAlarmes();
void processarComandosSerial();
void aplicarComandosRemotos();
void agendarComando(uint8_t cmd);
void interpretarComandosTexto(const char* texto);
void taskTelemetria(void *pvParameters);
bool inicializarMPU6050();
bool lerAcelerometroBruto(float &ax, float &ay, float &az);
void calibrarMPU6050();
LeituraMPU lerMPU6050(unsigned long agora);

// ------------------------------------------------------------------------------
// 5. SETUP INICIAL
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Configuração dos pinos com pull-up interno no sensor
  pinMode(PINO_SENSOR_IR, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_RELE_1, OUTPUT);
  pinMode(PINO_RELE_2, OUTPUT);
  pinMode(PINO_LED_REDE, OUTPUT);

  // GND Virtual (0V) no Pino D13 para o terra do Módulo Relé
  pinMode(PINO_RELE_GND, OUTPUT);
  digitalWrite(PINO_RELE_GND, LOW);

  // Alimentação Virtual do Acelerômetro GY-521:
  // VCC Virtual no D23 (3.3V)
  pinMode(PINO_MPU_VCC, OUTPUT);
  digitalWrite(PINO_MPU_VCC, HIGH);

  // GND Virtual no RX2 / GPIO 16 (0V)
  pinMode(PINO_MPU_GND, OUTPUT);
  digitalWrite(PINO_MPU_GND, LOW);

  desativarAlarmes();

  // Aguarda 150ms para o sensor GY-521 estabilizar a alimentação
  delay(150);

  // Inicializa I2C nos pinos dedicados D21 (SDA) e D22 (SCL)
  Wire.begin(PINO_MPU_SDA, PINO_MPU_SCL, 400000);
  Wire.setTimeOut(20); // Nunca deixa um fio solto travar o Core 1
  if (inicializarMPU6050()) {
    calibrarMPU6050();
  }

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("   Monitoramento de Postura e Fadiga Veicular     "));
  Serial.println(F("=================================================="));
  Serial.println(F("[HARDWARE] Sensor IR: GPIO 32 | Buzzer: GPIO 25"));
  Serial.println(F("[HARDWARE] Rele 1: GPIO 18 | Rele 2: GPIO 19 | GND Rele: GPIO 13"));
  Serial.printf("[HARDWARE] Acelerometro GY-521: SDA=%d, SCL=%d, VCC=%d, GND=%d (RX2)\n",
                PINO_MPU_SDA, PINO_MPU_SCL, PINO_MPU_VCC, PINO_MPU_GND);
  Serial.printf("[HARDWARE] Status MPU-6050: %s\n", mpuDetectado ? "CONECTADO E CALIBRADO" : "AGUARDANDO CONEXAO");
  Serial.printf("[SEGURANCA] Tolerancia: %lu ms | Inclinacao maxima: %.0f graus\n",
                LIMIAR_CABECA_BAIXA_MS, LIMIAR_INCLINACAO_GRAUS);
  Serial.println(F("[DICA] Ligue (ou envie CMD:CALIBRAR) com a cabeca erguida para calibrar o acelerometro."));

  // Modo Station puro com reconexão resiliente
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false); // Evita gravar na flash a cada reconexão
  // Sem economia de energia no rádio: comandos chegam sem atraso e o consumo constante
  // impede que o power bank se desligue sozinho por "carga baixa"
  WiFi.setSleep(false);

  // Registra as redes conhecidas (o WiFiMulti escolhe a de melhor sinal)
  wifiMulti.addAP(WIFI_CASA_SSID, WIFI_CASA_PASS);
  wifiMulti.addAP(WIFI_REDMI_SSID, WIFI_REDMI_PASS);
  wifiMulti.addAP(WIFI_IPHONE_SSID, WIFI_IPHONE_PASS);

  // Inicializa a Tarefa de Telemetria no Core 0 (Background)
  xTaskCreatePinnedToCore(
    taskTelemetria,
    "TaskTelemetria",
    12288,
    NULL,
    1,
    NULL,
    0
  );
}

// ------------------------------------------------------------------------------
// 6. INICIALIZAÇÃO, CALIBRAÇÃO E LEITURA DO ACELERÔMETRO MPU-6050 (I2C)
// ------------------------------------------------------------------------------
bool inicializarMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  if (Wire.endTransmission() != 0) {
    mpuDetectado = false;
    return false;
  }

  // Acorda o MPU-6050 (limpa o registrador PWR_MGMT_1)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  // Filtro digital passa-baixas interno de 44 Hz (reduz vibração do caminhão)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1A); // CONFIG
  Wire.write(0x03);
  Wire.endTransmission(true);

  // Fundo de escala do acelerômetro (+/- 2g)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C); // ACCEL_CONFIG
  Wire.write(0x00);
  Wire.endTransmission(true);

  mpuDetectado = true;
  return true;
}

bool lerAcelerometroBruto(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B); // Registrador ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)6, true) != 6) return false;

  int16_t rx = (Wire.read() << 8) | Wire.read();
  int16_t ry = (Wire.read() << 8) | Wire.read();
  int16_t rz = (Wire.read() << 8) | Wire.read();
  ax = rx / 16384.0;
  ay = ry / 16384.0;
  az = rz / 16384.0;
  return true;
}

// Grava a postura atual (cabeça erguida) como referência zero. Bloqueia ~0,5s.
void calibrarMPU6050() {
  float somaX = 0, somaY = 0, somaZ = 0;
  int amostras = 0;
  for (int i = 0; i < 50; i++) {
    float ax, ay, az;
    if (lerAcelerometroBruto(ax, ay, az)) {
      somaX += ax; somaY += ay; somaZ += az;
      amostras++;
    }
    delay(10);
  }
  float mx = somaX / max(amostras, 1);
  float my = somaY / max(amostras, 1);
  float mz = somaZ / max(amostras, 1);
  float modulo = sqrt(mx * mx + my * my + mz * mz);

  // Gravidade medida tem que dar ~1g; fora disso o módulo está com defeito/solto
  if (amostras < 40 || modulo < 0.6 || modulo > 1.4) {
    mpuCalibrado = false;
    Serial.println(F("[MPU-6050] Falha na calibracao (leitura instavel). Verifique os fios."));
    return;
  }

  gRefX = mx / modulo; gRefY = my / modulo; gRefZ = mz / modulo;
  gFiltX = gRefX; gFiltY = gRefY; gFiltZ = gRefZ;
  pitchRef = atan2(-gRefX, sqrt(gRefY * gRefY + gRefZ * gRefZ)) * RAD_TO_DEG;
  rollRef  = atan2(gRefY, gRefZ) * RAD_TO_DEG;
  for (uint8_t i = 0; i < 10; i++) historicoInclinacao[i] = 0;
  mpuCalibrado = true;
  Serial.println(F("[MPU-6050] Calibrado: postura atual registrada como cabeca erguida."));
}

LeituraMPU lerMPU6050(unsigned long agora) {
  LeituraMPU r = {false, 0.0, 0.0, 0.0, false};

  if (!mpuDetectado) {
    // Tenta reconectar no máximo 1x por segundo (não sobrecarrega o barramento I2C)
    if (agora - ultimaTentativaMpu < 1000) return r;
    ultimaTentativaMpu = agora;
    if (!inicializarMPU6050()) return r;
    Serial.println(F("[MPU-6050] Modulo detectado. Calibrando..."));
    calibrarMPU6050();
  }

  float ax, ay, az;
  if (!lerAcelerometroBruto(ax, ay, az)) {
    mpuDetectado = false;
    ultimaTentativaMpu = agora;
    Serial.println(F("[MPU-6050] Leitura falhou (modulo desconectado?)."));
    return r;
  }

  // Filtro passa-baixas leve (70% anterior + 30% atual): suave sem atrasar a pescada
  gFiltX = gFiltX * 0.7 + ax * 0.3;
  gFiltY = gFiltY * 0.7 + ay * 0.3;
  gFiltZ = gFiltZ * 0.7 + az * 0.3;

  float modulo = sqrt(gFiltX * gFiltX + gFiltY * gFiltY + gFiltZ * gFiltZ);
  if (modulo < 0.3 || !mpuCalibrado) return r;

  float nx = gFiltX / modulo, ny = gFiltY / modulo, nz = gFiltZ / modulo;

  // Ângulo entre a gravidade atual e a de referência = quanto a cabeça saiu da postura
  float cosAng = nx * gRefX + ny * gRefY + nz * gRefZ;
  cosAng = constrain(cosAng, -1.0f, 1.0f);
  r.inclinacao = acos(cosAng) * RAD_TO_DEG;

  // Pitch e Roll relativos à postura calibrada (somente para exibição na Central)
  r.pitch = atan2(-nx, sqrt(ny * ny + nz * nz)) * RAD_TO_DEG - pitchRef;
  r.roll  = atan2(ny, nz) * RAD_TO_DEG - rollRef;

  // Detecção de "Pescada" de Sono: queda angular brusca dentro de 100 ms
  float inclinacao100msAtras = historicoInclinacao[indiceHistorico];
  historicoInclinacao[indiceHistorico] = r.inclinacao;
  indiceHistorico = (indiceHistorico + 1) % 10;
  r.pescada = (r.inclinacao - inclinacao100msAtras > PESCADA_VARIACAO_GRAUS) &&
              (r.inclinacao > PESCADA_INCLINACAO_MIN);

  r.valida = true;
  return r;
}

// ------------------------------------------------------------------------------
// 7. LOOP PRINCIPAL (CORE 1 — 100 HZ / TEMPO REAL ESTRITO)
// ------------------------------------------------------------------------------
void loop() {
  processarComandosSerial();
  aplicarComandosRemotos();

  // Leitura digital do Sensor Infravermelho
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  unsigned long agora = millis();
  unsigned long tempoDecorridoMs = 0;
  bool bipPreAlertaAtivo = false;
  const char* estadoAtualTexto = "NORMAL";

  // Leitura do Acelerômetro GY-521
  LeituraMPU mpu = lerMPU6050(agora);
  bool inclinadoDemais = mpu.valida && (mpu.inclinacao > LIMIAR_INCLINACAO_GRAUS);
  bool pescadaSono = mpu.valida && mpu.pescada;

  // Sensor IR só conta na falta do acelerômetro, e se estiver vivo
  if (estadoLeitura != NIVEL_CABECA_BAIXA) ultimoIrLivreMs = agora;
  bool irConfiavel = ultimoIrLivreMs != 0 && agora - ultimoIrLivreMs < IR_SINAL_VALIDO_MS;
  bool irCabecaBaixa = !mpu.valida && irConfiavel && estadoLeitura == NIVEL_CABECA_BAIXA;

  // Condição de Cabeça Baixa: acelerômetro passa do ângulo OU pescada (OU IR, como reserva)
  bool condicaoCabecaBaixa = inclinadoDemais || pescadaSono || irCabecaBaixa;

  // Teste remoto da Central expira sozinho por segurança
  if (testeAlarmeAtivo && (agora - inicioTesteAlarme >= DURACAO_MAX_TESTE_MS)) {
    testeAlarmeAtivo = false;
    Serial.println(F("[TESTE] Tempo maximo do teste remoto atingido. Encerrando."));
  }

  if (condicaoCabecaBaixa) {
    if (!cabecaEstavaBaixa) {
      cabecaEstavaBaixa = true;
      tempoInicioCabecaBaixa = agora;
    }

    tempoDecorridoMs = agora - tempoInicioCabecaBaixa;

    if (!alarmeAtivo) {
      bool disparoPescada = PESCADA_DISPARA_ALARME && pescadaSono;
      if (tempoDecorridoMs >= LIMIAR_CABECA_BAIXA_MS || disparoPescada) {
        alarmeAtivo = true;
        if (disparoPescada) {
          alarmeMinimoAte = agora + ALARME_MINIMO_PESCADA_MS;
          Serial.printf(">>> PESCADA DE SONO DETECTADA (%.0f graus)! Alarme imediato.\n", mpu.inclinacao);
        } else {
          Serial.println(F(">>> ALERTA CRITICO: Fadiga confirmada! Buzzer continuo e Reles ativados!"));
        }
      } else {
        // 3 BIPS PROGRESSIVOS ALONGADOS (350 ms cada):
        // 1º Bip: 700 a 1050 ms | 2º Bip: 1700 a 2050 ms | 3º Bip: 2700 a 3050 ms
        // Aos 3.5s (3500ms): Alarme contínuo ininterrupto + relés!
        bipPreAlertaAtivo = (tempoDecorridoMs >= 700 && tempoDecorridoMs < 1050) ||
                            (tempoDecorridoMs >= 1700 && tempoDecorridoMs < 2050) ||
                            (tempoDecorridoMs >= 2700 && tempoDecorridoMs < 3050);
      }
    }
  } else if (cabecaEstavaBaixa) {
    // Cabeça erguida: encerra o alarme (respeitando o tempo mínimo da pescada)
    if (!alarmeAtivo || agora >= alarmeMinimoAte) {
      if (alarmeAtivo) Serial.println(F("Postura Normalizada: Cabeca Erguida (Alarmes cessados)."));
      alarmeAtivo = false;
      cabecaEstavaBaixa = false;
      tempoInicioCabecaBaixa = 0;
    } else {
      tempoDecorridoMs = agora - tempoInicioCabecaBaixa;
    }
  }

  bool alarmeGeral = alarmeAtivo || testeAlarmeAtivo;
  if (alarmeGeral) {
    estadoAtualTexto = "ALARME_ATIVADO";
    ativarAlarmes();
  } else {
    if (cabecaEstavaBaixa) estadoAtualTexto = "PRE_ALERTA";
    desativarAlarmes();
    digitalWrite(PINO_BUZZER, bipPreAlertaAtivo ? NIVEL_BUZZER_LIGADO : NIVEL_BUZZER_DESLIGADO);
  }

  // Atualiza os dados compartilhados com o Core 0 de forma atômica
  portENTER_CRITICAL(&mutexDados);
  dadosGlobais.sensorIr = estadoLeitura;
  dadosGlobais.tempoCabecaBaixaMs = tempoDecorridoMs;
  dadosGlobais.buzzer = (alarmeGeral || bipPreAlertaAtivo) ? 1 : 0;
  dadosGlobais.rele1 = (alarmeGeral || testeManualRele1) ? 1 : 0;
  dadosGlobais.rele2 = (alarmeGeral || testeManualRele2) ? 1 : 0;
  dadosGlobais.pitch = mpu.pitch;
  dadosGlobais.roll = mpu.roll;
  dadosGlobais.inclinacao = mpu.inclinacao;
  // Mantém a pescada visível por um pacote inteiro de telemetria
  if (pescadaSono) dadosGlobais.pescadaDetectada = true;
  else if (!alarmeAtivo) dadosGlobais.pescadaDetectada = false;
  dadosGlobais.mpuOnline = mpuDetectado;
  dadosGlobais.mpuCalibrado = mpuCalibrado;
  dadosGlobais.testeAtivo = testeAlarmeAtivo;
  dadosGlobais.alarmeAtivo = alarmeGeral;
  dadosGlobais.irConfiavel = irConfiavel;
  strncpy(dadosGlobais.estado, estadoAtualTexto, sizeof(dadosGlobais.estado) - 1);
  portEXIT_CRITICAL(&mutexDados);

  // LED azul: mostra a conexão mesmo sem o cabo USB (power bank)
  bool ledAceso = statusRede == REDE_SERVIDOR_LOCAL ? true
                : statusRede == REDE_NUVEM ? (agora % 1000 < 500)
                : (agora % 200 < 100);
  digitalWrite(PINO_LED_REDE, ledAceso ? HIGH : LOW);

  delay(10);
}

// ------------------------------------------------------------------------------
// 8. PROCESSAMENTO DE COMANDOS (SERIAL USB + REDE)
// ------------------------------------------------------------------------------
void agendarComando(uint8_t cmd) {
  portENTER_CRITICAL(&mutexComandos);
  comandosRemotos |= cmd;
  portEXIT_CRITICAL(&mutexComandos);
}

// Procura nomes de comandos em qualquer texto (resposta UDP, HTTP ou Serial)
void interpretarComandosTexto(const char* texto) {
  if (strstr(texto, "TEST_ALARM"))   agendarComando(CMD_TESTE_ALARME);
  if (strstr(texto, "SILENCIAR") || strstr(texto, "RESET_ALARM")) agendarComando(CMD_SILENCIAR);
  if (strstr(texto, "TOGGLE_RELE1")) agendarComando(CMD_TOGGLE_RELE1);
  if (strstr(texto, "TOGGLE_RELE2")) agendarComando(CMD_TOGGLE_RELE2);
  if (strstr(texto, "CALIBRAR"))     agendarComando(CMD_CALIBRAR);
}

void processarComandosSerial() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) interpretarComandosTexto(cmd.c_str());
  }
}

// Executa no Core 1 os comandos agendados pelo Core 0 / Serial
void aplicarComandosRemotos() {
  portENTER_CRITICAL(&mutexComandos);
  uint8_t cmds = comandosRemotos;
  comandosRemotos = 0;
  portEXIT_CRITICAL(&mutexComandos);
  if (!cmds) return;

  if (cmds & CMD_SILENCIAR) {
    testeAlarmeAtivo = false;
    testeManualRele1 = false;
    testeManualRele2 = false;
    alarmeMinimoAte = 0;
    Serial.println(F("[COMANDO] Alarmes silenciados pela Central."));
  }
  if (cmds & CMD_TESTE_ALARME) {
    testeAlarmeAtivo = true;
    inicioTesteAlarme = millis();
    Serial.println(F("[COMANDO] Teste remoto: Buzzer + Reles 1 e 2 ligados (ate SILENCIAR ou 30s)."));
  }
  if (cmds & CMD_TOGGLE_RELE1) {
    testeManualRele1 = !testeManualRele1;
    Serial.println(testeManualRele1 ? F("[RELE 1] Safe-Stop LIGADO") : F("[RELE 1] Safe-Stop DESLIGADO"));
  }
  if (cmds & CMD_TOGGLE_RELE2) {
    testeManualRele2 = !testeManualRele2;
    Serial.println(testeManualRele2 ? F("[RELE 2] Pisca-Alerta LIGADO") : F("[RELE 2] Pisca-Alerta DESLIGADO"));
  }
  if (cmds & CMD_CALIBRAR) {
    if (mpuDetectado) {
      Serial.println(F("[COMANDO] Recalibrando acelerometro (mantenha a cabeca erguida)..."));
      calibrarMPU6050();
    } else {
      Serial.println(F("[COMANDO] Calibracao ignorada: MPU-6050 nao conectado."));
    }
  }
}

// ------------------------------------------------------------------------------
// 9. ATUADORES DE SEGURANÇA
// ------------------------------------------------------------------------------
void ativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
  digitalWrite(PINO_RELE_1, NIVEL_RELE_LIGADO);
  digitalWrite(PINO_RELE_2, NIVEL_RELE_LIGADO);
}

void desativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
  digitalWrite(PINO_RELE_1, testeManualRele1 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
  digitalWrite(PINO_RELE_2, testeManualRele2 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
}

// ------------------------------------------------------------------------------
// 10. TAREFA DE TELEMETRIA EM REDE LOCAL E NUVEM (CORE 0 — SEGUNDO PLANO)
// ------------------------------------------------------------------------------
int montarPayload(const DadosTelemetria &d, uint32_t seq, char *buf, size_t cap) {
  bool conectado = WiFi.status() == WL_CONNECTED;
  String ssid = conectado ? WiFi.SSID() : String("");
  String ip = conectado ? WiFi.localIP().toString() : String("");
  const char* statusMpu = !d.mpuOnline ? "AGUARDANDO MODULO"
                        : (d.mpuCalibrado ? "ONLINE (MPU-6050 ATIVO)" : "ONLINE (SEM CALIBRACAO)");

  return snprintf(buf, cap,
    "{\"dispositivo\":\"ESP32_SMART_DRIVER_01\",\"seq\":%lu,\"uptime_ms\":%lu,"
    "\"sensor_ir\":%d,\"ir_confiavel\":%s,\"tempo_cabeca_baixa_ms\":%lu,\"limiar_ms\":%lu,"
    "\"buzzer\":%d,\"rele\":%d,\"rele_1\":%d,\"rele_2\":%d,\"alarme_teste\":%d,"
    "\"estado\":\"%s\","
    "\"acelerometro\":{\"pitch\":%.1f,\"roll\":%.1f,\"inclinacao\":%.1f,"
    "\"pescada_detectada\":%s,\"online\":%s,\"calibrado\":%s,\"status\":\"%s\"},"
    "\"wifi\":{\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"}}",
    (unsigned long)seq, millis(),
    d.sensorIr, d.irConfiavel ? "true" : "false", d.tempoCabecaBaixaMs, LIMIAR_CABECA_BAIXA_MS,
    d.buzzer, d.rele1, d.rele1, d.rele2, d.testeAtivo ? 1 : 0,
    d.estado,
    d.pitch, d.roll, d.inclinacao,
    d.pescadaDetectada ? "true" : "false", d.mpuOnline ? "true" : "false",
    d.mpuCalibrado ? "true" : "false", statusMpu,
    ssid.c_str(), conectado ? WiFi.RSSI() : 0, ip.c_str());
}

void taskTelemetria(void *pvParameters) {
  WiFiClientSecure clienteSeguro;
  clienteSeguro.setInsecure();
  HTTPClient httpNuvem;
  httpNuvem.setReuse(true); // Mantém a conexão TLS aberta (evita handshake a cada envio)

  bool udpAtivo = false;
  bool servidorConhecido = false;
  IPAddress ipServidor;
  unsigned long ultimoContatoServidor = 0;
  unsigned long ultimoCheckWifi = 0;
  unsigned long ultimoEnvioNuvem = 0;
  unsigned long ultimoSucessoNuvem = 0;
  uint32_t seq = 0;
  char payload[700];
  char resposta[256];

  for (;;) {
    bool conectado = (WiFi.status() == WL_CONNECTED);

    if (!conectado) {
      if (udpAtivo) {
        udp.stop();
        udpAtivo = false;
        servidorConhecido = false;
        Serial.println(F("[WIFI] Conexao perdida. Procurando redes conhecidas..."));
      }
      if (ultimoCheckWifi == 0 || millis() - ultimoCheckWifi >= 2500) {
        ultimoCheckWifi = millis();
        wifiMulti.run(3000); // Bloqueia só o Core 0; o Core 1 segue protegendo o motorista
      }
    } else if (!udpAtivo) {
      udpAtivo = udp.begin(UDP_PORTA_LOCAL);
      Serial.printf("[WIFI] Conectado em \"%s\" | IP da ESP32: %s | RSSI: %d dBm\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    }

    DadosTelemetria snap;
    portENTER_CRITICAL(&mutexDados);
    snap = dadosGlobais;
    portEXIT_CRITICAL(&mutexDados);

    int tamanho = montarPayload(snap, seq++, payload, sizeof(payload));
    if (tamanho <= 0 || tamanho >= (int)sizeof(payload)) tamanho = 0;

    // 1. Transmissão Serial USB Contínua (ponte opcional do server.js)
    if (tamanho) {
      Serial.print(F("TELEMETRY:"));
      Serial.println(payload);
    }

    if (conectado && udpAtivo && tamanho) {
      // 2. UDP: unicast para o servidor já descoberto, ou broadcast para descobrir
      IPAddress destino;
      if (servidorConhecido) {
        destino = ipServidor;
      } else {
        IPAddress ip = WiFi.localIP();
        IPAddress mascara = WiFi.subnetMask();
        destino = IPAddress(ip[0] | ~mascara[0], ip[1] | ~mascara[1],
                            ip[2] | ~mascara[2], ip[3] | ~mascara[3]);
      }
      udp.beginPacket(destino, UDP_PORTA_SERVIDOR);
      udp.write((const uint8_t*)payload, tamanho);
      udp.endPacket();

      // Lê respostas do servidor (descoberta de IP + comandos da Central)
      while (udp.parsePacket() > 0) {
        int lidos = udp.read(resposta, sizeof(resposta) - 1);
        if (lidos <= 0) continue;
        resposta[lidos] = '\0';
        if (strstr(resposta, "INOVATECH")) {
          if (!servidorConhecido || ipServidor != udp.remoteIP()) {
            Serial.printf("[REDE] Servidor local encontrado em %s\n", udp.remoteIP().toString().c_str());
          }
          ipServidor = udp.remoteIP();
          servidorConhecido = true;
          ultimoContatoServidor = millis();
          interpretarComandosTexto(resposta);
        }
      }

      if (servidorConhecido && millis() - ultimoContatoServidor > TIMEOUT_SERVIDOR_LOCAL_MS) {
        servidorConhecido = false;
        Serial.println(F("[REDE] Servidor local sem resposta. Voltando ao modo de descoberta."));
      }

      // 3. Fallback para a nuvem quando não há servidor local respondendo
      bool semServidorLocal = !servidorConhecido &&
                              (millis() - ultimoContatoServidor > TIMEOUT_SERVIDOR_LOCAL_MS);
      if (ENVIAR_NUVEM_SEM_SERVIDOR_LOCAL && semServidorLocal &&
          millis() - ultimoEnvioNuvem >= INTERVALO_NUVEM_MS) {
        ultimoEnvioNuvem = millis();
        httpNuvem.setConnectTimeout(4000);
        httpNuvem.setTimeout(4000);
        if (httpNuvem.begin(clienteSeguro, BACKEND_URL_CLOUD)) {
          httpNuvem.addHeader("Content-Type", "application/json");
          int codigo = httpNuvem.POST((uint8_t*)payload, tamanho);
          if (codigo == 200) {
            ultimoSucessoNuvem = millis();
            String corpo = httpNuvem.getString();
            interpretarComandosTexto(corpo.c_str());
          }
          httpNuvem.end();
        }
      }
    }

    bool nuvemRespondendo = ultimoSucessoNuvem && millis() - ultimoSucessoNuvem < TIMEOUT_SERVIDOR_LOCAL_MS;
    statusRede = !conectado ? REDE_SEM_COMUNICACAO
               : servidorConhecido ? REDE_SERVIDOR_LOCAL
               : nuvemRespondendo ? REDE_NUVEM : REDE_SEM_COMUNICACAO;

    bool emAlerta = strcmp(snap.estado, "NORMAL") != 0;
    vTaskDelay(pdMS_TO_TICKS(emAlerta ? INTERVALO_TELEMETRIA_ALERTA_MS : INTERVALO_TELEMETRIA_MS));
  }
}
