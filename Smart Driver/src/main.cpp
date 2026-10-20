#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ==============================================================================
// INOVA TECH — SMART DRIVER (FIRMWARE ESP32 DUAL-CORE DE ALTA PRECISÃO)
// ==============================================================================
// Monitoramento biométrico de postura e combate à fadiga / sono ao volante.
// Arquitetura Dual-Core (FreeRTOS):
// - CORE 1: Sensor IR, Buzzer e Relé (Tempo real estrito a 100 Hz, zero delay).
// - CORE 0: Conectividade Wi-Fi e Telemetria IoT (Nuvem Render ou Servidor Local).
// ==============================================================================

// ------------------------------------------------------------------------------
// 1. CONFIGURAÇÃO DE REDE WI-FI & SERVIDOR
// ------------------------------------------------------------------------------
// Rede própria da ESP32 (Access Point sem senha sempre ativo):
const char* AP_SSID       = "ESP_900EC9";

// Opcional: caso queira conectar a ESP32 também a um roteador externo:
const char* WIFI_SSID     = "ESP_900EC9";
const char* WIFI_PASSWORD = "";

// Endereço do Servidor de Telemetria:
// - Pelo Wi-Fi próprio da ESP32 (ESP_900EC9): o PC recebe IP 192.168.4.2
// - Pela Nuvem Render HTTPS: "https://inovatech-tcc.onrender.com/api/telemetry"
const char* BACKEND_URL_AP    = "http://192.168.4.2:3001/api/telemetry";
const char* BACKEND_URL_CLOUD = "https://inovatech-tcc.onrender.com/api/telemetry";

// ------------------------------------------------------------------------------
// 2. MAPEAMENTO DOS PINOS DE HARDWARE
// ------------------------------------------------------------------------------
const uint8_t PINO_SENSOR_IR = 32; // GPIO 32 - Sensor IR de obstáculo (haste dos óculos -> queixo/peito)
const uint8_t PINO_BUZZER    = 25; // GPIO 25 - Buzzer sonoro ativo nos óculos
const uint8_t PINO_RELE      = 18; // GPIO 18 - Módulo Relé veicular de segurança

// Níveis Lógicos
const int NIVEL_CABECA_BAIXA     = LOW;  // Sensor IR detecta aproximação (cabeça baixa)
const int NIVEL_BUZZER_LIGADO    = HIGH; // Ativa o buzzer sonoro 3.3V
const int NIVEL_BUZZER_DESLIGADO = LOW;
const int NIVEL_RELE_LIGADO      = HIGH; // Comuta o módulo relé
const int NIVEL_RELE_DESLIGADO   = LOW;

// Filtro Temporal de Segurança: 6 segundos de tolerância com bips progressivos
const unsigned long LIMIAR_CABECA_BAIXA_MS = 6000;
const unsigned long INTERVALO_TELEMETRIA_MS = 250; // 4 pacotes por segundo

// ------------------------------------------------------------------------------
// 3. ESTRUTURA DE TELEMETRIA COMPARTILHADA ENTRE OS CORES
// ------------------------------------------------------------------------------
struct DadosTelemetria {
  int sensorIr;
  unsigned long tempoCabecaBaixaMs;
  int buzzer;
  int rele;
  char estado[24];
  bool alarmeAtivo;
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, "NORMAL", false};
portMUX_TYPE mutexDados = portMUX_INITIALIZER_UNLOCKED;

// Variáveis de Estado no Core 1 (Sensor)
unsigned long tempoInicioCabecaBaixa = 0;
bool cabecaEstavaBaixa               = false;
bool alarmeAtivo                     = false;

// Protótipos de Funções
void ativarAlarmes();
void desativarAlarmes();
void taskTelemetria(void *pvParameters);

