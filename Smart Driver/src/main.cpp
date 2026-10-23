#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>

WiFiMulti wifiMulti;
WiFiUDP udpClient;
const uint16_t UDP_PORT_DESTINO = 3002;

// ==============================================================================
// INOVA TECH — SMART DRIVER (FIRMWARE ESP32 DUAL-CORE DE ALTA PRECISÃO)
// ==============================================================================
// Monitoramento biométrico de postura e combate à fadiga / sono ao volante.
// Arquitetura Dual-Core (FreeRTOS):
// - CORE 1: Sensor IR, Acelerômetro GY-521 (MPU-6050), Buzzer e Relé Duplo HL-52S.
// - CORE 0: Conectividade Wi-Fi e Telemetria IoT (Local + Nuvem).
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

// Endereços do Servidor de Telemetria:
const char* BACKEND_URL_CASA      = "http://192.168.1.218:3001/api/telemetry";
const char* BACKEND_URL_IPHONE_PC = "http://172.20.10.2:3001/api/telemetry";
const char* BACKEND_URL_CLOUD     = "https://inovatech-tcc.onrender.com/api/telemetry";

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DOS PINOS DE HARDWARE
// ------------------------------------------------------------------------------
const uint8_t PINO_SENSOR_IR = 32; // GPIO 32 - Sensor IR de obstáculo
const uint8_t PINO_BUZZER    = 25; // GPIO 25 - Buzzer sonoro ativo
const uint8_t PINO_RELE_1    = 18; // GPIO 18 - Relé Safe-Stop (Tração)
const uint8_t PINO_RELE_2    = 19; // GPIO 19 - Relé Pisca-Alerta Veicular
const uint8_t PINO_RELE_GND  = 13; // GPIO 13 (D13) - GND Virtual Relé

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
const int NIVEL_RELE_LIGADO      = LOW;
const int NIVEL_RELE_DESLIGADO   = HIGH;

// Filtro Temporal de Segurança: 3.5 segundos de tolerância com aviso sonoro progressivo
const unsigned long LIMIAR_CABECA_BAIXA_MS = 3500;
const unsigned long INTERVALO_TELEMETRIA_MS = 200; // 5 pacotes por segundo

// ------------------------------------------------------------------------------
// 3. ESTRUTURA DE TELEMETRIA COMPARTILHADA ENTRE OS CORES
// ------------------------------------------------------------------------------
struct DadosTelemetria {
  int sensorIr;
  unsigned long tempoCabecaBaixaMs;
  int buzzer;
  int rele1;
  int rele2;
  float pitch;
  float roll;
  bool pescadaDetectada;
  bool mpuOnline;
  char estado[24];
  bool alarmeAtivo;
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, 0, 0.0, 0.0, false, false, "NORMAL", false};
portMUX_TYPE mutexDados = portMUX_INITIALIZER_UNLOCKED;

// Variáveis de Estado no Core 1
unsigned long tempoInicioCabecaBaixa = 0;
bool cabecaEstavaBaixa               = false;
bool alarmeAtivo                     = false;
bool testeManualRele1                = false;
bool testeManualRele2                = false;
bool mpuDetectado                    = false;

float pitchFiltrado = 0.0;
float rollFiltrado  = 0.0;
float ultimoPitch    = 0.0;

// Protótipos de Funções
void ativarAlarmes();
void desativarAlarmes();
void processarComandosSerial();
void taskTelemetria(void *pvParameters);
void inicializarMPU6050();
void lerMPU6050(float &pitch, float &roll, bool &pescada);

