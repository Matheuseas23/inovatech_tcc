# MEMÓRIA TÉCNICA DO PROJETO — INOVA TECH SMART DRIVER
**Projeto de TCC:** Óculos Inteligentes de Deteção de Fadiga e Queda de Cabeça para Camionistas  
**Desenvolvedor:** Matheus Garcia  
**Instituição:** Instituição Adventista de Ensino (IAE / UNASP)  
**Microcontrolador:** ESP32 Dev Module (Espressif Systems)  
**Framework:** Arduino Core (PlatformIO / Arduino IDE)  
**Data da Última Atualização:** 15 de Outubro de 2026 às 11:50  

---

## 1. Visão Geral e Princípio de Operação
O **Inova Tech Smart Driver** é um dispositivo de segurança ativa montado em óculos para frotas de transporte pesado, focado em detetar quando o motorista adormece ao volante e tomba a cabeça para a frente (cabeça baixa / perda de consciência).

### Mecânica de Deteção com Sensor de Obstáculo Infravermelho:
1. **Posicionamento na Armação:** O módulo sensor de obstáculo infravermelho (placa azul com emissor transparente e recetor fotodíodo preto) é fixado na armação/haste dos óculos, apontado estrategicamente para baixo (em direção ao peito / tronco / queixo do condutor).
2. **Postura Normal de Condução (Cabeça Erguida):**
   - A distância entre os óculos e o peito é ampla (superior a 20–30 cm).
   - O sensor não deteta obstáculo próximo: saída digital em nível `HIGH` e LED indicador do módulo apagado.
3. **Queda de Cabeça por Adormecimento (Cabeça Baixa):**
   - Quando o motorista adormece e a cabeça tomba para a frente, a distância para o peito/gola da camisa diminui para 5–15 cm.
   - O feixe infravermelho reflete na roupa/corpo: saída digital vai para nível `LOW` e LED indicador do sensor acende.
4. **Filtro de Segurança Temporal (2,5 segundos):**
   - Para permitir que o motorista olhe pontualmente para o painel de instrumentos, espelhos retrovisores ou alavanca de velocidades sem falsos alarmes, o sistema só dispara após **2500 ms (2,5 segundos)** ininterruptos de cabeça baixa.
5. **Acionamento e Corte Imediato:**
   - Ultrapassado o limiar, o **Buzzer Ativo** apita de forma contínua para acordar o condutor, acionando também a saída para relé veicular.
   - Assim que o condutor ergue a cabeça (postura corrigida), o alarme cessa **instantaneamente** e a contagem é reiniciada.

---

## 2. Pinout e Mapeamento de Hardware Atual

| Componente | Terminal / Fio | Pino na Placa ESP32 | Função / Nível Lógico | Notas Técnicas de Engenharia |
| :--- | :--- | :--- | :--- | :--- |
| **Sensor de Obstáculo IR** | `VCC` | **`3V3`** | Alimentação positiva (3,3V) | Proteção obrigatória para manter a saída nos limites seguros dos GPIOs do ESP32. |
| **Sensor de Obstáculo IR** | `GND` | **`GND`** | Referência de terra | Ligação comum com a ESP32. |
| **Sensor de Obstáculo IR** | `OUT` | **`D32` (GPIO 32)** | Entrada digital com pull-up | Nível `LOW` indica cabeça baixa (obstáculo/peito aproximado). Nível `HIGH` postura normal. |
| **Buzzer Ativo** | Positivo (`+`) | **`D25` (GPIO 25)** | Saída digital | Ativação a nível `HIGH` (3,3V). |
| **Buzzer Ativo** | Negativo (`-`) | **`GND`** | Retorno de terra | Ligado diretamente ao GND comum. |
| **Módulo Relé (2 Canais)** | `VCC` | **`VIN`** / **`5V`** | Alimentação da bobina | Conectar no pino 5V para acionamento eletromecânico seguro. |
| **Módulo Relé (2 Canais)** | `GND` | **`GND`** | Referência de terra | Ligado ao GND da ESP32. |
| **Módulo Relé (Canal 1)** | `IN1` | **`D18` (GPIO 18)** | Saída digital | Reservado para atuar comandos no veículo (buzina auxiliar / luz de emergência). |
| **Módulo Relé (Canal 2)** | `IN2` | *(Livre / Reserva)* | Saída digital | Fica disponível para expansões futuras. |

> [!IMPORTANT]
> **Calibração do Trimpot Azul do Sensor IR:**
> 1. Vista os óculos e fique na postura normal de condução (olhando em frente).
> 2. Gire o trimpot azul com uma chave de fendas pequena até que o LED indicador do sensor fique totalmente **apagado**.
> 3. Em seguida, incline a cabeça para baixo simulando adormecimento: o LED indicador deve **acender**.
> 4. Volte a erguer a cabeça: o LED deve **apagar imediatamente**.

