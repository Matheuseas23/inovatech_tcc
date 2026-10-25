#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <math.h>
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

WiFiMulti wifiMulti;
TwoWire WireOled = TwoWire(1);
Adafruit_SSD1306 oled(128, 64, &WireOled, -1, 400000UL, 400000UL);
bool oledOk = false;
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

// 4. Wi-Fi de casa (TP-Link, a ESP usa a faixa de 2,4 GHz):
const char* WIFI_TPLINK_SSID = "TP-Link_5E68";
const char* WIFI_TPLINK_PASS = "GQW5Yt5N";

// true = conecta SOMENTE no ponto de acesso do Redmi (usado na apresentação do CPIC).
// false = qualquer rede conhecida; o WiFiMulti escolhe a de melhor sinal.
const bool SOMENTE_WIFI_REDMI = false;

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
const uint8_t PINO_RELE_GND  = 13; // GPIO 13 (D13) - GND Virtual Relé (as bobinas puxam ~70 mA cada: o ideal é GND de verdade)
const uint8_t PINO_BUZZER_GND = 33; // GPIO 33 (D33) - GND virtual do buzzer: libera o GND físico para o relé
const uint8_t PINO_LED_REDE  = 2;  // GPIO 2 - LED azul da placa (status da conexão sem cabo)

// LED RGB de status nos óculos (fios: R laranja, comum cinza, G verde, B roxo)
const uint8_t PINO_LED_R      = 26; // GPIO 26 - vermelho (fio laranja, resistor 220 Ω)
const uint8_t PINO_LED_G      = 4;  // GPIO 4  - verde (fio verde, resistor 100 Ω; o D33 está com o IR)
const uint8_t PINO_LED_B      = 27; // GPIO 27 - azul (fio roxo, resistor 100 Ω)
const uint8_t PINO_LED_COMUM  = 14; // GPIO 14 - perna comum, a mais comprida (fio cinza)
const bool LED_RGB_ANODO_COMUM = false; // true se for ânodo comum (cores invertidas no autoteste)

// Acelerômetro GY-521 (I2C + Alimentação Virtual)
const uint8_t PINO_MPU_SDA   = 21; // GPIO 21 - Linha SDA I2C
const uint8_t PINO_MPU_SCL   = 22; // GPIO 22 - Linha SCL I2C
const uint8_t PINO_MPU_VCC   = 23; // GPIO 23 - VCC Virtual (3.3V HIGH): hoje alimenta o display OLED (VDD)
// 24/10/2026: VCC do GY-521 passou para o pino 3V3 (alimentação forte). Com isso a ESP32 não corta
// mais a energia dele pelo D23 ao destravar o I2C. Se voltar o VCC para o D23, troque para false.
const bool MPU_VCC_NO_3V3      = true;
const uint8_t PINO_MPU_GND   = 16; // GPIO 16 (RX2) - GND Virtual (0V LOW)

// Display OLED 0,96" (SSD1306) no meio dos óculos, virado para fora. Usa o SEGUNDO barramento I2C
// da ESP32 (pinos próprios): se o display soltar, o acelerômetro não é afetado. Atualizado no Core 0.
const uint8_t PINO_OLED_SDA = 17; // TX2 - SDA
const uint8_t PINO_OLED_SCL = 5;  // D5  - SCK/SCL
const uint8_t PINO_OLED_VCC = 23; // D23 - VDD (VCC virtual; o acelerômetro foi para o 3V3)
const uint8_t PINO_OLED_GND = 15; // D15 - GND virtual (todos os GND físicos estão ocupados)

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
// Filtro Temporal de Segurança: 1,5 s de tolerância. Linha do tempo da cabeça baixa:
//   1,0 s -> 1 bip rápido de aviso + "tec" do Relé 2 (pisca-alerta arma)
//   1,5 s -> bip contínuo + "tec" do Relé 1 (safe-stop)
//   cabeça erguida -> buzzer para e os dois relés soltam juntos ("tec")
const unsigned long LIMIAR_CABECA_BAIXA_MS = 1500;
const unsigned long BIP_AVISO_INICIO_MS    = 1000;
const unsigned long BIP_AVISO_FIM_MS       = 1150; // Bip rápido (150 ms) + 350 ms de silêncio antes do contínuo

// Acelerômetro: ângulo medido em relação à postura calibrada (cabeça erguida).
// Funciona em qualquer orientação de montagem do GY-521 na haste dos óculos.
const float LIMIAR_INCLINACAO_GRAUS = 32.0;  // Cabeça caída além disso = cabeça baixa
const float LIMIAR_RETORNO_GRAUS    = 25.0;  // Depois de baixa, só volta a "erguida" abaixo disso (sem liga-desliga)
const float PESCADA_VARIACAO_GRAUS  = 15.0;  // Queda em 100 ms (~150°/s) = pescada brusca
const float PESCADA_INCLINACAO_MIN  = 25.0;  // ...desde que termine a pelo menos 25°
const bool  PESCADA_DISPARA_ALARME  = true;  // Pescada dispara o alarme na hora
const unsigned long ALARME_MINIMO_PESCADA_MS = 2000; // Alarme de pescada dura no mínimo 2s

// Pegar objeto x sono (giroscópio): sono = cabeça baixa e PARADA; pegar um objeto = cabeça
// baixa SE MEXENDO. Enquanto a cabeça se mexe de propósito, a contagem de 1,5 s não anda.
// Medido em 24/10/2026 com os óculos no rosto: cabeça inclinada e parada = até 23 °/s;
// procurando algo mexendo a cabeça = 44 a 77 °/s. 35 °/s fica no meio, com folga dos dois lados.
const float LIMIAR_MOVIMENTO_GRAUS_S    = 35.0; // Rotação acima disso = movimento voluntário
const unsigned long JANELA_MOVIMENTO_MS = 600;  // Depois de mexer, espera a cabeça assentar
const unsigned long CONFIRMA_PESCADA_MS = 500;  // Pescada vale se a cabeça assentar parada em até 0,5 s

// Calibração guiada por bipes (sem PC por perto, ex.: power bank nas costas):
// ao ligar, 1 bip -> 4 s para pôr os óculos e olhar para frente -> 2 bips = calibrado.
// Cabeça mexendo durante a medição = 1 bip longo e nova tentativa em 2 s.
const unsigned long ESPERA_CALIBRACAO_MS     = 4000;
const unsigned long NOVA_TENTATIVA_CALIB_MS  = 2000;
const float AGITACAO_MAX_CALIBRACAO_G        = 0.08; // Cabeça parada fica bem abaixo disso

// TESTE (23/10/2026): os sensores só valem com o monitoramento ativado no app do motorista.
// Ao ativar, o buzzer toca "pan, pan-ran-ran-pan... pan, pan" e os sensores entram em ação.
// Para tirar a função (sensores sempre ligados), troque para false.
const bool SENSORES_SO_COM_MONITORAMENTO = true;
// À prova de falha: 15 s sem falar com nenhum servidor (PC ou nuvem) = sensores ligam sozinhos
const unsigned long FAILSAFE_SEM_CONTATO_MS = 15000;

// Sensor IR = reserva do acelerômetro. Só é confiável se leu "cabeça erguida" no último
// minuto: sem alimentação ele lê LOW direto e seguraria o alarme ligado para sempre.
const unsigned long IR_SINAL_VALIDO_MS = 60000;

const unsigned long DURACAO_MAX_TESTE_MS     = 30000; // Teste remoto se desliga sozinho em 30s
const unsigned long INTERVALO_TELEMETRIA_MS  = 200;   // 5 pacotes/s em operação normal
const unsigned long INTERVALO_TELEMETRIA_ALERTA_MS = 100; // 10 pacotes/s durante alertas
const unsigned long TIMEOUT_SERVIDOR_LOCAL_MS = 5000;
const unsigned long INTERVALO_NUVEM_MS        = 1000;
// Durante a contagem (1,5 s) e o alarme a nuvem recebe mais rápido; e TODA mudança de estado
// (normal -> contando -> alarme -> normal) é enviada na hora, senão o app nunca via a contagem
const unsigned long INTERVALO_NUVEM_ALERTA_MS = 300;

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
  bool monitorando;
  float giro;
  bool diagnostico;
  bool calibrando;     // Calibração guiada em andamento (para a tela OLED)
  bool claudePresente; // Claude acessando a placa pela serial (para a tela OLED)
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, 0, 0.0, 0.0, 0.0, false, false, false, false, "NORMAL", false, false, false, 0.0, false};
portMUX_TYPE mutexDados = portMUX_INITIALIZER_UNLOCKED;

// Comandos recebidos pela rede (Core 0) e executados no Core 1
const uint8_t CMD_TESTE_ALARME = 0x01;
const uint8_t CMD_SILENCIAR    = 0x02;
const uint8_t CMD_TOGGLE_RELE1 = 0x04;
const uint8_t CMD_TOGGLE_RELE2 = 0x08;
const uint8_t CMD_CALIBRAR     = 0x10;
// Botão Desligar/Ligar do app ("pwr":0/1): repouso com componentes desligados, mas Wi-Fi ligado
// para ouvir o "Ligar". Também liga com o botão EN ou reconectando o cabo.
volatile int8_t energiaServidor = -1;    // -1 = ainda sem resposta do servidor
bool emRepouso = false;
volatile unsigned long desligandoMs = 0; // Quando começou o desligamento (o display mostra a despedida)
volatile bool oledApagado = false;       // O display já se despediu e apagou
volatile bool religarDisplay = false;    // Saiu do repouso: o display liga e mostra a carinha "OLÁ"
volatile uint8_t comandosRemotos = 0;
portMUX_TYPE mutexComandos = portMUX_INITIALIZER_UNLOCKED;

// Status da rede (escrito pelo Core 0, exibido no LED pelo Core 1):
// pisca rápido = sem comunicação · pisca devagar = via nuvem (Render) · aceso = servidor do PC
const uint8_t REDE_SEM_COMUNICACAO = 0;
const uint8_t REDE_NUVEM           = 1;
const uint8_t REDE_SERVIDOR_LOCAL  = 2;
volatile uint8_t statusRede = REDE_SEM_COMUNICACAO;

// Monitoramento ligado no app: chega como "mon":1 / "mon":0 nas respostas do servidor (-1 = sem resposta ainda)
volatile int8_t monitoramentoServidor = -1;
// Modo exibição ligado no app do motorista ("exib":1): o display mostra olhos piscando e olhando em volta
volatile int8_t modoExibicaoServidor = 0;
volatile bool semContatoServidor = false; // Escrito pelo Core 0 (à prova de falha)

// Modo checagem (comando serial DIAG:1 / DIAG:0): bip de monitor cardíaco a cada 1 s e sensores
// suspensos (nada de alarme). Desliga sozinho em 15 min por segurança.
const unsigned long DURACAO_MAX_DIAGNOSTICO_MS = 15UL * 60UL * 1000UL;
volatile bool modoDiagnostico = false;
volatile unsigned long inicioDiagnosticoMs = 0;
volatile bool pedidoScanWifi = false; // Comando serial SCAN: lista as redes que a ESP32 enxerga
volatile unsigned long arcoIrisAteMs = 0; // Comando serial ARCOIRIS: teste do LED RGB por 10 s
volatile bool pedidoDiagI2C = false;      // Comando serial I2C: diagnóstico + destravamento do acelerômetro
volatile unsigned long bipOlaAteMs = 0;   // Comando serial CLAUDE:OLA: bip de 0,5 s de "sinal de vida"
unsigned long verdeAtivadoMs = 0;         // Quando o monitoramento foi ativado (LED verde que apaga devagar)
// Claude na ESP (CLAUDE:OLA / CLAUDE:TCHAU pela serial): LED pisca e fica no laranja do Claude.
// Sem comando nenhum por 3 min, a placa entende que ele saiu e volta ao normal.
volatile unsigned long claudeChegouMs = 0;
volatile unsigned long claudeUltimoMs = 0;
const unsigned long CLAUDE_AUSENTE_MS = 180000;
const unsigned long VERDE_ACESO_MS = 1500; // LED verde aceso ao ativar o monitoramento...
const unsigned long VERDE_FADE_MS  = 3000; // ...e depois apaga devagar nesse tempo
volatile int8_t pedidoModoLed = -1;       // Comandos seriais LED:DIG (1) / LED:PWM (0)
bool ledModoDigital = false;              // true = LED só liga/desliga, sem PWM (sem ruído no I2C)
uint8_t tentativasMpu = 0;                // Tentativas seguidas de reconectar o acelerômetro sem sucesso
bool wireIniciado = false;
// Modo econômico: o GY-521 caía ao ligar o giroscópio (alimentação fraca pelo D23). Com o giroscópio
// em standby a detecção de cabeça baixa e pescada segue igual; só o filtro "pegar objeto" fica sem efeito.
bool giroDesligado = false;

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
bool monitoramentoAtivo              = false; // Estado do monitoramento já aplicado no Core 1
bool melodiaPendente                 = false; // Toca o ritmo antes de liberar os sensores