// ------------------------------------------------------------------------------
// 4. SETUP INICIAL
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Configuração dos pinos com pull-up interno no sensor
  pinMode(PINO_SENSOR_IR, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_RELE_1, OUTPUT);
  pinMode(PINO_RELE_2, OUTPUT);

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
  inicializarMPU6050();

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("   Monitoramento de Postura e Fadiga Veicular     "));
  Serial.println(F("=================================================="));
  Serial.println(F("[HARDWARE] Sensor IR: GPIO 32 | Buzzer: GPIO 25"));
  Serial.println(F("[HARDWARE] Relé 1: GPIO 18 | Relé 2: GPIO 19 | GND Relé: GPIO 13"));
  Serial.printf("[HARDWARE] Acelerômetro GY-521: SDA=%d, SCL=%d, VCC=%d, GND=%d (RX2)\n",
                PINO_MPU_SDA, PINO_MPU_SCL, PINO_MPU_VCC, PINO_MPU_GND);
  Serial.printf("[HARDWARE] Status MPU-6050: %s\n", mpuDetectado ? "CONECTADO COM SUCESSO!" : "AGUARDANDO CONEXAO");

  // Modo Station puro com reconexão resiliente
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  // Registra as redes conhecidas
  wifiMulti.addAP(WIFI_CASA_SSID, WIFI_CASA_PASS);
  wifiMulti.addAP(WIFI_REDMI_SSID, WIFI_REDMI_PASS);
  wifiMulti.addAP(WIFI_IPHONE_SSID, WIFI_IPHONE_PASS);

  WiFi.begin(WIFI_CASA_SSID, WIFI_CASA_PASS);

  // Inicializa a Tarefa de Telemetria no Core 0 (Background)
  xTaskCreatePinnedToCore(
    taskTelemetria,
    "TaskTelemetria",
    10240,
    NULL,
    1,
    NULL,
    0
  );
}

// ------------------------------------------------------------------------------
// 5. INICIALIZAÇÃO E LEITURA DO ACELERÔMETRO MPU-6050 (I2C)
// ------------------------------------------------------------------------------
void inicializarMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  byte erro = Wire.endTransmission();
  if (erro == 0) {
    mpuDetectado = true;
    // Acorda o MPU-6050 (limpa o registrador PWR_MGMT_1)
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x6B); // Registrador PWR_MGMT_1
    Wire.write(0x00); // 0 para acordar o chip
    Wire.endTransmission(true);

    // Configura fundo de escala do acelerômetro (+/- 2g)
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x1C); // ACCEL_CONFIG
    Wire.write(0x00);
    Wire.endTransmission(true);
  } else {
    mpuDetectado = false;
  }
}

void lerMPU6050(float &pitch, float &roll, bool &pescada) {
  pescada = false;
  if (!mpuDetectado) {
    inicializarMPU6050();
    if (!mpuDetectado) {
      pitch = 0.0;
      roll = 0.0;
      return;
    }
  }

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B); // Registrador ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) {
    mpuDetectado = false;
    return;
  }

  if (Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)6, true) == 6) {
    int16_t ax = (Wire.read() << 8) | Wire.read();
    int16_t ay = (Wire.read() << 8) | Wire.read();
    int16_t az = (Wire.read() << 8) | Wire.read();

    float axG = ax / 16384.0;
    float ayG = ay / 16384.0;
    float azG = az / 16384.0;

    // Ângulo Pitch (Inclinação Frontal / Queixo para baixo) em graus
    float pitchInst = atan2(-axG, sqrt(ayG * ayG + azG * azG)) * 57.2957795;
    // Ângulo Roll (Inclinação Lateral) em graus
    float rollInst  = atan2(ayG, azG) * 57.2957795;

    // Filtro Passa-Baixas suave (90% anterior + 10% instantâneo)
    pitchFiltrado = (pitchFiltrado * 0.85) + (pitchInst * 0.15);
    rollFiltrado  = (rollFiltrado * 0.85) + (rollInst * 0.15);

    pitch = pitchFiltrado;
    roll  = rollFiltrado;

    // Detecção de "Pescada" de Sono (queda angular brusca > 35°/s)
    float deltaPitch = pitch - ultimoPitch;
    if (deltaPitch > 25.0 || pitch > 38.0) {
      pescada = true;
    }
    ultimoPitch = pitch;
  } else {
    mpuDetectado = false;
  }
}

