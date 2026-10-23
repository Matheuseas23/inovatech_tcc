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

// Apresentação: conectar SOMENTE no ponto de acesso do Redmi. Para voltar a usar as 3 redes, troque para false.
const bool SOMENTE_WIFI_REDMI = true;

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
const float LIMIAR_MOVIMENTO_GRAUS_S    = 25.0; // Rotação acima disso = movimento voluntário
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
};

DadosTelemetria dadosGlobais = {1, 0, 0, 0, 0, 0.0, 0.0, 0.0, false, false, false, false, "NORMAL", false, false, false, 0.0, false};
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

// Monitoramento ligado no app: chega como "mon":1 / "mon":0 nas respostas do servidor (-1 = sem resposta ainda)
volatile int8_t monitoramentoServidor = -1;
volatile bool semContatoServidor = false; // Escrito pelo Core 0 (à prova de falha)

// Modo checagem (comando serial DIAG:1 / DIAG:0): bip de monitor cardíaco a cada 1 s e sensores
// suspensos (nada de alarme). Desliga sozinho em 15 min por segurança.
const unsigned long DURACAO_MAX_DIAGNOSTICO_MS = 15UL * 60UL * 1000UL;
volatile bool modoDiagnostico = false;
volatile unsigned long inicioDiagnosticoMs = 0;
volatile bool pedidoScanWifi = false; // Comando serial SCAN: lista as redes que a ESP32 enxerga

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
void tecAspas();
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

  aplicarAtuadores(false, false, false);

  // Aguarda 150ms para o sensor GY-521 estabilizar a alimentação
  delay(150);

  // Inicializa I2C nos pinos dedicados D21 (SDA) e D22 (SCL)
  Wire.begin(PINO_MPU_SDA, PINO_MPU_SCL, 400000);
  Wire.setTimeOut(20); // Nunca deixa um fio solto travar o Core 1
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
  }
  // Modo checagem: sensores suspensos (sem alarme); desliga sozinho depois de 15 min
  if (modoDiagnostico && millis() - inicioDiagnosticoMs > DURACAO_MAX_DIAGNOSTICO_MS) modoDiagnostico = false;
  bool sensoresAtivos = !modoDiagnostico &&
                        (!SENSORES_SO_COM_MONITORAMENTO || (monitoramentoAtivo && !melodiaPendente));

  // Leitura digital do Sensor Infravermelho
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  unsigned long agora = millis();
  unsigned long tempoDecorridoMs = 0;
  bool bipPreAlertaAtivo = false;
  const char* estadoAtualTexto = "NORMAL";

  // Leitura do Acelerômetro GY-521
  LeituraMPU mpu = lerMPU6050(agora);
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
  aplicarAtuadores(buzzerLigado, rele1Ligado, rele2Ligado);

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
  const char* mon = strstr(texto, "\"mon\":");
  if (mon) monitoramentoServidor = (mon[6] == '1') ? 1 : 0;
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
      calibrarComAviso();
    } else {
      Serial.println(F("[COMANDO] Calibracao ignorada: MPU-6050 nao conectado."));
    }
  }
}

// ------------------------------------------------------------------------------
// 9. ATUADORES DE SEGURANÇA
// ------------------------------------------------------------------------------
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