// Acelerômetro
struct LeituraMPU {
  bool valida;
  float pitch;
  float roll;
  float inclinacao;
  bool pescada;
  float giro; // Velocidade de rotação da cabeça em qualquer direção (°/s, filtrada)
};

bool mpuDetectado             = false;
bool mpuCalibrado             = false;
unsigned long ultimaTentativaMpu = 0;
unsigned long calibracaoAgendadaMs = 0;    // 0 = nenhuma calibração guiada pendente
float gFiltX = 0, gFiltY = 0, gFiltZ = 1;  // Vetor gravidade filtrado (em g)
float gRefX = 0, gRefY = 0, gRefZ = 1;     // Vetor de referência (cabeça erguida)
float rotRef[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // Leva a postura calibrada para o eixo Z
float historicoInclinacao[10] = {0};       // 10 amostras x 10 ms = janela de 100 ms
uint8_t indiceHistorico = 0;
float biasGx = 0, biasGy = 0, biasGz = 0;  // Desvio de fábrica do giroscópio (medido na calibração)
float giroFiltrado = 0;                    // °/s
bool cabecaInclinada               = false; // Ângulo além do limite (com histerese)
unsigned long ultimoMovimentoMs    = 0;     // Última vez que a cabeça se mexeu de propósito
unsigned long pescadaCandidataMs   = 0;     // Queda brusca aguardando a cabeça assentar

// Protótipos de Funções
void aplicarAtuadores(bool buzzer, bool rele1, bool rele2);
void processarComandosSerial();
void aplicarComandosRemotos();
void agendarComando(uint8_t cmd);
void interpretarComandosTexto(const char* texto);
void taskTelemetria(void *pvParameters);
bool inicializarMPU6050();
bool lerMPUBruto(float &ax, float &ay, float &az, float &gx, float &gy, float &gz);
bool calibrarMPU6050();
void calibrarComAviso();
void bipAviso(uint8_t vezes, unsigned int duracaoMs);
void tocarMelodiaPronto();
void tocarDespertador();
void corLed(uint8_t r, uint8_t g, uint8_t b);
void tecAspas();
LeituraMPU lerMPU6050(unsigned long agora);
void recuperarBarramentoMPU();
void diagnosticarI2C();
void iniciarOled();
void entrarRepouso();
void sairRepouso();
bool atualizarOled(const DadosTelemetria &d, bool conectado);
void taskDisplay(void *pvParameters);

// ------------------------------------------------------------------------------
// 5. SETUP INICIAL
// ------------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  // Configuração dos pinos com pull-up interno no sensor
  pinMode(PINO_SENSOR_IR, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  // Buzzer mais alto: corrente máxima do pino (~40 mA, o padrão é ~20 mA)
  gpio_set_drive_capability((gpio_num_t)PINO_BUZZER, GPIO_DRIVE_CAP_3);
  pinMode(PINO_RELE_1, OUTPUT);
  pinMode(PINO_RELE_2, OUTPUT);
  pinMode(PINO_LED_REDE, OUTPUT);

  // LED RGB: perna comum no GND virtual (ou no + se for ânodo comum); cores por PWM com a
  // corrente limitada no pino (~10 mA), seguro mesmo se alguma cor ficar sem resistor
  pinMode(PINO_LED_COMUM, OUTPUT);
  digitalWrite(PINO_LED_COMUM, LED_RGB_ANODO_COMUM ? HIGH : LOW);
  for (uint8_t pino : {PINO_LED_R, PINO_LED_G, PINO_LED_B}) {
    ledcAttach(pino, 5000, 8);
    gpio_set_drive_capability((gpio_num_t)pino, GPIO_DRIVE_CAP_1);
  }
  // Autoteste ao ligar: vermelho, verde, azul
  corLed(255, 0, 0); delay(250);
  corLed(0, 255, 0); delay(250);
  corLed(0, 0, 255); delay(250);
  corLed(0, 0, 0);

  // GND Virtual (0V) no Pino D13 para o terra do Módulo Relé
  pinMode(PINO_RELE_GND, OUTPUT);
  digitalWrite(PINO_RELE_GND, LOW);
  gpio_set_drive_capability((gpio_num_t)PINO_RELE_GND, GPIO_DRIVE_CAP_3); // Força máxima para as bobinas

  // GND virtual do buzzer no D33 (o buzzer gasta pouco; o GND físico fica para o relé)
  pinMode(PINO_BUZZER_GND, OUTPUT);
  digitalWrite(PINO_BUZZER_GND, LOW);
  gpio_set_drive_capability((gpio_num_t)PINO_BUZZER_GND, GPIO_DRIVE_CAP_3);

  // Alimentação Virtual do Acelerômetro GY-521:
  // VCC Virtual no D23 (3.3V)
  pinMode(PINO_MPU_VCC, OUTPUT);
  digitalWrite(PINO_MPU_VCC, HIGH);

  // GND Virtual no RX2 / GPIO 16 (0V)
  pinMode(PINO_MPU_GND, OUTPUT);
  digitalWrite(PINO_MPU_GND, LOW);

  // Força máxima nos pinos de energia do GY-521: ao acordar (giroscópio ligado) ele puxa mais
  // corrente e, com pino fraco + mau contato, a tensão caía e o módulo reiniciava sozinho
  gpio_set_drive_capability((gpio_num_t)PINO_MPU_VCC, GPIO_DRIVE_CAP_3);
  gpio_set_drive_capability((gpio_num_t)PINO_MPU_GND, GPIO_DRIVE_CAP_3);

  // Display OLED: GND virtual no D15 (o VDD no D23 já está em HIGH acima) e tela de abertura
  pinMode(PINO_OLED_GND, OUTPUT);
  digitalWrite(PINO_OLED_GND, LOW);
  gpio_set_drive_capability((gpio_num_t)PINO_OLED_GND, GPIO_DRIVE_CAP_3);
  iniciarOled();

  aplicarAtuadores(false, false, false);

  // Aguarda 150ms para o sensor GY-521 estabilizar a alimentação
  delay(150);

  // Religa o acelerômetro do zero e inicia o I2C (D21 SDA / D22 SCL): um reset da ESP32 não
  // corta a energia dele, que pode ter ficado travado no meio de uma leitura
  recuperarBarramentoMPU();
  inicializarMPU6050();

  // Calibração guiada: 1 bip agora; a medição acontece no loop() depois da espera
  calibracaoAgendadaMs = millis() + ESPERA_CALIBRACAO_MS;
  bipAviso(1, 120);

  Serial.println(F("\n=================================================="));
  Serial.println(F("     INOVA TECH - SMART DRIVER (SISTEMA ATIVO)    "));
  Serial.println(F("   Monitoramento de Postura e Fadiga Veicular     "));
  Serial.println(F("=================================================="));
  Serial.println(F("[HARDWARE] Sensor IR: GPIO 32 | Buzzer: GPIO 25"));
  Serial.println(F("[HARDWARE] Rele 1: GPIO 18 | Rele 2: GPIO 19 | GND Rele: GPIO 13"));
  Serial.println(F("[HARDWARE] LED RGB: R=GPIO 26 | G=GPIO 4 | B=GPIO 27 | comum=GPIO 14"));
  Serial.printf("[HARDWARE] Acelerometro GY-521: SDA=%d, SCL=%d, VCC=%d, GND=%d (RX2)\n",
                PINO_MPU_SDA, PINO_MPU_SCL, PINO_MPU_VCC, PINO_MPU_GND);
  Serial.printf("[HARDWARE] Status MPU-6050: %s\n", mpuDetectado ? "CONECTADO (calibracao guiada em 4 s)" : "AGUARDANDO CONEXAO");
  Serial.printf("[SEGURANCA] Tolerancia: %lu ms | Inclinacao maxima: %.0f graus\n",
                LIMIAR_CABECA_BAIXA_MS, LIMIAR_INCLINACAO_GRAUS);
  Serial.println(F("[DICA] Ao ligar: 1 bip -> olhe para frente parado -> 2 bips = acelerometro calibrado."));

  // Modo Station puro com reconexão resiliente
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false); // Evita gravar na flash a cada reconexão
  // Sem economia de energia no rádio: comandos chegam sem atraso e o consumo constante
  // impede que o power bank se desligue sozinho por "carga baixa"
  WiFi.setSleep(false);

  // Registra o motivo de cada queda do Wi-Fi (diagnóstico do ponto de acesso do celular)
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    uint8_t m = info.wifi_sta_disconnected.reason;
    const char* txt = m == 201 ? "rede nao encontrada (ponto de acesso desligado ou em 5 GHz?)"
                    : m == 200 ? "sinal perdido (o ponto de acesso sumiu ou trocou de canal)"
                    : (m == 210 || m == 211) ? "seguranca incompativel (use WPA2 no ponto de acesso)"
                    : (m == 2 || m == 15 || m == 202 || m == 204) ? "falha de senha/autenticacao"
                    : m == 8 ? "desconectado pela propria ESP32"
                    : "outro motivo";
    Serial.printf("[WIFI] Caiu: motivo %u - %s\n", m, txt);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Registra as redes conhecidas (o WiFiMulti escolhe a de melhor sinal).
  // SOMENTE_WIFI_REDMI = true: conecta só no ponto de acesso do Redmi (apresentação).
  wifiMulti.addAP(WIFI_REDMI_SSID, WIFI_REDMI_PASS);
  if (!SOMENTE_WIFI_REDMI) {
    wifiMulti.addAP(WIFI_TPLINK_SSID, WIFI_TPLINK_PASS);
    wifiMulti.addAP(WIFI_CASA_SSID, WIFI_CASA_PASS);
    wifiMulti.addAP(WIFI_IPHONE_SSID, WIFI_IPHONE_PASS);
  }

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

  // Tarefa do display OLED (Core 0): animações da carinha, abertura, caminhão/ônibus e status
  xTaskCreatePinnedToCore(taskDisplay, "TaskDisplay", 6144, NULL, 1, NULL, 0);
}

// ------------------------------------------------------------------------------
// 6. INICIALIZAÇÃO, CALIBRAÇÃO E LEITURA DO ACELERÔMETRO MPU-6050 (I2C)
// ------------------------------------------------------------------------------
// Destrava o barramento I2C do acelerômetro: corta a energia dele pelo VCC virtual (D23), dá 9
// pulsos de clock e um STOP. Resolve o "responde mas não lê": MPU-6050 preso segurando o SDA
// depois de um reset ou de um fio mexido no meio de uma leitura. Bloqueia ~0,4 s.
void recuperarBarramentoMPU() {
  if (wireIniciado) Wire.end();
  if (!MPU_VCC_NO_3V3) {
    // Linhas em 0 V junto com o VCC: o módulo não fica meio-alimentado pelos pull-ups
    pinMode(PINO_MPU_SDA, OUTPUT); digitalWrite(PINO_MPU_SDA, LOW);
    pinMode(PINO_MPU_SCL, OUTPUT); digitalWrite(PINO_MPU_SCL, LOW);
    digitalWrite(PINO_MPU_VCC, LOW);
    delay(250);
    digitalWrite(PINO_MPU_VCC, HIGH);
    delay(100);
  }
  // No 3V3 não dá para cortar a energia: só destrava o barramento (9 pulsos de clock + STOP)
  pinMode(PINO_MPU_SCL, OUTPUT); digitalWrite(PINO_MPU_SCL, LOW);
  pinMode(PINO_MPU_SDA, INPUT_PULLUP);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(PINO_MPU_SCL, HIGH); delayMicroseconds(10);
    digitalWrite(PINO_MPU_SCL, LOW);  delayMicroseconds(10);
  }
  pinMode(PINO_MPU_SDA, OUTPUT); digitalWrite(PINO_MPU_SDA, LOW); delayMicroseconds(10);
  digitalWrite(PINO_MPU_SCL, HIGH); delayMicroseconds(10);
  digitalWrite(PINO_MPU_SDA, HIGH); delayMicroseconds(10);
  // 100 kHz (padrão I2C): mais robusto com fios fêmea longos e os fios do LED passando junto
  Wire.begin(PINO_MPU_SDA, PINO_MPU_SCL, 100000);
  Wire.setTimeOut(20); // Nunca deixa um fio solto travar o Core 1
  wireIniciado = true;
}

