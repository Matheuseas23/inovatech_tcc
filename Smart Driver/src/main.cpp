#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

WiFiMulti wifiMulti;

// ==============================================================================
// INOVA TECH — SMART DRIVER (FIRMWARE ESP32 DUAL-CORE DE ALTA PRECISÃO)
// ==============================================================================
// Monitoramento biométrico de postura e combate à fadiga / sono ao volante.
// Arquitetura Dual-Core (FreeRTOS):
// - CORE 1: Sensor IR, Buzzer e Módulo Relé Duplo HL-52S (100 Hz, tempo real estrito).
// - CORE 0: Conectividade Wi-Fi e Telemetria IoT (Nuvem Render ou Servidor Local).
// ==============================================================================

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO DE REDES WI-FI (MULTI-WIFI AUTOMÁTICO) & SERVIDOR
// ------------------------------------------------------------------------------
// Rede própria da ESP32 (Access Point sem senha sempre ativo):
const char* AP_SSID = "ESP_900EC9";

// Lista de Redes Wi-Fi Cadastradas (comuta automaticamente para a rede disponível):
// Rede 1 (Celular Redmi):
const char* WIFI_1_SSID = "REDMI Note 15 5G";
const char* WIFI_1_PASS = "Garcia2026";

// Rede 2 (Wi-Fi Residencial):
const char* WIFI_2_SSID = "GARCIA 7G";
const char* WIFI_2_PASS = "MMarcos12@";

// Endereço do Servidor de Telemetria:
const char* BACKEND_URL_AP    = "http://192.168.4.2:3001/api/telemetry";
const char* BACKEND_URL_LOCAL = "http://192.168.33.216:3001/api/telemetry";
const char* BACKEND_URL_CLOUD = "https://inovatech-tcc.onrender.com/api/telemetry";

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DOS PINOS DE HARDWARE
// ------------------------------------------------------------------------------
const uint8_t PINO_SENSOR_IR = 32; // GPIO 32 - Sensor IR de obstáculo (haste dos óculos -> queixo/peito)
const uint8_t PINO_BUZZER    = 25; // GPIO 25 - Buzzer sonoro ativo nos óculos
const uint8_t PINO_RELE_1    = 18; // GPIO 18 - Relé Canal 1: Intervenção Veicular Safe-Stop (Tração)
const uint8_t PINO_RELE_2    = 19; // GPIO 19 - Relé Canal 2: Sinalização de Emergência (Pisca-Alerta Veicular)
const uint8_t PINO_RELE_GND  = 13; // GPIO 13 (D13) - GND Virtual (0V) exclusivo para o Módulo Relé

// Níveis Lógicos
const int NIVEL_CABECA_BAIXA     = LOW;  // Sensor IR detecta aproximação (cabeça baixa)
const int NIVEL_BUZZER_LIGADO    = HIGH; // Ativa o buzzer sonoro 3.3V
const int NIVEL_BUZZER_DESLIGADO = LOW;

// Módulo Relé Duplo Optoisolado HL-52S (Active LOW):
// LOW aciona a bobina / acende o LED do relé | HIGH desliga
const int NIVEL_RELE_LIGADO      = LOW;
const int NIVEL_RELE_DESLIGADO   = HIGH;

// Filtro Temporal de Segurança: 3 segundos de tolerância com aviso sonoro progressivo
const unsigned long LIMIAR_CABECA_BAIXA_MS = 3000;
const unsigned long INTERVALO_TELEMETRIA_MS = 250; // 4 pacotes por segundo

// ------------------------------------------------------------------------------
// 3. ESTRUTURA DE TELEMETRIA COMPARTILHADA ENTRE OS CORES
// ------------------------------------------------------------------------------
struct DadosTelemetria {
  int sensorIr;
  unsigned long tempoCabecaBaixaMs;
  int buzzer;
  int rele1;
  int rele2;
  char estado[24];
  bool alarmeAtivo;
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, 0, "NORMAL", false};
portMUX_TYPE mutexDados = portMUX_INITIALIZER_UNLOCKED;

// Variáveis de Estado no Core 1 (Sensor)
unsigned long tempoInicioCabecaBaixa = 0;
bool cabecaEstavaBaixa               = false;
bool alarmeAtivo                     = false;
bool testeManualRele1                = false;
bool testeManualRele2                = false;