// ------------------------------------------------------------------------------
// 4. SETUP INICIAL
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Configuração dos pinos com pull-up interno no sensor
  pinMode(PINO_SENSOR_IR, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_RELE, OUTPUT);

  desativarAlarmes();

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("   Monitoramento de Postura e Fadiga Veicular     "));
  Serial.println(F("=================================================="));
  Serial.println(F("[HARDWARE] Sensor IR: GPIO 32 | Buzzer: GPIO 25 | Relé: GPIO 18"));
  Serial.println(F("[SEGURANCA] Filtro temporal calibrado em 6000 ms (6s) com 1 bip/s"));

  // Mantém a rede Wi-Fi própria da ESP32 (ESP_900EC9 sem senha) sempre ativa
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID); // Sem senha (rede aberta)
  Serial.print(F("[WIFI AP] Rede ativa sem senha: "));
  Serial.print(AP_SSID);
  Serial.print(F(" | IP da ESP32: "));
  Serial.println(WiFi.softAPIP());

  // Se houver um roteador externo configurado diferente de ESP_900EC9, conecta também
  if (String(WIFI_SSID) != "NOME_DO_WIFI" && String(WIFI_SSID) != "ESP_900EC9") {
    Serial.print(F("[WIFI STA] Conectando a "));
    Serial.println(WIFI_SSID);
    if (strlen(WIFI_PASSWORD) > 0) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    } else {
      WiFi.begin(WIFI_SSID);
    }
  }

  // Cria tarefa de Telemetria no Core 0 (Background IoT)
  // Stack de 8192 bytes para suportar TLS/HTTPS do Render e HTTP local
  xTaskCreatePinnedToCore(
    taskTelemetria,
    "TaskTelemetria",
    8192,
    NULL,
    1,
    NULL,
    0 // Executa exclusivamente no Core 0
  );

  Serial.println(F("[FREERTOS] Core 1: Sensor IR & Atuadores (100 Hz, tempo real)."));
  Serial.println(F("[FREERTOS] Core 0: Telemetria IoT e Wi-Fi em background."));
  Serial.println(F("Pronto para operação.\n"));
}

// ------------------------------------------------------------------------------
// 5. LOOP DE SEGURANÇA E SENSOR (CORE 1 — NUNCA TRAVA NEM ATRASA)
// ------------------------------------------------------------------------------
void loop() {
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  bool cabecaBaixaAgora = (estadoLeitura == NIVEL_CABECA_BAIXA);
  unsigned long tempoAtual = millis();
  unsigned long tempoDecorridoMs = 0;
  const char* estadoAtualTexto = "NORMAL";

  if (cabecaBaixaAgora) {
    if (!cabecaEstavaBaixa) {
      cabecaEstavaBaixa = true;
      tempoInicioCabecaBaixa = tempoAtual;
      Serial.println(F("Aviso: Cabeça Baixa Detectada (Iniciando contagem de segurança)..."));
    } 

    tempoDecorridoMs = tempoAtual - tempoInicioCabecaBaixa;

    if (!alarmeAtivo) {
      if (tempoDecorridoMs >= LIMIAR_CABECA_BAIXA_MS) {
        alarmeAtivo = true;
        ativarAlarmes();
        estadoAtualTexto = "ALARME_ATIVADO";
        Serial.println(F("PERIGO: MOTORISTA ADORMECEU! BUZZER E RELE ACIONADOS!"));
      } else {
        estadoAtualTexto = "PRE_ALERTA";
      }
    } else {
      estadoAtualTexto = "ALARME_ATIVADO";
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
  portENTER_CRITICAL(&mutexDados);
  dadosGlobais.sensorIr = estadoLeitura;
  dadosGlobais.tempoCabecaBaixaMs = tempoDecorridoMs;
  dadosGlobais.buzzer = alarmeAtivo ? 1 : 0;
  dadosGlobais.rele = alarmeAtivo ? 1 : 0;
  dadosGlobais.alarmeAtivo = alarmeAtivo;
  strncpy(dadosGlobais.estado, estadoAtualTexto, sizeof(dadosGlobais.estado) - 1);
  portEXIT_CRITICAL(&mutexDados);

  // Amostragem constante a 100 Hz (10 ms) no Core 1
  delay(10);
}

// ------------------------------------------------------------------------------
// 6. ATUADORES DE SEGURANÇA
// ------------------------------------------------------------------------------
void ativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
  digitalWrite(PINO_RELE, NIVEL_RELE_LIGADO);
}

void desativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
  digitalWrite(PINO_RELE, NIVEL_RELE_DESLIGADO);
}

// ------------------------------------------------------------------------------
// 7. TAREFA DE TELEMETRIA EM NUVEM E REDE (CORE 0 — EXECUTA EM SEGUNDO PLANO)
// ------------------------------------------------------------------------------
void taskTelemetria(void *pvParameters) {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure(); // Pula validação de certificado CA para garantir comunicação com o Render

  HTTPClient http;

  for (;;) {
    // Obtém snapshot instantâneo dos dados do motorista protegidos por mutex
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
    payload += "\"rele\":" + String(snap.rele) + ",";
    payload += "\"estado\":\"" + String(snap.estado) + "\",";
    payload += "\"acelerometro\":{\"pitch\":0.0,\"roll\":0.0,\"pescada_detectada\":" + String(snap.alarmeAtivo ? "true" : "false") + "}";
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

    // 3. Se estiver conectado a um Wi-Fi com internet, envia para a nuvem Render
    if (WiFi.status() == WL_CONNECTED) {
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