// Comando serial I2C: estado das linhas, quem responde no barramento e o WHO_AM_I do módulo
void diagnosticarI2C() {
  Wire.end();
  pinMode(PINO_MPU_SDA, INPUT_PULLUP);
  pinMode(PINO_MPU_SCL, INPUT_PULLUP);
  delay(2);
  int sda = digitalRead(PINO_MPU_SDA), scl = digitalRead(PINO_MPU_SCL);
  Wire.begin(PINO_MPU_SDA, PINO_MPU_SCL, 100000);
  Wire.setTimeOut(20);
  Serial.printf("[I2C] Linhas em repouso: SDA=%s SCL=%s (o certo e ALTO/ALTO)\n",
                sda ? "ALTO" : "BAIXO (preso!)", scl ? "ALTO" : "BAIXO (preso!)");

  // Teste da alimentação: com o D23 (VCC) desligado, o módulo NÃO pode responder. Se responder,
  // o fio VCC não leva energia e ele vive só da corrente que vaza pelos fios de dados (SDA/SCL)
  if (MPU_VCC_NO_3V3) {
    Serial.println(F("[I2C] Teste de energia: VCC do modulo no pino 3V3 (alimentacao fixa, sem teste)"));
  } else {
    digitalWrite(PINO_MPU_VCC, LOW);
    delay(200);
    Wire.beginTransmission(MPU6050_ADDR);
    bool respondeSemVcc = Wire.endTransmission() == 0;
    digitalWrite(PINO_MPU_VCC, HIGH);
    delay(100);
    Serial.printf("[I2C] Teste de energia: com o D23 (VCC) desligado o modulo %s\n",
                  respondeSemVcc ? "AINDA RESPONDE -> o fio VCC NAO esta levando energia (confira VCC <-> D23)"
                                 : "parou de responder -> fio VCC no D23 OK");
  }
  // Mesmo teste para o GND virtual (RX2): sem ele em 0 V, o módulo também não pode responder
  digitalWrite(PINO_MPU_GND, HIGH);
  delay(200);
  Wire.beginTransmission(MPU6050_ADDR);
  bool respondeSemGnd = Wire.endTransmission() == 0;
  digitalWrite(PINO_MPU_GND, LOW);
  delay(100);
  Serial.printf("[I2C] Teste de terra: com o RX2 (GND) desligado o modulo %s\n",
                respondeSemGnd ? "AINDA RESPONDE -> o GND do modulo NAO esta no RX2 (esta pegando terra por outro caminho)"
                               : "parou de responder -> fio GND no RX2 OK");
  uint8_t achados = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (achados < 6) Serial.printf("[I2C] Respondeu no endereco 0x%02X\n", a);
      achados++;
    }
  }
  if (achados == 0) Serial.println(F("[I2C] Ninguem respondeu: sem energia (VCC no D23, GND no RX2) ou SDA/SCL soltos."));
  else if (achados > 3) Serial.printf("[I2C] %u enderecos 'responderam': linha SDA presa em 0 V (travada ou em curto).\n", achados);
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x75); // WHO_AM_I
  uint8_t erro = Wire.endTransmission(false);
  uint8_t n = Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)1, true);
  int quem = n ? Wire.read() : -1;
  if (quem < 0) Serial.printf("[I2C] WHO_AM_I: sem resposta (erro %u) -> leitura de dados falha\n", erro);
  else Serial.printf("[I2C] WHO_AM_I = 0x%02X (erro %u) (MPU-6050 original = 0x68; clones: 0x70, 0x72, 0x98)\n", quem, erro);

  // Leitura completa usada pelo firmware (14 bytes a partir de 0x3B), com o módulo dormindo e acordado
  auto lerBloco = [](const char* quando) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(0x3B);
    uint8_t e = Wire.endTransmission(false);
    uint8_t q = Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)14, true);
    while (Wire.available()) Wire.read();
    Serial.printf("[I2C] Leitura de 14 bytes (%s): erro=%u bytes=%u -> %s\n", quando, e, q,
                  (e == 0 && q == 14) ? "OK" : "FALHOU");
  };
  lerBloco("como esta");
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B); Wire.write(0x00); // PWR_MGMT_1: acorda (liga o giroscópio)
  uint8_t eAcordar = Wire.endTransmission(true);
  // Depois de acordar: quem responde em 0x68 e 0x69 ao longo do tempo (0x69 = pino AD0 encostando no +)
  for (uint16_t ms : {5, 50, 300, 1000}) {
    static unsigned long base; if (ms == 5) base = millis();
    while (millis() - base < ms) delay(1);
    Wire.beginTransmission(0x68); uint8_t e68 = Wire.endTransmission();
    Wire.beginTransmission(0x69); uint8_t e69 = Wire.endTransmission();
    Serial.printf("[I2C]   %4u ms depois de acordar: 0x68=%s 0x69=%s\n", ms, e68 == 0 ? "responde" : "--", e69 == 0 ? "responde" : "--");
  }
  Wire.beginTransmission(MPU6050_ADDR);
  uint8_t eSonda = Wire.endTransmission();
  Serial.printf("[I2C] Acordar o modulo: erro=%u | ainda responde depois de acordar: %s\n", eAcordar, eSonda == 0 ? "sim" : "NAO (caiu ao acordar)");
  lerBloco("acordado");
}

bool inicializarMPU6050() {
  Wire.beginTransmission(MPU6050_ADDR);
  if (Wire.endTransmission() != 0) {
    mpuDetectado = false;
    return false;
  }

  // Giroscópio em standby no modo econômico (definido ANTES de acordar: o módulo já acorda gastando pouco)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6C); // PWR_MGMT_2
  Wire.write(giroDesligado ? 0x07 : 0x00);
  Wire.endTransmission(true);

  // Acorda o MPU-6050 (limpa o registrador PWR_MGMT_1)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission(true);

  // Confere se ele aguentou acordar; se caiu, religa em modo econômico (só acelerômetro)
  delay(30);
  Wire.beginTransmission(MPU6050_ADDR);
  if (Wire.endTransmission() != 0) {
    if (giroDesligado) { mpuDetectado = false; return false; }
    giroDesligado = true;
    Serial.println(F("[MPU-6050] Caiu ao ligar o giroscopio (energia fraca). Modo economico: so acelerometro."));
    recuperarBarramentoMPU();
    return inicializarMPU6050();
  }

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

  // Fundo de escala do giroscópio (+/- 250 °/s)
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B); // GYRO_CONFIG
  Wire.write(0x00);
  Wire.endTransmission(true);

  mpuDetectado = true;
  return true;
}

// Lê acelerômetro (g) e giroscópio (°/s) numa única transação I2C de 14 bytes
bool lerMPUBruto(float &ax, float &ay, float &az, float &gx, float &gy, float &gz) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B); // Registrador ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)MPU6050_ADDR, (size_t)14, true) != 14) return false;

  int16_t rx = (Wire.read() << 8) | Wire.read();
  int16_t ry = (Wire.read() << 8) | Wire.read();
  int16_t rz = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read(); // Temperatura (não usada)
  int16_t qx = (Wire.read() << 8) | Wire.read();
  int16_t qy = (Wire.read() << 8) | Wire.read();
  int16_t qz = (Wire.read() << 8) | Wire.read();
  ax = rx / 16384.0;
  ay = ry / 16384.0;
  az = rz / 16384.0;
  gx = qx / 131.0;
  gy = qy / 131.0;
  gz = qz / 131.0;
  return true;
}

// Grava a postura atual (cabeça erguida) como referência zero. Bloqueia ~0,5s.
// Retorna false se a leitura falhar ou a cabeça estiver mexendo (mantém a referência anterior).
bool calibrarMPU6050() {
  float somaX = 0, somaY = 0, somaZ = 0, somaQuadrados = 0;
  float somaGx = 0, somaGy = 0, somaGz = 0;
  int amostras = 0;
  for (int i = 0; i < 50; i++) {
    float ax, ay, az, gx, gy, gz;
    if (lerMPUBruto(ax, ay, az, gx, gy, gz)) {
      somaX += ax; somaY += ay; somaZ += az;
      somaQuadrados += ax * ax + ay * ay + az * az;
      somaGx += gx; somaGy += gy; somaGz += gz;
      amostras++;
    }
    delay(10);
  }
  float n = max(amostras, 1);
  float mx = somaX / n;
  float my = somaY / n;
  float mz = somaZ / n;
  float modulo = sqrt(mx * mx + my * my + mz * mz);
  // Quanto as leituras variaram em torno da média (em g): mede se a cabeça ficou parada
  float agitacao = sqrt(max(somaQuadrados / n - modulo * modulo, 0.0f));

  // Gravidade medida tem que dar ~1g; fora disso o módulo está com defeito/solto
  if (amostras < 40 || modulo < 0.6 || modulo > 1.4) {
    Serial.println(F("[MPU-6050] Falha na calibracao (leitura instavel). Verifique os fios."));
    return false;
  }
  if (agitacao > AGITACAO_MAX_CALIBRACAO_G) {
    Serial.printf("[MPU-6050] Falha na calibracao: cabeca mexendo (%.2f g). Fique parado.\n", agitacao);
    return false;
  }

  gRefX = mx / modulo; gRefY = my / modulo; gRefZ = mz / modulo;
  gFiltX = gRefX; gFiltY = gRefY; gFiltZ = gRefZ;
  // Cabeça parada: a média do giroscópio é o desvio de fábrica, descontado nas leituras
  biasGx = somaGx / n; biasGy = somaGy / n; biasGz = somaGz / n;
  giroFiltrado = 0;
  // Rotação mínima que leva a gravidade da postura calibrada até o eixo Z (Rodrigues).
  // Assim pitch e roll partem de 0° com a cabeça erguida, em qualquer montagem do sensor
  // (preso de pé na haste, o cálculo direto do roll ficava instável e pulava até 280°).
  float vx = gRefY, vy = -gRefX, c = gRefZ;  // v = gRef x Z ; c = gRef . Z
  if (c < -0.999f) {
    // Sensor de cabeça para baixo: giro de 180° em torno de X
    rotRef[0][0] = 1; rotRef[0][1] = 0;  rotRef[0][2] = 0;
    rotRef[1][0] = 0; rotRef[1][1] = -1; rotRef[1][2] = 0;
    rotRef[2][0] = 0; rotRef[2][1] = 0;  rotRef[2][2] = -1;
  } else {
    float k = 1.0f / (1.0f + c);
    rotRef[0][0] = 1 - vy * vy * k; rotRef[0][1] = vx * vy * k;     rotRef[0][2] = vy;
    rotRef[1][0] = vx * vy * k;     rotRef[1][1] = 1 - vx * vx * k; rotRef[1][2] = -vx;
    rotRef[2][0] = -vy;             rotRef[2][1] = vx;              rotRef[2][2] = 1 - (vx * vx + vy * vy) * k;
  }
  for (uint8_t i = 0; i < 10; i++) historicoInclinacao[i] = 0;
  mpuCalibrado = true;
  Serial.println(F("[MPU-6050] Calibrado: postura atual registrada como cabeca erguida."));
  return true;
}

// Bipes curtos de aviso. Bloqueiam o Core 1 por menos de 1 s (só durante a calibração).
void bipAviso(uint8_t vezes, unsigned int duracaoMs) {
  for (uint8_t i = 0; i < vezes; i++) {
    if (i) delay(150);
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
    delay(duracaoMs);
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
  }
}

// "Pan, pan-ran-ran-pan... pan, pan" (ritmo "Shave and a Haircut"). O buzzer é ativo (uma nota só),
// então a música é o ritmo. Toca ao ativar o monitoramento no app: sensores prontos. Dura ~2 s.
void tocarMelodiaPronto() {
  tecAspas();
  const uint16_t notas[][2] = {  // {buzzer ligado, silêncio depois} em ms
    {200, 100},  // pan
    {100, 50},   // pan
    {100, 50},   // ran
    {200, 100},  // ran
    {200, 400},  // pan  (+ pausa)
    {200, 100},  // pan
    {250, 0}     // pan
  };
  for (const auto &nota : notas) {
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
    delay(nota[0]);
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
    delay(nota[1]);
  }
}