---

## 3. Lógica de Firmware (Não-Bloqueante)
O código em [src/main.cpp](file:///c:/Users/matheus_garcia01/OneDrive%20-%20Institui%C3%A7%C3%A3o%20Adventista%20de%20Ensino/Projetos%202026/INOVATECH/Smart%20Driver/src/main.cpp) utiliza arquitetura orientada a eventos com temporização assíncrona:
1. **Zero Bloqueios (`millis()`):** Não utiliza a função `delay()`, assegurando resposta em microssegundos e capacidade de expansão para telemetria sem fios.
2. **Transmissão Serial por Borda:** Mensagens de log enviadas apenas na mudança de estado (`Aviso: Cabeca Baixa Detectada`, `ALARME ATIVADO` e `Postura Normalizada`), sem sobrecarregar a porta UART a `115200 baud`.

---

## 4. Código-Fonte Atual (`src/main.cpp`)

```cpp
#include <Arduino.h>

const uint8_t PINO_SENSOR_IR = 32;
const uint8_t PINO_BUZZER    = 25;
const uint8_t PINO_RELE      = 18;

const int NIVEL_CABECA_BAIXA    = LOW;
const int NIVEL_BUZZER_LIGADO   = HIGH;
const int NIVEL_BUZZER_DESLIGADO = LOW;
const int NIVEL_RELE_LIGADO     = HIGH;
const int NIVEL_RELE_DESLIGADO  = LOW;

const unsigned long LIMIAR_CABECA_BAIXA_MS = 2500;

unsigned long tempoInicioCabecaBaixa = 0;
bool cabecaEstavaBaixa              = false;
bool alarmeAtivo                    = false;

void ativarAlarmes();
void desativarAlarmes();

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
  Serial.println(F("Sistema pronto. A monitorizar inclinacao da cabeca...\n"));
}

void loop() {
  int estadoLeitura = digitalRead(PINO_SENSOR_IR);
  bool cabecaBaixaAgora = (estadoLeitura == NIVEL_CABECA_BAIXA);
  unsigned long tempoAtual = millis();

  if (cabecaBaixaAgora) {
    if (!cabecaEstavaBaixa) {
      cabecaEstavaBaixa = true;
      tempoInicioCabecaBaixa = tempoAtual;
      Serial.println(F("Aviso: Cabeca Baixa Detectada (A iniciar contagem)..."));
    } 
    else if (!alarmeAtivo) {
      if ((tempoAtual - tempoInicioCabecaBaixa) >= LIMIAR_CABECA_BAIXA_MS) {
        alarmeAtivo = true;
        ativarAlarmes();
        Serial.println(F("ALARME ATIVADO: MOTORISTA ADORMECEU COM A CABECA BAIXA!"));
      }
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
```

---

## 5. Sensores Secundários e Evolução (PIR)
- **Sensor de Presença PIR (Cúpula Branca):** Mapeado como módulo complementar para verificação de presença humana no banco/cabine do caminhão. Poderá ser integrado caso se deseje desarmar o sistema caso o motorista saia da cabine.

---

## 6. Registro de Sessão e Ponto de Parada (Check-in)
* **Data e Hora:** 15 de Outubro de 2026, 11:50 (Horário Local)
* **Status do Código:** 100% implementado e compilado com sucesso (`[SUCCESS]` no PlatformIO, flash em 20.6%, RAM em 6.6%).
* **Cenário de Teste:** Teste de bancada (componentes na protoboard/mesa, ainda não montados na armação física dos óculos).
* **Diagnóstico de Hardware/Comunicação:**
  - A placa ESP32 utiliza chip conversor USB-Serial **CP2102** (`VID: 10C4, PID: EA60`).
  - No computador anterior, a placa caía na porta `COM1` por ausência do driver CP2102 do Windows.
  - O driver oficial foi baixado e guardado na pasta: `INOVATECH\drivers\CP210x_Drivers.zip` *(fora da pasta do site do TCC)*.
* **Instruções Imediatas para Retomada no Próximo Dispositivo:**
  1. Conectar a ESP32 na porta USB do novo computador.
  2. Verificar no Gerenciador de Dispositivos / PlatformIO se a porta serial da ESP32 é reconhecida (ex: `COM3`, `COM4` etc.). Se necessário, instalar o driver da pasta `INOVATECH\drivers\`.
  3. No PlatformIO, clicar na seta de **Upload** ($\rightarrow$). Se travar em `Connecting......`, pressionar o botão **BOOT** na placa por 2 segundos.
  4. Abrir o **Monitor Serial** (ícone da tomada a 115200 baud).
  5. Testar colocando a mão na frente do sensor IR por 2,5 segundos para verificar o disparo do buzzer e o corte imediato ao retirar a mão.