// ------------------------------------------------------------------------------
// 6. LOOP PRINCIPAL (CORE 1 — 100 HZ / TEMPO REAL ESTRITO)
// ------------------------------------------------------------------------------
void loop() {
  processarComandosSerial();

  // Leitura digital do Sensor Infravermelho
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  unsigned long agora = millis();
  unsigned long tempoDecorridoMs = 0;
  bool bipPreAlertaAtivo = false;
  const char* estadoAtualTexto = "NORMAL";

  // Leitura do Acelerômetro GY-521
  float pAng = 0.0, rAng = 0.0;
  bool pescadaSono = false;
  lerMPU6050(pAng, rAng, pescadaSono);

  // Condição de Cabeça Baixa: Sensor IR detecta OU Acelerômetro inclina mais de 35 graus
  bool condicaoCabecaBaixa = (estadoLeitura == NIVEL_CABECA_BAIXA) || (pAng > 32.0) || pescadaSono;

  if (condicaoCabecaBaixa) {
    if (!cabecaEstavaBaixa) {
      cabecaEstavaBaixa = true;
      tempoInicioCabecaBaixa = agora;
    }

    tempoDecorridoMs = agora - tempoInicioCabecaBaixa;

    if (!alarmeAtivo) {
      if (tempoDecorridoMs >= LIMIAR_CABECA_BAIXA_MS || pescadaSono) {
        alarmeAtivo = true;
        estadoAtualTexto = "ALARME_ATIVADO";
        ativarAlarmes();
        Serial.println(F(">>> ALERTA CRITICO: Fadiga confirmada! Buzzer continuo e Reles ativados!"));
      } else {
        estadoAtualTexto = "PRE_ALERTA";

        // 3 BIPS PROGRESSIVOS ALONGADOS:
        // 1º Bip: aos 1.0s (700 a 1050 ms) -> Pulso de 350ms
        // 2º Bip: aos 2.0s (1700 a 2050 ms) -> Pulso de 350ms
        // 3º Bip: aos 3.0s (2700 a 3050 ms) -> Pulso de 350ms
        // Aos 3.5s (3500ms): Alarme contínuo ininterrupto + relés!
        bipPreAlertaAtivo = (tempoDecorridoMs >= 700 && tempoDecorridoMs < 1050) ||
                            (tempoDecorridoMs >= 1700 && tempoDecorridoMs < 2050) ||
                            (tempoDecorridoMs >= 2700 && tempoDecorridoMs < 3050);

        digitalWrite(PINO_BUZZER, bipPreAlertaAtivo ? NIVEL_BUZZER_LIGADO : NIVEL_BUZZER_DESLIGADO);
      }
    } else {
      estadoAtualTexto = "ALARME_ATIVADO";
      digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
    }
  } 
  else {
    if (cabecaEstavaBaixa || alarmeAtivo) {
      desativarAlarmes();
      alarmeAtivo = false;
      cabecaEstavaBaixa = false;
      tempoInicioCabecaBaixa = 0;
      Serial.println(F("Postura Normalizada: Cabeca Erguida (Alarmes cessados)."));
    }
    estadoAtualTexto = "NORMAL";
    tempoDecorridoMs = 0;
  }

  // Atualiza os dados compartilhados com o Core 0 de forma atômica
  int r1Val = (alarmeAtivo || testeManualRele1) ? 1 : 0;
  int r2Val = (alarmeAtivo || testeManualRele2) ? 1 : 0;

  portENTER_CRITICAL(&mutexDados);
  dadosGlobais.sensorIr = estadoLeitura;
  dadosGlobais.tempoCabecaBaixaMs = tempoDecorridoMs;
  dadosGlobais.buzzer = (alarmeAtivo || bipPreAlertaAtivo) ? 1 : 0;
  dadosGlobais.rele1 = r1Val;
  dadosGlobais.rele2 = r2Val;
  dadosGlobais.pitch = pAng;
  dadosGlobais.roll = rAng;
  dadosGlobais.pescadaDetectada = pescadaSono;
  dadosGlobais.mpuOnline = mpuDetectado;
  dadosGlobais.alarmeAtivo = alarmeAtivo;
  strncpy(dadosGlobais.estado, estadoAtualTexto, sizeof(dadosGlobais.estado) - 1);
  portEXIT_CRITICAL(&mutexDados);

  delay(10);
}