// Despertador digital clássico ("pi-pi-pi-pi"): toca ao desativar o monitoramento no app
void tocarDespertador() {
  tecAspas();
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_LIGADO);
    delay(80);
    digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
    delay(70);
  }
}

// "Tec" de aspas: pulso curto nos dois relés juntos, abrindo os sons de ativar/desativar
// o monitoramento. O loop() restaura logo depois o estado certo dos relés.
void tecAspas() {
  digitalWrite(PINO_RELE_1, NIVEL_RELE_LIGADO);
  digitalWrite(PINO_RELE_2, NIVEL_RELE_LIGADO);
  delay(60);
  digitalWrite(PINO_RELE_1, NIVEL_RELE_DESLIGADO);
  digitalWrite(PINO_RELE_2, NIVEL_RELE_DESLIGADO);
  delay(60);
}

// Calibra e avisa pelo buzzer: 2 bips curtos = ok; 1 bip longo = falhou (tenta de novo sozinho)
void calibrarComAviso() {
  calibracaoAgendadaMs = 0;
  if (!mpuDetectado) return; // lerMPU6050() agenda a calibração quando o módulo aparecer
  if (calibrarMPU6050()) {
    bipAviso(2, 100);
  } else {
    bipAviso(1, 700);
    calibracaoAgendadaMs = millis() + NOVA_TENTATIVA_CALIB_MS;
  }
}

LeituraMPU lerMPU6050(unsigned long agora) {
  LeituraMPU r = {false, 0.0, 0.0, 0.0, false, 0.0};

  if (!mpuDetectado) {
    // Tenta reconectar no máximo 1x por segundo (não sobrecarrega o barramento I2C)
    if (agora - ultimaTentativaMpu < 1000) return r;
    ultimaTentativaMpu = agora;
    // A cada 5 tentativas sem leitura, religa o módulo e destrava o I2C
    if (++tentativasMpu % 5 == 0) {
      Serial.println(F("[MPU-6050] Sem leitura: religando o modulo e destravando o I2C..."));
      recuperarBarramentoMPU();
    }
    // Cada reconexão tenta de novo com o giroscópio ligado (o modo econômico só volta se ele cair)
    giroDesligado = false;
    if (!inicializarMPU6050()) return r;
    Serial.println(F("[MPU-6050] Modulo detectado."));
    // Sem referência ainda: calibra com aviso. Se já tinha, mantém (o sensor segue preso no óculos)
    if (!mpuCalibrado && !calibracaoAgendadaMs) calibracaoAgendadaMs = agora + 1000;
  }

  float ax, ay, az, gx, gy, gz;
  if (!lerMPUBruto(ax, ay, az, gx, gy, gz)) {
    mpuDetectado = false;
    ultimaTentativaMpu = agora;
    Serial.println(F("[MPU-6050] Leitura falhou (modulo desconectado?)."));
    return r;
  }
  tentativasMpu = 0;

  // Filtro passa-baixas leve (70% anterior + 30% atual): suave sem atrasar a pescada
  gFiltX = gFiltX * 0.7 + ax * 0.3;
  gFiltY = gFiltY * 0.7 + ay * 0.3;
  gFiltZ = gFiltZ * 0.7 + az * 0.3;

  // Giroscópio: rotação da cabeça em qualquer direção (inclusive virar para os lados), sem o desvio
  float wx = gx - biasGx, wy = gy - biasGy, wz = gz - biasGz;
  giroFiltrado = giroFiltrado * 0.8 + sqrt(wx * wx + wy * wy + wz * wz) * 0.2;
  r.giro = giroFiltrado;

  float modulo = sqrt(gFiltX * gFiltX + gFiltY * gFiltY + gFiltZ * gFiltZ);
  if (modulo < 0.3 || !mpuCalibrado) return r;

  float nx = gFiltX / modulo, ny = gFiltY / modulo, nz = gFiltZ / modulo;

  // Ângulo entre a gravidade atual e a de referência = quanto a cabeça saiu da postura
  float cosAng = nx * gRefX + ny * gRefY + nz * gRefZ;
  cosAng = constrain(cosAng, -1.0f, 1.0f);
  r.inclinacao = acos(cosAng) * RAD_TO_DEG;

  // Pitch e Roll relativos à postura calibrada (somente para exibição na Central)
  float px = rotRef[0][0] * nx + rotRef[0][1] * ny + rotRef[0][2] * nz;
  float py = rotRef[1][0] * nx + rotRef[1][1] * ny + rotRef[1][2] * nz;
  float pz = rotRef[2][0] * nx + rotRef[2][1] * ny + rotRef[2][2] * nz;
  r.pitch = atan2(-px, sqrt(py * py + pz * pz)) * RAD_TO_DEG;
  r.roll  = atan2(py, pz) * RAD_TO_DEG;

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
  if (pedidoDiagI2C) {
    // O I2C só é usado no Core 1: o diagnóstico roda aqui, não no Core 0 da rede
    pedidoDiagI2C = false;
    Serial.println(F("[I2C] ===== ANTES ====="));
    diagnosticarI2C();
    recuperarBarramentoMPU();
    Serial.println(F("[I2C] ===== DEPOIS DE RELIGAR O MODULO ====="));
    diagnosticarI2C();
    mpuDetectado = false;
    ultimaTentativaMpu = 0;
    tentativasMpu = 0;
  }
  if (pedidoModoLed >= 0) {
    ledModoDigital = pedidoModoLed == 1;
    pedidoModoLed = -1;
    for (uint8_t pino : {PINO_LED_R, PINO_LED_G, PINO_LED_B}) {
      if (ledModoDigital) { ledcDetach(pino); pinMode(pino, OUTPUT); }
      else ledcAttach(pino, 5000, 8);
      gpio_set_drive_capability((gpio_num_t)pino, GPIO_DRIVE_CAP_1);
    }
    Serial.println(ledModoDigital ? F("[LED] Modo digital: sem PWM") : F("[LED] Modo PWM"));
  }
  aplicarComandosRemotos();

  // Calibração guiada pendente (do boot, de nova tentativa ou de módulo reconectado)
  if (calibracaoAgendadaMs && millis() >= calibracaoAgendadaMs) calibrarComAviso();

  // Monitoramento ativado/desativado no app do motorista (via servidor).
  // Sem contato com nenhum servidor, liga sozinho: os óculos nunca ficam "mudos" por falha de rede.
  bool deveMonitorar = monitoramentoServidor == 1 || semContatoServidor;
  if (deveMonitorar != monitoramentoAtivo) {
    monitoramentoAtivo = deveMonitorar;
    melodiaPendente = monitoramentoAtivo;
    cabecaEstavaBaixa = false;
    alarmeAtivo = false;
    alarmeMinimoAte = 0;
    tempoInicioCabecaBaixa = 0;
    if (!monitoramentoAtivo) Serial.println(F("[MONITORAMENTO] Desativado no app: sensores pausados."));
    else if (semContatoServidor) Serial.println(F("[MONITORAMENTO] Sem contato com o servidor: sensores ligados por seguranca."));
    else Serial.println(F("[MONITORAMENTO] Ativado no app: sensores em acao."));
    if (!monitoramentoAtivo) {
      aplicarAtuadores(false, false, false); // Encerra qualquer alarme antes do aviso sonoro
      delay(150);
      tocarDespertador();
    }
  }
  // O ritmo espera a calibração guiada terminar (não se mistura com os bips dela)
  if (melodiaPendente && !calibracaoAgendadaMs) {
    melodiaPendente = false;
    tocarMelodiaPronto();
    verdeAtivadoMs = millis(); // LED da lente: verde e apaga devagar
  }
  // Botão Desligar/Ligar do app ("pwr":0/1): repouso com o Wi-Fi ligado para ouvir o "Ligar"
  if (energiaServidor == 0 && !emRepouso) entrarRepouso();
  else if (energiaServidor == 1 && emRepouso) sairRepouso();

  // Modo checagem: sensores suspensos (sem alarme); desliga sozinho depois de 15 min
  if (modoDiagnostico && millis() - inicioDiagnosticoMs > DURACAO_MAX_DIAGNOSTICO_MS) modoDiagnostico = false;
  bool sensoresAtivos = !emRepouso && !modoDiagnostico &&
                        (!SENSORES_SO_COM_MONITORAMENTO || (monitoramentoAtivo && !melodiaPendente));

  // Leitura digital do Sensor Infravermelho
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  unsigned long agora = millis();
  unsigned long tempoDecorridoMs = 0;
  bool bipPreAlertaAtivo = false;
  const char* estadoAtualTexto = "NORMAL";

  // Leitura do Acelerômetro GY-521
  // (em repouso o acelerômetro está dormindo: não lê)
  LeituraMPU mpu = {false, 0.0, 0.0, 0.0, false, 0.0};
  if (!emRepouso) mpu = lerMPU6050(agora);
  // Histerese: inclinada acima de 32°, só volta a "erguida" abaixo de 25° (sem liga-desliga)
  float limiarAtual = (cabecaInclinada || alarmeAtivo) ? LIMIAR_RETORNO_GRAUS : LIMIAR_INCLINACAO_GRAUS;
  cabecaInclinada = mpu.valida && (mpu.inclinacao > limiarAtual);

  // Pegar objeto x sono: cabeça se mexendo = movimento voluntário, a contagem não anda.
  // Depois que o alarme dispara, mexer não cancela: só erguer a cabeça encerra.
  if (mpu.valida && mpu.giro > LIMIAR_MOVIMENTO_GRAUS_S) ultimoMovimentoMs = agora;
  bool movimentoAtivo = ultimoMovimentoMs && agora - ultimoMovimentoMs < JANELA_MOVIMENTO_MS;
  bool inclinadoDemais = cabecaInclinada && (alarmeAtivo || !movimentoAtivo);

  // Pescada: queda brusca que "assenta" parada lá embaixo em até 0,5 s (quem pega algo segue se mexendo)
  if (mpu.valida && mpu.pescada) pescadaCandidataMs = agora;
  bool pescadaSono = false;
  if (pescadaCandidataMs) {
    if (agora - pescadaCandidataMs > CONFIRMA_PESCADA_MS) {
      pescadaCandidataMs = 0;
    } else if (mpu.inclinacao > PESCADA_INCLINACAO_MIN && mpu.giro < LIMIAR_MOVIMENTO_GRAUS_S) {
      pescadaSono = true;
      pescadaCandidataMs = 0;
    }
  }

  // Sensor IR só conta na falta do acelerômetro, e se estiver vivo
  if (estadoLeitura != NIVEL_CABECA_BAIXA) ultimoIrLivreMs = agora;
  bool irConfiavel = ultimoIrLivreMs != 0 && agora - ultimoIrLivreMs < IR_SINAL_VALIDO_MS;
  bool irCabecaBaixa = !mpuDetectado && irConfiavel && estadoLeitura == NIVEL_CABECA_BAIXA;

  // Condição de Cabeça Baixa: acelerômetro passa do ângulo OU pescada (OU IR, como reserva).
  // Com o monitoramento desligado no app, os sensores não detectam nada.
  bool condicaoCabecaBaixa = sensoresAtivos && (inclinadoDemais || pescadaSono || irCabecaBaixa);

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
        // 1 BIP RÁPIDO DE AVISO (1,0 a 1,15 s); aos 1,5 s vem o alarme contínuo + relés
        bipPreAlertaAtivo = tempoDecorridoMs >= BIP_AVISO_INICIO_MS && tempoDecorridoMs < BIP_AVISO_FIM_MS;
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
  // Relé 2 (pisca-alerta) arma já no bip de aviso; o Relé 1 (safe-stop) só no alarme contínuo.
  // Cada mudança de estado de um relé é um "tec" audível no módulo.
  bool avisoPiscaAlerta = cabecaEstavaBaixa && tempoDecorridoMs >= BIP_AVISO_INICIO_MS;
  bool bipBatimento = modoDiagnostico && (agora % 1000 < 70); // "Monitor cardíaco" do modo checagem
  bool buzzerLigado = alarmeGeral || bipPreAlertaAtivo || bipBatimento;
  bool rele1Ligado  = alarmeGeral || testeManualRele1;
  bool rele2Ligado  = alarmeGeral || avisoPiscaAlerta || testeManualRele2;
  if (alarmeGeral) estadoAtualTexto = "ALARME_ATIVADO";
  else if (cabecaEstavaBaixa) estadoAtualTexto = "PRE_ALERTA";
  // "Sinal de vida" (comando CLAUDE:OLA): bip de 0,5 s quando o Claude acessa a placa pela serial
  bool bipOla = bipOlaAteMs && (long)(bipOlaAteMs - agora) > 0;
  aplicarAtuadores(buzzerLigado || bipOla, rele1Ligado, rele2Ligado);

  // LED RGB (fica na lente, no campo de visão do motorista): vermelho piscando = alarme (estímulo
  // para acordar); roxo piscando = acelerômetro desconectado; amarelo = aviso (1 s); azul piscando =
  // calibrando; monitorando = acende verde ao ativar e apaga devagar, depois fica APAGADO para não
  // atrapalhar a visão; azul fixo = aguardando o servidor; azul "respirando" = monitoramento
  // desligado; branco piscando com o "batimento" = modo checagem
  bool calibrando = calibracaoAgendadaMs != 0;
  if (arcoIrisAteMs && agora < arcoIrisAteMs) {
    // Teste do LED (comando ARCOIRIS): passeia por todas as cores a cada 2 s
    float h = (agora % 2000) / 2000.0f * 6.0f;
    float x = 1 - fabsf(fmodf(h, 2.0f) - 1);
    float r = 0, g = 0, b = 0;
    if (h < 1)      { r = 1; g = x; }
    else if (h < 2) { r = x; g = 1; }
    else if (h < 3) { g = 1; b = x; }
    else if (h < 4) { g = x; b = 1; }
    else if (h < 5) { r = x; b = 1; }
    else            { r = 1; b = x; }
    corLed(r * 255, g * 255, b * 255);
  } else if (modoDiagnostico) {
    uint8_t b = bipBatimento ? 180 : 0;
    corLed(b, b, b);
  } else if (alarmeGeral) {
    corLed(agora % 200 < 100 ? 255 : 0, 0, 0);
  } else if (!mpuDetectado) {
    // Roxo piscando = acelerômetro desconectado (confira os fios D21, D22, D23 e RX2)
    uint8_t roxo = agora % 1000 < 500 ? 160 : 0;
    corLed(roxo, 0, roxo);
  } else if (avisoPiscaAlerta) {
    corLed(255, 140, 0);
  } else if (calibrando) {
    corLed(0, 0, agora % 500 < 250 ? 255 : 0);
  } else if (claudeChegouMs && agora - claudeUltimoMs < CLAUDE_AUSENTE_MS) {
    // Laranja do Claude: pisca 3 vezes ao chegar e depois fica aceso
    unsigned long dt = agora - claudeChegouMs;
    bool aceso = dt >= 1200 || (dt % 400 < 200);
    corLed(aceso ? 255 : 0, aceso ? 70 : 0, aceso ? 18 : 0);
  } else if (emRepouso) {
    corLed(0, 0, 0); // Desligado pelo app
  } else if (modoExibicaoServidor == 1) {
    // Modo exibição: passa suavemente de branco para azul e volta (ciclo de 4 s)
    float f = 0.5f - 0.5f * cosf(2 * PI * (agora % 4000) / 4000.0f);
    uint8_t rg = (uint8_t)(220 * (1 - f));
    corLed(rg, rg, 255);
  } else if (sensoresAtivos) {
    // Verde ao ativar o monitoramento, depois apaga devagar e fica apagado
    unsigned long dt = agora - verdeAtivadoMs;
    if (verdeAtivadoMs && dt < VERDE_ACESO_MS) {
      corLed(0, 160, 0);
    } else if (verdeAtivadoMs && dt < VERDE_ACESO_MS + VERDE_FADE_MS) {
      float f = 1.0f - (float)(dt - VERDE_ACESO_MS) / VERDE_FADE_MS;
      corLed(0, (uint8_t)(160 * f * f), 0);
    } else {
      corLed(0, 0, 0);
    }
  } else if (statusRede == REDE_SEM_COMUNICACAO) {
    corLed(0, 0, 160); // Azul fixo = aguardando o servidor (sem conexão)
  } else {
    float fase = (agora % 3000) / 3000.0f; // Respira a cada 3 s
    corLed(0, 0, (uint8_t)(10 + 90 * (0.5f - 0.5f * cosf(fase * 2 * PI))));
  }

  // Atualiza os dados compartilhados com o Core 0 de forma atômica
  portENTER_CRITICAL(&mutexDados);
  dadosGlobais.sensorIr = estadoLeitura;
  dadosGlobais.tempoCabecaBaixaMs = tempoDecorridoMs;
  dadosGlobais.buzzer = buzzerLigado ? 1 : 0;
  dadosGlobais.rele1 = rele1Ligado ? 1 : 0;
  dadosGlobais.rele2 = rele2Ligado ? 1 : 0;
  dadosGlobais.pitch = mpu.pitch;
  dadosGlobais.roll = mpu.roll;
  dadosGlobais.inclinacao = mpu.inclinacao;
  // Mantém a pescada visível por um pacote inteiro de telemetria
  if (pescadaSono && sensoresAtivos) dadosGlobais.pescadaDetectada = true;
  else if (!alarmeAtivo) dadosGlobais.pescadaDetectada = false;
  dadosGlobais.mpuOnline = mpuDetectado;
  dadosGlobais.mpuCalibrado = mpuCalibrado;
  dadosGlobais.testeAtivo = testeAlarmeAtivo;
  dadosGlobais.alarmeAtivo = alarmeGeral;
  dadosGlobais.irConfiavel = irConfiavel;
  dadosGlobais.monitorando = sensoresAtivos;
  dadosGlobais.giro = mpu.giro;
  dadosGlobais.diagnostico = modoDiagnostico;
  dadosGlobais.calibrando = calibracaoAgendadaMs != 0;
  dadosGlobais.claudePresente = claudeChegouMs && agora - claudeUltimoMs < CLAUDE_AUSENTE_MS;
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
  const char* pwr = strstr(texto, "\"pwr\":");
  if (pwr) energiaServidor = (pwr[6] == '1') ? 1 : 0;
  const char* mon = strstr(texto, "\"mon\":");
  if (mon) monitoramentoServidor = (mon[6] == '1') ? 1 : 0;
  const char* exib = strstr(texto, "\"exib\":");
  if (exib) modoExibicaoServidor = (exib[7] == '1') ? 1 : 0;
  // Comandos de diagnóstico (só pela serial USB)
  if (strstr(texto, "DIAG:1")) {
    modoDiagnostico = true;
    inicioDiagnosticoMs = millis();
    Serial.println(F("[DIAGNOSTICO] Checagem ligada: bip a cada batimento, sensores suspensos."));
  }
  if (strstr(texto, "DIAG:0")) {
    modoDiagnostico = false;
    Serial.println(F("[DIAGNOSTICO] Checagem encerrada: sensores de volta ao normal."));
  }
  if (strstr(texto, "SCAN")) pedidoScanWifi = true;
  if (strstr(texto, "I2C")) pedidoDiagI2C = true;
  if (strstr(texto, "CLAUDE:OLA")) {
    bipOlaAteMs = millis() + 500;
    claudeChegouMs = millis();
    Serial.println(F("[CLAUDE] Ola! Cheguei na ESP32."));
  }
  if (strstr(texto, "CLAUDE:TCHAU")) {
    claudeChegouMs = 0;
    Serial.println(F("[CLAUDE] Tchau! Sai da ESP32."));
  }
  if (strstr(texto, "LED:DIG")) pedidoModoLed = 1;
  if (strstr(texto, "LED:PWM")) pedidoModoLed = 0;
  if (strstr(texto, "ARCOIRIS")) {
    arcoIrisAteMs = millis() + 10000;
    Serial.println(F("[LED] Teste do LED RGB: arco-iris por 10 s."));
  }
}

