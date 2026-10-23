# Prompt de especificação enviado ao Claude (23/10/2026, ~00:55)

> Cópia fiel do prompt longo colado na primeira conversa com o Claude Code.
> Única alteração: as senhas de Wi-Fi foram trocadas por "(ver main.cpp)", porque este repositório é publicado.
>
> **ATENÇÃO: o texto original chegou CORTADO** — termina no meio do Nível 3 do SOS ("...e envio").
> Tudo o que viria depois (resto do SOS, descrição do `motorista.html`, demais seções) nunca foi recebido.
> Se o restante for encontrado, cole-o no fim deste arquivo.

---

# CONTEXTO COMPLETO E ESPECIFICAÇÃO TÉCNICA — PROJETO TCC: INOVA TECH (SMART DRIVER)

Você está assumindo o desenvolvimento e a estabilização final do projeto de TCC **INOVA TECH — Smart Driver**, um sistema embarcado IoT de monitoramento biométrico e cognitivo em tempo real para prevenção de acidentes por fadiga e sonolência em motoristas de caminhão e operadores industriais.

Sua missão principal é auditar, corrigir e blindar a comunicação entre o **Firmware da ESP32**, o **Servidor Backend Node.js (`backend/server.js`)**, a **Torre de Controle (`central.html`)** e o **Aplicativo do Motorista (`motorista.html`)**, além de validar a integração recém-adicionada do acelerômetro físico **GY-521 (MPU-6050)**.

---

## 1. ESTRUTURA DO REPOSITÓRIO E ARQUIVOS PRINCIPAIS

- `GEMINI.md` e `Smart Driver/MEMORIA_DO_PROJETO.md`: Documentação canônica e regras de negócio/design do projeto.
- `index.html` + `style.css`: Portal institucional público para apresentação acadêmica aos professores/banca.
- `central.html`: **CENTRAL TECH** — Torre de Controle Operacional (HUD) exibida na TV/PC para monitoramento de frota em tempo real.
- `motorista.html`: **App do Motorista** — Interface mobile para smartphone fixado no painel da cabine veicular.
- `backend/server.js`: Servidor unificado em Node.js (Express + WebSocket `ws` na porta `3001` + Receptor UDP Broadcast na porta `3002` + Ponte Serial USB `COM5`).
- `backend/data/historico_eventos.json` e `backend/data/ultimo_estado.json`: Persistência local em disco.
- `Smart Driver/src/main.cpp` e `Smart Driver/platformio.ini`: Projeto PlatformIO do firmware C++ Dual-Core (FreeRTOS) para a placa `esp32dev`.

> **REGRA DE OURO DE DIRETÓRIO**: Todo o projeto roda dentro de uma pasta do OneDrive no Windows. Nunca instale pacotes globais fora da pasta `backend/node_modules` e evite escritas excessivas em disco a cada pacote para não gerar conflitos de sincronização no OneDrive.

---

## 2. HARDWARE EMBARCADO (ESP32) E MAPEAMENTO EXATO DE PINOS

O protótipo físico é montado em um óculos inteligente sem uso de protoboard (fiação direta compacta). Como os pinos físicos `GND` e `3V3` da ESP32 já foram ocupados pelos primeiros sensores, utilizamos **GPIOs configurados via software como Alimentação/Terra Virtual (`OUTPUT` em `HIGH` ou `LOW` contínuo)** para os módulos adicionais.

### Tabela de Pinagem Oficial na ESP32 (30/38 pinos):

1. **Sensor de Obstáculo Infravermelho (IR)**:
   - Posicionado na haste dos óculos apontando a 45° para baixo (direção queixo/clavícula).
   - `VCC` ➔ Pino `3V3` físico da ESP32.
   - `GND` ➔ Pino `GND` físico da ESP32.
   - `OUT` ➔ **`GPIO 32 (D32)`** (`INPUT_PULLUP`).
   - *Lógica*: Cabeça erguida = `HIGH` (1). Cabeça baixa/queixo próximo = `LOW` (0).

2. **Buzzer Sonoro Ativo (Alerta nos Óculos)**:
   - Positivo (`+`) ➔ **`GPIO 25 (D25)`** (`OUTPUT`, ativo em `HIGH`).
   - Negativo (`-`) ➔ Pino `GND` físico da ESP32.