// ------------------------------------------------------------------------------
// 7. PROCESSAMENTO DE COMANDOS DA SERIAL (PC / CENTRAL)
// ------------------------------------------------------------------------------
void processarComandosSerial() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "CMD:TEST_ALARM" || cmd == "TEST_ALARM") {
      alarmeAtivo = true;
      ativarAlarmes();
      Serial.println(F("[COMANDO] Teste geral acionado via Serial!"));
    } else if (cmd == "CMD:SILENCIAR" || cmd == "SILENCIAR_ALARME" || cmd == "RESET_ALARM") {
      alarmeAtivo = false;
      testeManualRele1 = false;
      testeManualRele2 = false;
      desativarAlarmes();
      Serial.println(F("[COMANDO] Alarmes silenciados via Serial!"));
    } else if (cmd == "CMD:TOGGLE_RELE1") {
      testeManualRele1 = !testeManualRele1;
      digitalWrite(PINO_RELE_1, testeManualRele1 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
      Serial.println(testeManualRele1 ? F("[RELE 1] Safe-Stop LIGADO") : F("[RELE 1] Safe-Stop DESLIGADO"));
    } else if (cmd == "CMD:TOGGLE_RELE2") {
      testeManualRele2 = !testeManualRele2;
      digitalWrite(PINO_RELE_2, testeManualRele2 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
      Serial.println(testeManualRele2 ? F("[RELE 2] Pisca-Alerta LIGADO") : F("[RELE 2] Pisca-Alerta DESLIGADO"));
    }
  }
}

// ------------------------------------------------------------------------------
// 8. ATUADORES DE SEGURANÇA
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
// 9. TAREFA DE TELEMETRIA EM NUVEM E REDE (CORE 0 — EXECUTA EM SEGUNDO PLANO)
// ------------------------------------------------------------------------------
void taskTelemetria(void *pvParameters) {
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  unsigned long ultimoCheckMultiWifi = 0;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - ultimoCheckMultiWifi >= 2500) {
        ultimoCheckMultiWifi = millis();
        wifiMulti.run();
      }
    }

    DadosTelemetria snap;
    portENTER_CRITICAL(&mutexDados);
    snap = dadosGlobais;
    portEXIT_CRITICAL(&mutexDados);

    // Monta o payload JSON completo incluindo os dados reais do acelerômetro
    String payload = "{";
    payload += "\"dispositivo\":\"ESP32_SMART_DRIVER_01\",";
    payload += "\"sensor_ir\":" + String(snap.sensorIr) + ",";
    payload += "\"tempo_cabeca_baixa_ms\":" + String(snap.tempoCabecaBaixaMs) + ",";
    payload += "\"buzzer\":" + String(snap.buzzer) + ",";
    payload += "\"rele\":" + String(snap.rele1) + ",";
    payload += "\"rele_1\":" + String(snap.rele1) + ",";
    payload += "\"rele_2\":" + String(snap.rele2) + ",";
    payload += "\"estado\":\"" + String(snap.estado) + "\",";
    payload += "\"acelerometro\":{";
    payload += "\"pitch\":" + String(snap.pitch, 1) + ",";
    payload += "\"roll\":" + String(snap.roll, 1) + ",";
    payload += "\"pescada_detectada\":" + String(snap.pescadaDetectada ? "true" : "false") + ",";
    payload += "\"status\":\"" + String(snap.mpuOnline ? "ONLINE (MPU-6050 ATIVO)" : "AGUARDANDO MODULO") + "\"";
    payload += "},";
    payload += "\"atuadores\":{\"rele1_safestop\":" + String(snap.rele1) + ",\"rele2_piscaalerta\":" + String(snap.rele2) + "}";
    payload += "}";

    // 1. Transmissão UDP Broadcast Instantânea na rede local (Zero Latência)
    if (WiFi.status() == WL_CONNECTED) {
      udpClient.beginPacket("255.255.255.255", UDP_PORT_DESTINO);
      udpClient.print(payload);
      udpClient.endPacket();
    }

    // 2. Transmissão Serial USB Contínua
    Serial.println("TELEMETRY:" + payload);

    // 3. Envio HTTP direto para o IP do computador na rede de casa
    if (WiFi.status() == WL_CONNECTED) {
      http.setTimeout(180);
      if (http.begin(plainClient, BACKEND_URL_CASA)) {
        http.addHeader("Content-Type", "application/json");
        http.POST(payload);
        http.end();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(INTERVALO_TELEMETRIA_MS));
  }
}