void processarComandosSerial() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      claudeUltimoMs = millis(); // Qualquer comando pela serial = Claude ainda presente
      interpretarComandosTexto(cmd.c_str());
    }
  }
}

// Toca notas no buzzer (Hz, ms). Num buzzer PASSIVO sai a melodia de verdade; no buzzer ATIVO (o atual,
// de uma nota só) sai o ritmo, com o timbre dele.
void tocarNotas(const uint16_t* freqs, const uint16_t* duracoes, uint8_t n) {
  ledcAttach(PINO_BUZZER, 2000, 10);
  for (uint8_t i = 0; i < n; i++) {
    ledcWriteTone(PINO_BUZZER, freqs[i]);
    delay(duracoes[i]);
    ledcWriteTone(PINO_BUZZER, 0);
    delay(25);
  }
  ledcDetach(PINO_BUZZER);
  pinMode(PINO_BUZZER, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)PINO_BUZZER, GPIO_DRIVE_CAP_3);
  digitalWrite(PINO_BUZZER, NIVEL_BUZZER_DESLIGADO);
}

// Sons de ligar/desligar inspirados na máquina de lavar Samsung: "tiriri-rim" subindo / descendo
// (sol5, dó6, mi6, sol6; a última nota mais longa)
void somLigar() {
  const uint16_t f[4] = {784, 1047, 1319, 1568};
  const uint16_t d[4] = {100, 100, 100, 280};
  tocarNotas(f, d, 4);
}
void somDesligar() {
  const uint16_t f[4] = {1568, 1319, 1047, 784};
  const uint16_t d[4] = {100, 100, 100, 280};
  tocarNotas(f, d, 4);
}

// Botão Desligar do app: despedida (som, LED e "TCHAU!" no display), atuadores e sensores desligados e
// acelerômetro dormindo. O Wi-Fi continua ligado para ouvir o botão Ligar.
// (Tirar o cabo também não estraga a ESP32: o firmware não grava nada na memória flash.)
void entrarRepouso() {
  Serial.println(F("[ENERGIA] Desligando os oculos (repouso) pelo app..."));
  emRepouso = true;
  oledApagado = false;
  desligandoMs = millis();                      // O display mostra "TCHAU!" e a TV desligando
  cabecaEstavaBaixa = false;
  alarmeAtivo = false;
  alarmeMinimoAte = 0;
  tempoInicioCabecaBaixa = 0;
  testeAlarmeAtivo = false;
  testeManualRele1 = false;
  testeManualRele2 = false;
  calibracaoAgendadaMs = 0;
  melodiaPendente = false;
  aplicarAtuadores(false, false, false);
  tecAspas();
  somDesligar();
  for (int16_t v = 120; v >= 0; v -= 8) { corLed(v, 0, 0); delay(40); } // LED vermelho apagando devagar
  corLed(0, 0, 0);
  unsigned long inicio = millis();
  while (!oledApagado && millis() - inicio < 4000) delay(50); // Espera o display se despedir e apagar
  Wire.beginTransmission(MPU6050_ADDR);         // Acelerômetro em modo sono
  Wire.write(0x6B); Wire.write(0x40);
  Wire.endTransmission(true);
  Serial.println(F("[ENERGIA] Em repouso. Para ligar: botao Ligar do app, botao EN ou reconectar o cabo."));
}

// Botão Ligar do app: acorda o acelerômetro, bips subindo, autoteste do LED, carinha "OLÁ" no display
// e calibração guiada (1 bip -> olhar para frente -> 2 bips)
void sairRepouso() {
  Serial.println(F("[ENERGIA] Ligando os oculos pelo app..."));
  emRepouso = false;
  desligandoMs = 0;
  religarDisplay = true;
  giroDesligado = false;
  inicializarMPU6050();
  tecAspas();
  somLigar();
  corLed(255, 0, 0); delay(250); corLed(0, 255, 0); delay(250); corLed(0, 0, 255); delay(250); corLed(0, 0, 0);
  calibracaoAgendadaMs = millis() + ESPERA_CALIBRACAO_MS;
  bipAviso(1, 120);
  if (monitoramentoAtivo) melodiaPendente = true; // O ritmo toca depois da calibração
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
      if (giroDesligado) {
        // Tenta religar o giroscópio antes de calibrar (se cair de novo, volta ao modo econômico)
        giroDesligado = false;
        if (inicializarMPU6050()) Serial.println(giroDesligado ? F("[MPU-6050] Giroscopio segue desligado (modo economico).")
                                                               : F("[MPU-6050] Giroscopio religado."));
      }
      calibrarComAviso();
    } else {
      Serial.println(F("[COMANDO] Calibracao ignorada: MPU-6050 nao conectado."));
    }
  }
}