// Protótipos de Funções
void ativarAlarmes();
void desativarAlarmes();
void processarComandosSerial();
void taskTelemetria(void *pvParameters);

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

  desativarAlarmes();

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("   Monitoramento de Postura e Fadiga Veicular     "));
  Serial.println(F("=================================================="));
  Serial.println(F("[HARDWARE] Sensor IR: GPIO 32 | Buzzer: GPIO 25"));
  Serial.println(F("[HARDWARE] Relé 1 (Safe-Stop): GPIO 18 | Relé 2 (Pisca-Alerta): GPIO 19"));
  Serial.println(F("[HARDWARE] GND Virtual Relé: GPIO 13 (LOW / 0V permanente)"));
  Serial.println(F("[SEGURANCA] Filtro temporal calibrado em 3000 ms (3.0s)"));
  Serial.println(F("[AVISO] Contagem progressiva: 1 bip por segundo (3 bips antes do alarme)"));

  // Mantém a rede Wi-Fi própria da ESP32 (ESP_900EC9 sem senha) sempre ativa
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID); // Sem senha (rede aberta)
  Serial.print(F("[WIFI AP] Rede ativa sem senha: "));
  Serial.print(AP_SSID);
  Serial.print(F(" | IP da ESP32: "));
  Serial.println(WiFi.softAPIP());

  // Registra as redes no gerenciador inteligente WiFiMulti
  wifiMulti.addAP(WIFI_1_SSID, WIFI_1_PASS);
  wifiMulti.addAP(WIFI_2_SSID, WIFI_2_PASS);

  Serial.println(F("[WIFI MULTI] Redes registradas no sistema:"));
  Serial.print(F("  1. ")); Serial.println(WIFI_1_SSID);
  Serial.print(F("  2. ")); Serial.println(WIFI_2_SSID);
  Serial.println(F("[WIFI MULTI] Conectando à melhor rede disponível..."));

  // Tentativa inicial rápida de conexão
  if (wifiMulti.run() == WL_CONNECTED) {
    Serial.print(F("[WIFI STA] Conectado com sucesso à rede: "));
    Serial.println(WiFi.SSID());
    Serial.print(F("[WIFI STA] IP da ESP32: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("[WIFI STA] Buscando redes ativas em segundo plano no Core 0..."));
  }

  // Cria tarefa de Telemetria no Core 0 (Background IoT)
  xTaskCreatePinnedToCore(
    taskTelemetria,
    "TaskTelemetria",
    8192,
    NULL,
    1,
    NULL,
    0 // Executa exclusivamente no Core 0
  );

  Serial.println(F("[FREERTOS] Core 1: Sensor IR & Atuadores Duplos (100 Hz, tempo real)."));
  Serial.println(F("[FREERTOS] Core 0: Telemetria IoT e Wi-Fi em background."));
  Serial.println(F("Pronto para operação.\n"));
}

// ------------------------------------------------------------------------------
// 5. LOOP DE SEGURANÇA E SENSOR (CORE 1 — NUNCA TRAVA NEM ATRASA)
// ------------------------------------------------------------------------------
void loop() {
  processarComandosSerial();

  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  bool cabecaBaixaAgora = (estadoLeitura == NIVEL_CABECA_BAIXA);
  unsigned long tempoAtual = millis();
  unsigned long tempoDecorridoMs = 0;
  const char* estadoAtualTexto = "NORMAL";
  bool bipPreAlertaAtivo = false;

  if (cabecaBaixaAgora) {
    if (!cabecaEstavaBaixa) {
      cabecaEstavaBaixa = true;
      tempoInicioCabecaBaixa = tempoAtual;
      Serial.println(F("Aviso: Cabeça Baixa Detectada (Iniciando contagem de segurança de 3s)..."));
    } 

    tempoDecorridoMs = tempoAtual - tempoInicioCabecaBaixa;

    if (!alarmeAtivo) {
      if (tempoDecorridoMs >= LIMIAR_CABECA_BAIXA_MS) {
        alarmeAtivo = true;
        ativarAlarmes();
        estadoAtualTexto = "ALARME_ATIVADO";
        Serial.println(F("PERIGO: MOTORISTA ADORMECEU! BUZZER E RELES 1 E 2 ACIONADOS!"));
      } else {
        estadoAtualTexto = "PRE_ALERTA";

        // 3 BIPS PROGRESSIVOS (1 a cada segundo durante a contagem antes do alarme):
        // 1º Bip: aos 1.0s (850 a 1050 ms) -> Pulso de 200ms
        // 2º Bip: aos 2.0s (1850 a 2050 ms) -> Pulso de 200ms
        // 3º Bip: aos 3.0s (2850 a 3000 ms) -> Pulso de 150ms antes do alarme contínuo
        bipPreAlertaAtivo = (tempoDecorridoMs >= 850 && tempoDecorridoMs < 1050) ||
                            (tempoDecorridoMs >= 1850 && tempoDecorridoMs < 2050) ||
                            (tempoDecorridoMs >= 2850 && tempoDecorridoMs < 3000);

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
      Serial.println(F("Postura Normalizada: Cabeça Erguida (Alarmes cessados)."));
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
  dadosGlobais.alarmeAtivo = alarmeAtivo;
  strncpy(dadosGlobais.estado, estadoAtualTexto, sizeof(dadosGlobais.estado) - 1);
  portEXIT_CRITICAL(&mutexDados);

  // Amostragem constante a 100 Hz (10 ms) no Core 1
  delay(10);
}

// ------------------------------------------------------------------------------
// 6. PROCESSAMENTO DE COMANDOS DA SERIAL (PC / CENTRAL)
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
// 7. ATUADORES DE SEGURANÇA
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
// 8. TAREFA DE TELEMETRIA EM NUVEM E REDE (CORE 0 — EXECUTA EM SEGUNDO PLANO)
// ------------------------------------------------------------------------------
void taskTelemetria(void *pvParameters) {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Pula validação CA para conectar à nuvem Render

  HTTPClient http;
  unsigned long ultimoCheckMultiWifi = 0;

  for (;;) {
    // Se a conexão cair, o WiFiMulti busca e reconecta automaticamente à melhor rede disponível
    if (WiFi.status() != WL_CONNECTED) {
      if (millis() - ultimoCheckMultiWifi >= 3000) {
        ultimoCheckMultiWifi = millis();
        wifiMulti.run();
      }
    }
    // Obtém snapshot instantâneo dos dados protegidos por mutex
    DadosTelemetria snap;
    portENTER_CRITICAL(&mutexDados);
    snap = dadosGlobais;
    portEXIT_CRITICAL(&mutexDados);

    // Monta o payload JSON formatado
    String payload = "{";
    payload += "\"dispositivo\":\"ESP32_SMART_DRIVER_01\",";
    payload += "\"sensor_ir\":" + String(snap.sensorIr) + ",";
    payload += "\"tempo_cabeca_baixa_ms\":" + String(snap.tempoCabecaBaixaMs) + ",";
    payload += "\"buzzer\":" + String(snap.buzzer) + ",";
    payload += "\"rele\":" + String(snap.rele1) + ",";
    payload += "\"rele_1\":" + String(snap.rele1) + ",";
    payload += "\"rele_2\":" + String(snap.rele2) + ",";
    payload += "\"estado\":\"" + String(snap.estado) + "\",";
    payload += "\"atuadores\":{\"rele1_safestop\":" + String(snap.rele1) + ",\"rele2_piscaalerta\":" + String(snap.rele2) + "}";
    payload += "}";

    // 1. Emite telemetria via Serial USB (COM5) para ponte direta com o PC
    Serial.println("TELEMETRY:" + payload);

    // 2. Se o PC estiver conectado na rede Wi-Fi ESP_900EC9, envia via HTTP para 192.168.4.2:3001
    if (WiFi.softAPgetStationNum() > 0) {
      http.setTimeout(350);
      if (http.begin(plainClient, BACKEND_URL_AP)) {
        http.addHeader("Content-Type", "application/json");
        http.POST(payload);
        http.end();
      }
    }

    // 3. Se estiver conectado ao ponto de acesso do celular (Wi-Fi), envia direto para o IP do notebook local e para a nuvem
    if (WiFi.status() == WL_CONNECTED) {
      // Envio local ultra-rápido para o notebook na mesma rede do celular
      http.setTimeout(400);
      if (http.begin(plainClient, BACKEND_URL_LOCAL)) {
        http.addHeader("Content-Type", "application/json");
        http.POST(payload);
        http.end();
      }

      // Envio para a nuvem Render
      http.setTimeout(2500);
      if (http.begin(secureClient, BACKEND_URL_CLOUD)) {
        http.addHeader("Content-Type", "application/json");
        http.POST(payload);
        http.end();
      }
    }

    // Intervalo de transmissão contínua (250 ms = 4 vezes por segundo)
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_TELEMETRIA_MS));
  }
}