3. **Módulo Relé Duplo Optoisolado HL-52S (Atuação Veicular)**:
   - Jumper amarelo `JD-VCC / VCC` mantido encaixado.
   - `VCC` ➔ Pino **`VIN` (5V)** da ESP32.
   - `GND` ➔ **`GPIO 13 (D13)`** configurado como **GND Virtual** (`pinMode(13, OUTPUT); digitalWrite(13, LOW);`).
   - `IN1` ➔ **`GPIO 18 (D18)`** — Canal 1: Safe-Stop (Corte gradual de tração/motor).
   - `IN2` ➔ **`GPIO 19 (D19)`** — Canal 2: Acionamento automático do Pisca-Alerta de emergência.
   - *Lógica*: Módulo Active LOW (`LOW` aciona o relé/acende LED; `HIGH` desliga).

4. **Acelerômetro e Giroscópio GY-521 (MPU-6050 — I2C)** *(Recém-chegado e integrado)*:
   - `VCC` ➔ **`GPIO 23 (D23)`** configurado como **3.3V Virtual** (`pinMode(23, OUTPUT); digitalWrite(23, HIGH);`).
   - `GND` ➔ **`GPIO 16 (RX2)`** configurado como **GND Virtual** (`pinMode(16, OUTPUT); digitalWrite(16, LOW);`). *(Nota: Não usar TX0/RX0 para não travar a Serial USB).*
   - `SDA` ➔ **`GPIO 21 (D21)`** (I2C Data).
   - `SCL` ➔ **`GPIO 22 (D22)`** (I2C Clock).
   - *Função*: Mede inclinação da cabeça (`pitch` e `roll` em graus) e detecta "pescadas" bruscas de sono (`pitch > 32°` ou variação angular abrupta).

### Dinâmica de Tempo e Bips no Firmware (`Smart Driver/src/main.cpp`):
- **Arquitetura Dual-Core FreeRTOS**:
  - **Core 1 (`loop()` a 100 Hz / 10ms)**: Leitura em tempo real estrito do Sensor IR + MPU-6050, controle do Buzzer e Relés. Totalmente independente de Wi-Fi/rede (nunca trava se a internet cair).
  - **Core 0 (`taskTelemetria` a cada 200ms–250ms)**: Gerencia reconexão Wi-Fi (`WiFiMulti`) e envia o JSON de telemetria via:
    1. **Serial USB (`COM5`)** com prefixo `TELEMETRY:{...}`
    2. **UDP Broadcast (`255.255.255.255:3002`)** na rede local
    3. **HTTP POST** para o IP local do notebook (`http://192.168.1.218:3001/api/telemetry`) e/ou nuvem Render (`https://inovatech-tcc.onrender.com/api/telemetry`).
- **Redes Wi-Fi Cadastradas**:
  - Casa: SSID `GARCIA 7G` | Senha: (ver `Smart Driver/src/main.cpp`)
  - Hotspot Celular Android: SSID `REDMI Note 15 5G` | Senha: (ver `Smart Driver/src/main.cpp`)
  - Hotspot Contingência: SSID `iPhone de Matheus` | Senha: (ver `Smart Driver/src/main.cpp`)
- **Tolerância e Pré-Alerta (3,5 segundos / 3500 ms)**:
  - Quando a cabeça baixa é detectada (`PRE_ALERTA`), inicia-se a contagem até `3500 ms` (ou `1500 ms` caso queira modo rápido de demonstração — verificar sincronia entre firmware, backend e frontend).
  - Durante o pré-alerta de 3.5s, o buzzer emite **3 bips progressivos alongados (350ms cada)**:
    - 1º Bip: `700 ms` a `1050 ms`
    - 2º Bip: `1700 ms` a `2050 ms`
    - 3º Bip: `2700 ms` a `3050 ms`
  - Ao atingir `3500 ms`: entra em **`ALARME_ATIVADO`**, acionando o **Buzzer de forma contínua** e atracando os **Relés 1 e 2**.
  - Ao erguer a cabeça, o estado volta para `NORMAL` e desativa os atuadores.

---

## 3. ARQUITETURA DAS 3 PÁGINAS DO FRONTEND & DESIGN SYSTEM