// ------------------------------------------------------------------------------
// 9. ATUADORES DE SEGURANÇA
// ------------------------------------------------------------------------------
// Acende o LED RGB (0 a 255 em cada cor); inverte sozinho se o LED for de ânodo comum
void corLed(uint8_t r, uint8_t g, uint8_t b) {
  if (ledModoDigital) {
    // Sem PWM: cada cor só acesa ou apagada (metade do brilho para cima = acesa)
    digitalWrite(PINO_LED_R, (r >= 128) != LED_RGB_ANODO_COMUM ? HIGH : LOW);
    digitalWrite(PINO_LED_G, (g >= 128) != LED_RGB_ANODO_COMUM ? HIGH : LOW);
    digitalWrite(PINO_LED_B, (b >= 128) != LED_RGB_ANODO_COMUM ? HIGH : LOW);
    return;
  }
  ledcWrite(PINO_LED_R, LED_RGB_ANODO_COMUM ? 255 - r : r);
  ledcWrite(PINO_LED_G, LED_RGB_ANODO_COMUM ? 255 - g : g);
  ledcWrite(PINO_LED_B, LED_RGB_ANODO_COMUM ? 255 - b : b);
}

// Escreve o estado de todos os atuadores de uma vez (sem pulsos intermediários nos relés)
void aplicarAtuadores(bool buzzer, bool rele1, bool rele2) {
  digitalWrite(PINO_BUZZER, buzzer ? NIVEL_BUZZER_LIGADO : NIVEL_BUZZER_DESLIGADO);
  digitalWrite(PINO_RELE_1, rele1 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
  digitalWrite(PINO_RELE_2, rele2 ? NIVEL_RELE_LIGADO : NIVEL_RELE_DESLIGADO);
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
    "\"sensor_ir\":%d,\"ir_confiavel\":%s,\"monitorando\":%s,\"diagnostico\":%s,\"tempo_cabeca_baixa_ms\":%lu,\"limiar_ms\":%lu,"
    "\"buzzer\":%d,\"rele\":%d,\"rele_1\":%d,\"rele_2\":%d,\"alarme_teste\":%d,"
    "\"estado\":\"%s\","
    "\"acelerometro\":{\"pitch\":%.1f,\"roll\":%.1f,\"inclinacao\":%.1f,\"giro\":%.0f,"
    "\"pescada_detectada\":%s,\"online\":%s,\"calibrado\":%s,\"status\":\"%s\"},"
    "\"wifi\":{\"ssid\":\"%s\",\"rssi\":%d,\"ip\":\"%s\"}}",
    (unsigned long)seq, millis(),
    d.sensorIr, d.irConfiavel ? "true" : "false", d.monitorando ? "true" : "false", d.diagnostico ? "true" : "false",
    d.tempoCabecaBaixaMs, LIMIAR_CABECA_BAIXA_MS,
    d.buzzer, d.rele1, d.rele1, d.rele2, d.testeAtivo ? 1 : 0,
    d.estado,
    d.pitch, d.roll, d.inclinacao, d.giro,
    d.pescadaDetectada ? "true" : "false", d.mpuOnline ? "true" : "false",
    d.mpuCalibrado ? "true" : "false", statusMpu,
    ssid.c_str(), conectado ? WiFi.RSSI() : 0, ip.c_str());
}

// ------------------------------------------------------------------------------
// 11. DISPLAY OLED NO MEIO DOS ÓCULOS (segundo barramento I2C, atualizado no Core 0)
// ------------------------------------------------------------------------------
// Escreve um texto centralizado na linha y
void oledCentro(const char* txt, int16_t y, uint8_t tamanho) {
  oled.setTextSize(tamanho);
  int16_t x1, y1;
  uint16_t w, h;
  oled.getTextBounds(txt, 0, y, &x1, &y1, &w, &h);
  oled.setCursor((128 - (int16_t)w) / 2, y);
  oled.print(txt);
}

// Endereço I2C do display (0x3C ou 0x3D); 0 = não respondeu
uint8_t detectarOled() {
  for (uint8_t a : {0x3C, 0x3D}) {
    WireOled.beginTransmission(a);
    if (WireOled.endTransmission() == 0) return a;
  }
  return 0;
}

void iniciarOled() {
  delay(50); // O VDD virtual (D23) acabou de ligar
  WireOled.begin(PINO_OLED_SDA, PINO_OLED_SCL, 400000);
  WireOled.setTimeOut(20);
  uint8_t endereco = detectarOled();
  if (!endereco) {
    Serial.println(F("[OLED] Display nao encontrado (SDA=TX2, SCK=D5, VDD=D23, GND=D15). Seguindo sem tela."));
    return;
  }
  // periphBegin = false: o barramento já foi iniciado nos pinos certos (não mexe no I2C do acelerômetro)
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, endereco, true, false);
  if (!oledOk) { Serial.println(F("[OLED] Falha ao iniciar o display.")); return; }
  oled.cp437(true);
  oled.clearDisplay();
  oled.display();
  Serial.printf("[OLED] Display OK no endereco 0x%02X\n", endereco);
}

// ---- Animações (desenhadas na tarefa do display, ~25 quadros por segundo) ----
const unsigned long INTRO_CARINHA_MS  = 3300; // olhos abrem, sobem, sorriso e "OLÁ"
const unsigned long INTRO_ABERTURA_MS = 2500; // "INOVA TECH · Smart Driver · TCC 2026"
const unsigned long VEICULO_ENTRA_MS  = 900;  // caminhão/ônibus entra até o centro...
const unsigned long VEICULO_PARADO_MS = 700;  // ...para um instante...
const unsigned long VEICULO_SAI_MS    = 900;  // ...e sai pela direita
const unsigned long VEICULO_TOTAL_MS  = VEICULO_ENTRA_MS + VEICULO_PARADO_MS + VEICULO_SAI_MS;

float suave(float p) { p = constrain(p, 0.0f, 1.0f); return p * p * (3 - 2 * p); }

// Olhos abrem -> sobem um pouco -> viram olhinhos fechados felizes (^ ^) + sorriso -> "OLÁ"
void desenharCarinha(unsigned long t) {
  const int16_t xOlhos[2] = {44, 84};
  if (t < 1400) {
    float abrir = suave(t / 600.0f);                     // 0 a 0,6 s: abrindo
    float subir = suave((t - 900.0f) / 500.0f);          // 0,9 a 1,4 s: sobem e diminuem
    if (t < 900) subir = 0;
    int16_t cy = 30 - (int16_t)(12 * subir);
    int16_t h = (int16_t)(2 + 18 * abrir - 6 * subir);
    for (int16_t cx : xOlhos) oled.fillRoundRect(cx - 8, cy - h / 2, 16, h, h >= 8 ? 4 : h / 2, SSD1306_WHITE);
    return;
  }
  // Olhinhos fechados de felicidade (arcos para cima)
  for (int16_t cx : xOlhos) {
    oled.drawCircleHelper(cx, 22, 7, 1 | 2, SSD1306_WHITE);
    oled.drawCircleHelper(cx, 22, 6, 1 | 2, SSD1306_WHITE);
  }
  // Sorriso crescendo (meio círculo para baixo)
  int16_t r = (int16_t)(14 * suave((t - 1400.0f) / 400.0f));
  if (r > 1) {
    oled.drawCircleHelper(64, 26, r, 4 | 8, SSD1306_WHITE);
    oled.drawCircleHelper(64, 26, r - 1, 4 | 8, SSD1306_WHITE);
  }
  if (t >= 1900) {
    oled.setTextSize(2);
    oled.setCursor(46, 47);
    oled.print(F("OLA"));
    oled.drawLine(74, 45, 77, 42, SSD1306_WHITE); // acento do "Á"
    oled.drawLine(75, 45, 78, 42, SSD1306_WHITE);
  }
}

void desenharAbertura() {
  oledCentro("INOVA TECH", 10, 2);
  oledCentro("Smart Driver", 34, 1);
  oledCentro("TCC 2026", 50, 1);
}

// Caminhão virado para a direita (46 px de comprimento), rodas apoiadas em yBase
void desenharCaminhao(int16_t x, int16_t yBase) {
  oled.fillRect(x, yBase - 21, 30, 15, SSD1306_WHITE);              // baú
  oled.fillRect(x, yBase - 6, 46, 2, SSD1306_WHITE);                // chassi
  oled.fillRoundRect(x + 31, yBase - 17, 15, 11, 2, SSD1306_WHITE); // cabine
  oled.fillRect(x + 38, yBase - 15, 6, 5, SSD1306_BLACK);           // janela
  for (int16_t rx : {7, 20, 39}) {
    oled.fillCircle(x + rx, yBase - 3, 3, SSD1306_WHITE);
    oled.drawPixel(x + rx, yBase - 3, SSD1306_BLACK);
  }
}

// Ônibus de viagem virado para a direita (58 px de comprimento)
void desenharOnibus(int16_t x, int16_t yBase) {
  oled.fillRoundRect(x, yBase - 22, 58, 17, 3, SSD1306_WHITE);      // carroceria
  for (int16_t i = 0; i < 6; i++) oled.fillRect(x + 3 + i * 8, yBase - 19, 6, 5, SSD1306_BLACK); // janelas
  oled.fillRect(x + 52, yBase - 19, 4, 8, SSD1306_BLACK);           // para-brisa
  oled.fillRect(x + 47, yBase - 13, 3, 7, SSD1306_BLACK);           // porta
  for (int16_t rx : {12, 44}) {
    oled.fillCircle(x + rx, yBase - 4, 5, SSD1306_BLACK);           // caixa de roda
    oled.fillCircle(x + rx, yBase - 4, 3, SSD1306_WHITE);
  }
}

// Posição x de um veículo: entra suave até o centro, para, e sai acelerando pela direita
int16_t posicaoVeiculo(unsigned long t, int16_t largura) {
  int16_t centro = (128 - largura) / 2;
  if (t < VEICULO_ENTRA_MS) {
    float p = 1 - powf(1 - t / (float)VEICULO_ENTRA_MS, 2);         // desacelera ao chegar
    return (int16_t)(-largura + (centro + largura) * p);
  }
  if (t < VEICULO_ENTRA_MS + VEICULO_PARADO_MS) return centro;
  float p = powf((t - VEICULO_ENTRA_MS - VEICULO_PARADO_MS) / (float)VEICULO_SAI_MS, 2); // acelera ao sair
  return (int16_t)(centro + (128 - centro) * p);
}

void desenharVeiculos(unsigned long t) {
  oledCentro("BOA VIAGEM!", 0, 1);
  oled.drawFastHLine(0, 58, 128, SSD1306_WHITE);                    // pista
  for (int16_t x = 2; x < 128; x += 14) oled.drawFastHLine(x, 62, 7, SSD1306_WHITE); // faixa
  if (t < VEICULO_TOTAL_MS) desenharCaminhao(posicaoVeiculo(t, 46), 57);
  else desenharOnibus(posicaoVeiculo(t - VEICULO_TOTAL_MS, 58), 57);
}

// ---- Animações ao DESATIVAR o monitoramento (uma diferente a cada vez, em sequência) ----
// 0 = carinha dormindo · 1 = caminhão estacionando · 2 = pôr do sol · 3 = estaciona + carinha dorme
const unsigned long DURACAO_FIM_MS[4] = {4000, 4000, 4000, 8000};

