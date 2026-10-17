#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ==============================================================================
// INOVA TECH — SMART DRIVER (FIRMWARE ESP32 COM TELEMETRIA IOT)
// ==============================================================================
// Monitoramento biométrico de postura e detecção de fadiga / queda de cabeça.
// Integração instantânea com o Backend Node.js e Painel de Controle HUD.
// ==============================================================================

// ------------------------------------------------------------------------------
// CONFIGURAÇÃO DE REDE WI-FI & BACKEND
// ------------------------------------------------------------------------------
// Altere para a rede Wi-Fi da sala da apresentação ou do seu celular 4G:
const char* WIFI_SSID     = "NOME_DO_WIFI";
const char* WIFI_PASSWORD = "SENHA_DO_WIFI";

// Endereço do Servidor de Telemetria:
// - Apresentação Local (PC da sala): "http://<IP_DO_PC>:3001/api/telemetry"
// - Nuvem (Render):                  "https://inovatech-tcc.onrender.com/api/telemetry"
const char* BACKEND_URL   = "https://inovatech-tcc.onrender.com/api/telemetry";

// ------------------------------------------------------------------------------
// MAPEAMENTO DOS PINOS DE HARDWARE
// ------------------------------------------------------------------------------
const uint8_t PINO_SENSOR_IR = 32; // GPIO 32 - Sensor IR de obstáculo (queixo/peito)
const uint8_t PINO_BUZZER    = 25; // GPIO 25 - Buzzer sonoro ativo nos óculos
const uint8_t PINO_RELE      = 18; // GPIO 18 - Módulo Relé de segurança veicular

// Lógica de níveis
const int NIVEL_CABECA_BAIXA     = LOW;
const int NIVEL_BUZZER_LIGADO    = HIGH;
const int NIVEL_BUZZER_DESLIGADO = LOW;
const int NIVEL_RELE_LIGADO      = HIGH;
const int NIVEL_RELE_DESLIGADO   = LOW;

// Filtro Temporal de Segurança (2.500 ms)
const unsigned long LIMIAR_CABECA_BAIXA_MS = 2500;
const unsigned long INTERVALO_TELEMETRIA_MS = 250; // 4 transmissões por segundo

// Variáveis de Estado
unsigned long tempoInicioCabecaBaixa = 0;
unsigned long ultimoEnvioTelemetria  = 0;
bool cabecaEstavaBaixa               = false;
bool alarmeAtivo                     = false;

// Protótipos de funções
void ativarAlarmes();
void desativarAlarmes();
void conectarWiFi();
void enviarTelemetria(int irValor, unsigned long tempoBaixaMs, int buzzerVal, int releVal, const char* estadoTexto);

void setup() {
  Serial.begin(115200);

  pinMode(PINO_SENSOR_IR, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  pinMode(PINO_RELE, OUTPUT);

  desativarAlarmes();

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("  Monitorizacao de Postura e Queda de Cabeca (Sono) "));
  Serial.println(F("=================================================="));

  conectarWiFi();

  Serial.println(F("Sistema pronto. A monitorizar inclinacao da cabeca...\n"));
}

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
      Serial.println(F("Aviso: Cabeca Baixa Detectada (A iniciar contagem)..."));
    } 

    tempoDecorridoMs = tempoAtual - tempoInicioCabecaBaixa;

    if (!alarmeAtivo) {
      if (tempoDecorridoMs >= LIMIAR_CABECA_BAIXA_MS) {
        alarmeAtivo = true;
        ativarAlarmes();
        estadoAtualTexto = "ALARME_ATIVADO";
        Serial.println(F("ALARME ATIVADO: MOTORISTA ADORMECEU COM A CABECA BAIXA!"));
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
      Serial.println(F("Postura Normalizada: Cabeca Erguida"));
    }
    estadoAtualTexto = "NORMAL";
    tempoDecorridoMs = 0;
  }

  // Transmissão periódica da telemetria para a Central
  if (tempoAtual - ultimoEnvioTelemetria >= INTERVALO_TELEMETRIA_MS) {
    ultimoEnvioTelemetria = tempoAtual;
    int bVal = alarmeAtivo ? 1 : 0;
    int rVal = alarmeAtivo ? 1 : 0;
    enviarTelemetria(estadoLeitura, tempoDecorridoMs, bVal, rVal, estadoAtualTexto);
  }
}

void ativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
  digitalWrite(PINO_RELE, NIVEL_RELE_LIGADO);
}

void desativarAlarmes() {
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
  digitalWrite(PINO_RELE, NIVEL_RELE_DESLIGADO);
}

void conectarWiFi() {
  Serial.print(F("[INOVATECH] Conectando ao Wi-Fi: "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Tenta conectar por 10 segundos para não travar o loop físico
  unsigned long inicioConexao = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - inicioConexao < 10000)) {
    delay(500);
    Serial.print(F("."));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\n[INOVATECH] Wi-Fi Conectado com sucesso!"));
    Serial.print(F("[INOVATECH] IP do ESP32: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("\n[AVISO] Nao foi possivel conectar ao Wi-Fi. Operando em modo offline de seguranca."));
  }
}

void enviarTelemetria(int irValor, unsigned long tempoBaixaMs, int buzzerVal, int releVal, const char* estadoTexto) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(BACKEND_URL);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(150); // Timeout rápido: nunca atrasa os sensores físicos dos óculos

    String payload = "{";
    payload += "\"dispositivo\":\"ESP32_SMART_DRIVER_01\",";
    payload += "\"sensor_ir\":" + String(irValor) + ",";
    payload += "\"tempo_cabeca_baixa_ms\":" + String(tempoBaixaMs) + ",";
    payload += "\"buzzer\":" + String(buzzerVal) + ",";
    payload += "\"rele\":" + String(releVal) + ",";
    payload += "\"estado\":\"" + String(estadoTexto) + "\",";
    payload += "\"acelerometro\":{\"pitch\":0.0,\"roll\":0.0,\"pescada_detectada\":" + String(alarmeAtivo ? "true" : "false") + "}";
    payload += "}";

    http.POST(payload);
    http.end();
  }
}