### Diretrizes Visuais Obrigatórias:
- **Estética**: *Dark-Tech / Industrial Blueprint / Cockpit HUD*.
- **Cores**: Fundo da página `--page: #020b12`, contêiner `--bg: #031522`, destaque ciano `--cyan: #08deeb`, texto `--text: #e6f4f6`.
- **Tipografia**: `'Barlow Condensed'` (títulos em uppercase) e `'Space Mono'` (dados técnicos, métricas e cronômetros).
- **Ícones**: Usar exclusivamente **Google Fonts Material Symbols** (`lock`, `visibility`, `dashboard`, `smartphone`, `local_cafe`, `power_settings_new`, `location_on`, `medical_services`, etc.). Proibido emojis genéricos na interface tática.
- **Senha Mestra de Autenticação**: `inovatech26` (minúsculo).

### Página 1: `index.html` (Portal Institucional de Apresentação)
- Contém vídeo de fundo em tela cheia (`oculos.mp4`, que reproduz no máximo 2 vezes e para no último frame, com fallback `oculospc.jpg` e fundo `#020b12`).
- **Regra Crítica da Logo (`logo_inovatech.png`)**:
  - Na aba **Home (`#home`)**, a logo **NÃO deve aparecer no `header` (margin-top)**; na Home ela aparece **exclusivamente grande e centralizada no Hero** com efeito neon.
  - Nas demais abas (`#manual`, `#ecosystem`, `#about`, `#references`, `#game`), a logo aparece normalmente no canto superior esquerdo do `header`.
  - O `margin-top: 28px` da Home nunca deve ser removido, mantendo fundo transparente.
- **Modal Seletor Inicial (`#portalAccessModal`)**:
  - Controlado pela constante `const MOSTRAR_PORTAL_SELETOR = true;`.
  - Exibe 3 botões ao abrir o site:
    1. *Apresentação do Projeto (Acesso Livre)* -> Fecha o modal e mostra o site institucional.
    2. *Central Tech (Torre de Controle)* -> Pede a senha `inovatech26` e redireciona para `central.html`.
    3. *App do Motorista (Cabine Veicular)* -> Pede a senha `inovatech26` e redireciona para `motorista.html`.

### Página 2: `central.html` (CENTRAL TECH — Torre de Controle para TV/PC)
- **Visão de Frota**:
  - Mostra 4 motoristas. Apenas **Matheus Garcia** (Scania R-450, Placa `INO-2026`, BR-116 KM 284) está ativo/desbloqueado para abrir o Cockpit Ao Vivo. Os outros 3 (Matheus Oliveira, Nathan Arkhanjo, Thiago Tivium) aparecem fora de turno/bloqueados.
- **Cockpit Operacional (6 Cards + Rastreamento GPS)**:
  - **Card 1**: Status Geral do Condutor (`NORMAL`, `PRÉ-ALERTA`, `ALARME ATIVADO`) com cronômetro em tempo real e botões rápidos de simulação/normalização.
  - **Card 2**: Rastreamento GPS Ao Vivo / Monitoramento de Cabine.
  - **Card 3**: Telemetria do Sensor IR (GPIO 32 — Estado lógico `HIGH`/`LOW` e barra de progresso em ms).
  - **Card 4**: Telemetria do Acelerômetro (I2C GPIO 21/22 — Eixos `Pitch`, `Roll` em graus, status do módulo MPU-6050 e alerta de "pescada").
  - **Card 5**: Atuadores de Segurança & Comandos Remotos (Status do Buzzer GPIO 25, Relé 1 Safe-Stop GPIO 18, Relé 2 Pisca-Alerta GPIO 19, botões de Testar Alarme e Silenciar/Resetar).
  - **Card 6**: Conectividade (Status Online/Offline da ESP32, WebSocket, contador de alarmes da sessão e Log Cronológico de Eventos com exportação JSON).
- **Protocolo de Escalonamento Crítico SOS (3 Níveis)** quando o motorista não confirma/desativa o alerta:
  - **Nível 1**: Disparo do alarme de fadiga + Coffee Call no celular + Buzzer e Relés.
  - **Nível 2 (Alerta Crítico na Central)**: Quando o motorista fica sem responder ao alerta (calibrado no servidor entre 9s/10s ou 17s), dispara alarme prioritário e síntese de voz feminina na Central.
  - **Nível 3 (Protocolo de Possível Acidente / SOS)**: Persistência sem resposta (15s/20s ou 27s) -> Modo SOS vermelho piscante, sirene militar, desaceleração autônoma Safe-Stop (64 km/h até 0 km/h), acionamento de pisca-alerta e envio

---

*(fim do texto recebido — continuação pendente)*