// Carinha boceja, fecha os olhos devagar, sobem "Z z z" e aparece "BOM DESCANSO!"
void desenharCarinhaDormindo(unsigned long t) {
  const int16_t xOlhos[2] = {44, 84};
  if (t < 1600) {
    // Olhos abertos que vão apertando durante o bocejo
    float bocejo = t < 600 ? 0 : sinf(PI * (t - 600) / 1000.0f);    // 0 -> 1 -> 0 entre 0,6 e 1,6 s
    int16_t h = (int16_t)(14 - 9 * bocejo);
    for (int16_t cx : xOlhos) oled.fillRoundRect(cx - 8, 20 - h / 2, 16, h, h >= 8 ? 4 : h / 2, SSD1306_WHITE);
    int16_t rBoca = (int16_t)(2 + 7 * bocejo);
    oled.drawCircle(64, 40, rBoca, SSD1306_WHITE);                   // boca abrindo no bocejo
    if (rBoca > 3) oled.drawCircle(64, 40, rBoca - 1, SSD1306_WHITE);
    return;
  }
  if (t < 2200) {
    // Olhos fechando devagar
    int16_t h = (int16_t)(14 - 12 * suave((t - 1600) / 600.0f));
    for (int16_t cx : xOlhos) oled.fillRoundRect(cx - 8, 20 - h / 2, 16, h, h >= 8 ? 4 : h / 2, SSD1306_WHITE);
    oled.drawFastHLine(58, 40, 12, SSD1306_WHITE);
    return;
  }
  // Dormindo: olhos fechados (arcos para baixo), boquinha, "Z z z" subindo
  for (int16_t cx : xOlhos) {
    oled.drawCircleHelper(cx, 17, 7, 4 | 8, SSD1306_WHITE);
    oled.drawCircleHelper(cx, 17, 6, 4 | 8, SSD1306_WHITE);
  }
  oled.drawFastHLine(59, 38, 10, SSD1306_WHITE);
  oled.setTextSize(1);
  for (uint8_t i = 0; i < 3; i++) {
    float fase = ((t - 2200 + i * 400) % 1200) / 1200.0f;
    oled.setCursor(100 + i * 7 + (int16_t)(fase * 6), 30 - (int16_t)(fase * 28));
    oled.print(i == 0 ? 'Z' : 'z');
  }
  oledCentro("BOM DESCANSO!", 54, 1);
}

// Caminhão entra devagar, para na vaga com placa "P" e apaga os faróis: "FIM DE VIAGEM"
void desenharEstacionando(unsigned long t) {
  oled.drawFastHLine(0, 58, 128, SSD1306_WHITE);                    // pista
  oled.drawFastVLine(60, 50, 8, SSD1306_WHITE);                     // marcas da vaga
  oled.drawFastVLine(109, 50, 8, SSD1306_WHITE);
  oled.drawFastVLine(118, 26, 32, SSD1306_WHITE);                   // poste da placa
  oled.fillRect(111, 14, 14, 14, SSD1306_WHITE);                    // placa "P"
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(116, 18);
  oled.print('P');
  oled.setTextColor(SSD1306_WHITE);
  float p = 1 - powf(1 - constrain(t / 2000.0f, 0.0f, 1.0f), 3);   // freia devagar até parar
  int16_t x = (int16_t)(-46 + (62 + 46) * p);
  desenharCaminhao(x, 57);
  if (t < 2300) {                                                   // farol aceso até estacionar
    const int16_t yFarol = 47;
    oled.drawLine(x + 47, yFarol, x + 56, yFarol - 4, SSD1306_WHITE);
    oled.drawLine(x + 47, yFarol, x + 56, yFarol + 4, SSD1306_WHITE);
  }
  if (t >= 2600) oledCentro("FIM DE VIAGEM", 2, 1);
}

// Sol desce atrás do horizonte, aparecem a lua e as estrelas: "HORA DE DESCANSAR"
void desenharPorDoSol(unsigned long t) {
  const int16_t horizonte = 44;
  int16_t ySol = 22 + (int16_t)(34 * suave(t / 2000.0f));
  oled.fillCircle(64, ySol, 9, SSD1306_WHITE);
  if (t < 1200) {                                                   // raios enquanto está alto
    for (uint8_t i = 0; i < 8; i++) {
      float a = i * PI / 4;
      oled.drawLine(64 + cosf(a) * 12, ySol + sinf(a) * 12, 64 + cosf(a) * 15, ySol + sinf(a) * 15, SSD1306_WHITE);
    }
  }
  oled.fillRect(0, horizonte + 1, 128, 64 - horizonte - 1, SSD1306_BLACK); // esconde o sol abaixo do horizonte
  oled.drawFastHLine(0, horizonte, 128, SSD1306_WHITE);
  if (t >= 2000) {
    oled.fillCircle(100, 14, 7, SSD1306_WHITE);                     // lua crescente
    oled.fillCircle(104, 11, 6, SSD1306_BLACK);
    const int16_t estrelas[7][2] = {{12, 8}, {30, 20}, {48, 6}, {70, 16}, {84, 4}, {118, 26}, {20, 32}};
    uint8_t visiveis = (uint8_t)constrain((t - 2000) / 120, 0UL, 7UL);
    for (uint8_t i = 0; i < visiveis; i++) {
      if ((t / 300 + i) % 4 == 0) continue;                         // estrelas piscando
      oled.drawPixel(estrelas[i][0], estrelas[i][1], SSD1306_WHITE);
      oled.drawPixel(estrelas[i][0] + 1, estrelas[i][1], SSD1306_WHITE);
    }
  }
  if (t >= 2800) oledCentro("HORA DE DESCANSAR", 52, 1);
}

// Fagulha do Claude (símbolo de raios), de tamanho ajustável pela escala
void desenharFagulha(int16_t cx, int16_t cy, float escala, uint8_t espessura) {
  const uint8_t raios[12] = {16, 12, 15, 11, 16, 13, 15, 11, 16, 12, 14, 12};
  for (uint8_t i = 0; i < 12; i++) {
    float a = -PI / 2 + i * PI / 6;
    float c = cosf(a), s = sinf(a);
    int16_t x2 = cx + (int16_t)(c * raios[i] * escala);
    int16_t y2 = cy + (int16_t)(s * raios[i] * escala);
    for (int8_t k = -(int8_t)(espessura / 2); k <= (int8_t)(espessura / 2); k++) {
      int16_t ox = (int16_t)roundf(-s * k), oy = (int16_t)roundf(c * k);
      oled.drawLine(cx + ox, cy + oy, x2 + ox, y2 + oy, SSD1306_WHITE);
    }
  }
}

// Claude chegou na ESP (CLAUDE:OLA): a fagulha cresce, aparece "Claude" e "Entrou na ESP"
const unsigned long CLAUDE_TELA_MS = 5000;
void desenharClaude(unsigned long t) {
  desenharFagulha(64, 19, suave(t / 700.0f), 3);
  if (t >= 800) oledCentro("Claude", 38, 2);
  if (t >= 1400) oledCentro("Entrou na ESP", 56, 1);
}

// Abertura de uma piscada: fecha em 80 ms e abre em 120 ms (1 = aberto, 0 = fechado)
float aberturaPiscada(unsigned long dt) {
  if (dt < 80) return 1.0f - dt / 80.0f;
  if (dt < 200) return (dt - 80) / 120.0f;
  return 1.0f;
}

// Modo exibição: olhos grandes abertos, piscando de 2 a 6 s (às vezes piscada dupla)
// e olhando para os lados de vez em quando
void desenharOlhosExibicao(unsigned long agora) {
  static unsigned long proximaPiscada = 0, inicioPiscada = 0;
  static bool piscadaDupla = false;
  static unsigned long proximoOlhar = 0, inicioOlhar = 0, duracaoOlhar = 0;
  static int8_t direcao = 1;
  if (!proximaPiscada) proximaPiscada = agora + 1500;
  if (!proximoOlhar) proximoOlhar = agora + 4000;

  float abertura = 1.0f;
  if (!inicioPiscada && agora >= proximaPiscada) { inicioPiscada = agora; piscadaDupla = random(100) < 25; }
  if (inicioPiscada) {
    unsigned long dt = agora - inicioPiscada;
    unsigned long total = piscadaDupla ? 480 : 200;
    if (dt >= total) {
      inicioPiscada = 0;
      proximaPiscada = agora + random(2000, 6000);
    } else if (dt < 200) {
      abertura = aberturaPiscada(dt);
    } else if (dt >= 280) {
      abertura = aberturaPiscada(dt - 280);                      // segunda piscada
    }
  }

  float desvio = 0;
  if (!inicioOlhar && agora >= proximoOlhar) {
    inicioOlhar = agora;
    direcao = random(2) ? 1 : -1;
    duracaoOlhar = random(800, 1500);
  }
  if (inicioOlhar) {
    unsigned long dt = agora - inicioOlhar;
    if (dt < 250) desvio = suave(dt / 250.0f);
    else if (dt < 250 + duracaoOlhar) desvio = 1;
    else if (dt < 500 + duracaoOlhar) desvio = 1 - suave((dt - 250 - duracaoOlhar) / 250.0f);
    else { inicioOlhar = 0; proximoOlhar = agora + random(4000, 9000); }
  }

  int16_t dx = (int16_t)(14 * desvio * direcao);
  int16_t h = max((int16_t)2, (int16_t)(36 * abertura));
  for (int16_t cx : {40, 88}) {
    oled.fillRoundRect(cx + dx - 14, 32 - h / 2, 28, h, min((int16_t)8, (int16_t)(h / 2)), SSD1306_WHITE);
  }
}

void desenharFim(uint8_t tipo, unsigned long t) {
  switch (tipo) {
    case 0: desenharCarinhaDormindo(t); break;
    case 1: desenharEstacionando(t); break;
    case 2: desenharPorDoSol(t); break;
    default:
      if (t < 4000) desenharEstacionando(t);
      else desenharCarinhaDormindo(t - 4000);
  }
}

// Desenha um quadro do display. Retorna true enquanto uma animação está rodando (quadros rápidos).
unsigned long inicioIntroMs = 0; // Início da abertura (carinha "OLÁ" + "INOVA TECH"); volta a 0 ao religar

bool atualizarOled(const DadosTelemetria &d, bool conectado) {
  // Ligado de novo pelo app: o display volta e repete a abertura com a carinha "OLÁ"
  if (religarDisplay) {
    religarDisplay = false;
    oledApagado = false;
    uint8_t endereco = detectarOled();
    if (endereco) { oledOk = oled.begin(SSD1306_SWITCHCAPVCC, endereco, false, false); oled.cp437(true); }
    inicioIntroMs = millis();
  }
  // Desligando pelo app (prioridade máxima): carinha dizendo "TCHAU!" e efeito de TV antiga
  // desligando (tela branca achata numa faixa, a faixa vira um ponto e o ponto some)
  if (desligandoMs) {
    if (!oledOk || oledApagado) return false;
    unsigned long t = millis() - desligandoMs;
    if (t < 2800) {
      oled.clearDisplay();
      oled.invertDisplay(false);
      oled.setTextColor(SSD1306_WHITE);
      if (t < 1500) {
        for (int16_t cx : {44, 84}) {                       // olhinhos felizes ^ ^
          oled.drawCircleHelper(cx, 22, 7, 1 | 2, SSD1306_WHITE);
          oled.drawCircleHelper(cx, 22, 6, 1 | 2, SSD1306_WHITE);
        }
        oled.drawCircleHelper(64, 26, 14, 4 | 8, SSD1306_WHITE); // sorriso
        oled.drawCircleHelper(64, 26, 13, 4 | 8, SSD1306_WHITE);
        oledCentro("TCHAU!", 47, 2);
      } else if (t < 1900) {
        int16_t h = 64 - (int16_t)(62 * suave((t - 1500) / 400.0f));  // tela branca achatando
        oled.fillRect(0, 32 - h / 2, 128, h, SSD1306_WHITE);
      } else if (t < 2300) {
        int16_t w = 128 - (int16_t)(124 * suave((t - 1900) / 400.0f)); // faixa encolhendo
        oled.fillRect(64 - w / 2, 31, w, 2, SSD1306_WHITE);
      } else {
        int16_t r = 3 - (int16_t)(3 * (t - 2300) / 500.0f);            // ponto sumindo
        if (r > 0) oled.fillCircle(64, 32, r, SSD1306_WHITE);
        else oled.drawPixel(64, 32, SSD1306_WHITE);
      }
      oled.display();
      return true;
    }
    oled.clearDisplay();
    oled.display();
    oled.ssd1306_command(SSD1306_DISPLAYOFF);
    oledApagado = true;
    return false;
  }
  static unsigned long ultimo = 0;
  static unsigned long ultimaReconfiguracao = 0;
  if (!inicioIntroMs) inicioIntroMs = millis();
  static unsigned long inicioVeiculosMs = 0;
  static bool veiculosPendentes = false;
  static bool monitorandoAntes = false;
  static unsigned long inicioFimMs = 0;
  static uint8_t animFim = 0, proximaFim = 0;
  static unsigned long chegadaClaudeVista = 0, inicioClaudeMs = 0;
  static bool claudePendente = false;
  static bool invertido = false;
  bool alarme = d.alarmeAtivo;
  bool preAlerta = strcmp(d.estado, "PRE_ALERTA") == 0;
  unsigned long agora = millis();

  // Abertura (carinha + "INOVA TECH") logo que a placa liga
  bool naIntro = agora - inicioIntroMs < INTRO_CARINHA_MS + INTRO_ABERTURA_MS;

  // Caminhão e ônibus quando o monitoramento é ativado (espera a abertura terminar)
  if (d.monitorando && !monitorandoAntes) veiculosPendentes = true;
  // Desativado no app: uma animação de "fim de viagem" diferente a cada vez, em sequência
  if (!d.monitorando && monitorandoAntes && !d.diagnostico && !naIntro) {
    animFim = proximaFim;
    proximaFim = (proximaFim + 1) % 4;
    inicioFimMs = agora;
  }
  if (!d.monitorando) { veiculosPendentes = false; inicioVeiculosMs = 0; }
  else inicioFimMs = 0;
  monitorandoAntes = d.monitorando;
  if (veiculosPendentes && !naIntro) { veiculosPendentes = false; inicioVeiculosMs = agora; }
  if (inicioVeiculosMs && agora - inicioVeiculosMs >= 2 * VEICULO_TOTAL_MS) inicioVeiculosMs = 0;
  if (inicioFimMs && agora - inicioFimMs >= DURACAO_FIM_MS[animFim]) inicioFimMs = 0;
  // Claude chegou na placa: a tela dele espera as outras animações terminarem
  unsigned long chegada = claudeChegouMs;
  if (chegada && chegada != chegadaClaudeVista) { chegadaClaudeVista = chegada; claudePendente = true; }
  if (!d.claudePresente) { claudePendente = false; inicioClaudeMs = 0; }
  if (claudePendente && !naIntro && !inicioVeiculosMs && !inicioFimMs) { claudePendente = false; inicioClaudeMs = agora; }
  if (inicioClaudeMs && agora - inicioClaudeMs >= CLAUDE_TELA_MS) inicioClaudeMs = 0;
  // Alerta interrompe qualquer animação na hora (segurança primeiro)
  if (alarme || preAlerta || d.diagnostico) { inicioVeiculosMs = 0; veiculosPendentes = false; inicioFimMs = 0; inicioClaudeMs = 0; }
  bool animacaoCurta = naIntro || inicioVeiculosMs || inicioFimMs || inicioClaudeMs;
  // Modo exibição (olhos piscando) no lugar da tela de status, enquanto estiver ligado no app
  bool exibindo = !animacaoCurta && modoExibicaoServidor == 1 && !d.diagnostico;
  bool animando = oledOk && !alarme && !preAlerta && (animacaoCurta || exibindo);

  // O display tem que ficar SEMPRE ligado. Um mau contato de um instante no VDD/GND faz o SSD1306
  // reiniciar apagado (ele perde a configuração); por isso, a cada 5 s, a configuração é reenviada.
  // Também religa sozinho se o display for desconectado e conectado de novo.
  if (!animacaoCurta && agora - ultimaReconfiguracao >= 5000) { // inclusive no modo exibição (fica horas ligado)
    ultimaReconfiguracao = millis();
    uint8_t endereco = detectarOled();
    if (!endereco) {
      if (oledOk) Serial.println(F("[OLED] Display nao responde (fio VDD/GND/SDA/SCK solto?)."));
      oledOk = false;
    } else {
      bool estavaFora = !oledOk;
      oledOk = oled.begin(SSD1306_SWITCHCAPVCC, endereco, false, false);
      oled.cp437(true);
      if (estavaFora && oledOk) Serial.println(F("[OLED] Display reconectado."));
      ultimo = 0; // Redesenha já
    }
  }

  if (!oledOk) return false;

  if (animando) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    invertido = false;
    oled.invertDisplay(false);
    unsigned long t = agora - inicioIntroMs;
    if (naIntro) {
      if (t < INTRO_CARINHA_MS) desenharCarinha(t);
      else desenharAbertura();
    } else if (inicioVeiculosMs) {
      desenharVeiculos(agora - inicioVeiculosMs);
    } else if (inicioFimMs) {
      desenharFim(animFim, agora - inicioFimMs);
    } else if (inicioClaudeMs) {
      desenharClaude(agora - inicioClaudeMs);
    } else {
      desenharOlhosExibicao(agora);
    }
    oled.display();
    ultimo = 0; // Ao terminar, a tela de status aparece na hora
    return true;
  }

  if (agora - ultimo < (alarme ? 250UL : 400UL)) return false;
  ultimo = agora;

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);

  // Cabeçalho: nome do projeto + por onde a placa está falando com o servidor
  oled.setCursor(0, 0);
  oled.print(F("INOVA TECH"));
  const char* rede = statusRede == REDE_SERVIDOR_LOCAL ? "PC" : statusRede == REDE_NUVEM ? "NUVEM" : "SEM REDE";
  oled.setCursor(128 - (int16_t)strlen(rede) * 6, 0);
  oled.print(rede);
  if (d.claudePresente) desenharFagulha(128 - (int16_t)strlen(rede) * 6 - 8, 3, 0.25f, 1); // Claude na placa
  oled.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  // Estado principal em letra grande
  const char* titulo;
  const char* sub = nullptr;
  if (d.diagnostico)        { titulo = "CHECAGEM";   sub = "Sensores suspensos"; }
  else if (alarme)          { titulo = "ALARME!";    sub = "ACORDE! Erga a cabeca"; }
  else if (!d.mpuOnline)    { titulo = "SEM SENSOR"; sub = "Confira os fios"; }
  else if (d.calibrando)    { titulo = "CALIBRANDO"; sub = "Olhe para frente"; }
  else if (preAlerta)       { titulo = "ATENCAO"; }
  else if (d.monitorando)   { titulo = "ATIVO";      sub = "Monitorando"; }
  else                      { titulo = "PAUSADO";    sub = "Ative no app"; }
  oledCentro(titulo, 15, 2);

  char linha[32];
  if (preAlerta || (alarme && !d.testeAtivo)) {
    snprintf(linha, sizeof(linha), "Cabeca baixa %.1f/%.1fs", d.tempoCabecaBaixaMs / 1000.0, LIMIAR_CABECA_BAIXA_MS / 1000.0);
    oledCentro(linha, 34, 1);
  } else if (sub) {
    oledCentro(sub, 34, 1);
  }

  // Barra de inclinação da cabeça (cheia = 60°), com marca no limite de alerta (32°)
  if (d.mpuOnline && d.mpuCalibrado) {
    float inc = constrain(d.inclinacao, 0.0f, 60.0f);
    oled.drawRect(0, 45, 100, 7, SSD1306_WHITE);
    oled.fillRect(1, 46, (int16_t)(98 * inc / 60.0f), 5, SSD1306_WHITE);
    oled.drawFastVLine(1 + (int16_t)(98 * LIMIAR_INCLINACAO_GRAUS / 60.0f), 43, 11, SSD1306_WHITE);
    snprintf(linha, sizeof(linha), "%2.0f%c", d.inclinacao, (char)248); // 248 = símbolo de grau
    oled.setCursor(104, 45);
    oled.print(linha);
  }

  // Rodapé: Claude na placa, ou a rede Wi-Fi
  oled.setCursor(0, 56);
  if (d.claudePresente) {
    oled.print(F("Claude conectado :)"));
  } else if (conectado) {
    String ssid = WiFi.SSID();
    if (ssid.length() > 13) ssid = ssid.substring(0, 13);
    snprintf(linha, sizeof(linha), "%s %ddBm", ssid.c_str(), (int)WiFi.RSSI());
    oled.print(linha);
  } else {
    oled.print(F("Procurando Wi-Fi..."));
  }

  // No alarme, a tela pisca invertida (fundo branco) para chamar atenção
  invertido = alarme ? !invertido : false;
  oled.invertDisplay(invertido);
  oled.display();
  return false;
}

// Tarefa própria do display (Core 0): animações suaves sem atrasar a rede nem a detecção de sono
void taskDisplay(void *pvParameters) {
  for (;;) {
    DadosTelemetria snap;
    portENTER_CRITICAL(&mutexDados);
    snap = dadosGlobais;
    portEXIT_CRITICAL(&mutexDados);
    bool animando = atualizarOled(snap, WiFi.status() == WL_CONNECTED);
    vTaskDelay(pdMS_TO_TICKS(animando ? 10 : 50));
  }
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
  unsigned long desconectadoDesdeMs = 0;
  unsigned long ultimoEnvioNuvem = 0;
  unsigned long ultimoSucessoNuvem = 0;
  char estadoEnviadoNuvem[24] = "";
  uint32_t seq = 0;
  char payload[700];
  char resposta[256];

  for (;;) {
    // Comando serial SCAN: mostra as redes 2,4 GHz que a ESP32 enxerga (diagnóstico)
    if (pedidoScanWifi) {
      pedidoScanWifi = false;
      int n = WiFi.scanNetworks();
      Serial.printf("[SCAN] %d redes visiveis (seguranca: 3=WPA2 6=WPA3 7=WPA2/WPA3)\n", n);
      for (int i = 0; i < n && i < 15; i++) {
        Serial.printf("[SCAN] %-26s canal %2d  %4d dBm  seguranca %d\n",
                      WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), (int)WiFi.encryptionType(i));
      }
      WiFi.scanDelete();
    }

    bool conectado = (WiFi.status() == WL_CONNECTED);

    if (!conectado) {
      if (udpAtivo) {
        udp.stop();
        udpAtivo = false;
        servidorConhecido = false;
        desconectadoDesdeMs = millis();
        Serial.println(F("[WIFI] Conexao perdida. Procurando redes conhecidas..."));
      }
      // Na primeira vez procura já. Depois de uma queda, dá 8 s para a reconexão automática antes
      // de procurar de novo (uma busca no meio derruba a tentativa em andamento). Tempo limite
      // longo: ponto de acesso de celular demora mais que um roteador para entregar o IP.
      bool podeProcurar = ultimoCheckWifi == 0 ||
                          (millis() - ultimoCheckWifi >= 3000 && millis() - desconectadoDesdeMs >= 8000);
      if (podeProcurar) {
        ultimoCheckWifi = millis();
        // Bloqueia só o Core 0; o Core 1 segue protegendo o motorista
        if (wifiMulti.run(15000) != WL_CONNECTED) Serial.println(F("[WIFI] Nao conectou. Tentando de novo..."));
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
      bool mudouEstado = strcmp(snap.estado, estadoEnviadoNuvem) != 0;
      unsigned long intervaloNuvem = strcmp(snap.estado, "NORMAL") != 0 ? INTERVALO_NUVEM_ALERTA_MS : INTERVALO_NUVEM_MS;
      if (ENVIAR_NUVEM_SEM_SERVIDOR_LOCAL && semServidorLocal &&
          (mudouEstado || millis() - ultimoEnvioNuvem >= intervaloNuvem)) {
        ultimoEnvioNuvem = millis();
        strncpy(estadoEnviadoNuvem, snap.estado, sizeof(estadoEnviadoNuvem) - 1);
        httpNuvem.setConnectTimeout(2500);
        httpNuvem.setTimeout(2500);
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

    // À prova de falha: tempo desde o último contato com qualquer servidor (desde o boot, se nunca houve)
    semContatoServidor = millis() - max(ultimoContatoServidor, ultimoSucessoNuvem) > FAILSAFE_SEM_CONTATO_MS;

    bool nuvemRespondendo = ultimoSucessoNuvem && millis() - ultimoSucessoNuvem < TIMEOUT_SERVIDOR_LOCAL_MS;
    statusRede = !conectado ? REDE_SEM_COMUNICACAO
               : servidorConhecido ? REDE_SERVIDOR_LOCAL
               : nuvemRespondendo ? REDE_NUVEM : REDE_SEM_COMUNICACAO;


    bool emAlerta = strcmp(snap.estado, "NORMAL") != 0;
    vTaskDelay(pdMS_TO_TICKS(emAlerta ? INTERVALO_TELEMETRIA_ALERTA_MS : INTERVALO_TELEMETRIA_MS));
  }
}
