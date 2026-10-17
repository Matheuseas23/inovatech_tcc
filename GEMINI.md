# INOVATECH SMART DRIVER — MEMÓRIA DO PROJETO & DIRETRIZES DE DESIGN

Este arquivo serve como memória persistente e base de treinamento do assistente para todas as interações no projeto **INOVATECH SMART DRIVER (TCC)**. Todas as regras, padrões estéticos e preferências do usuário aqui documentados devem ser seguidos rigorosamente.

---

## 1. Visão Geral do Projeto
- **Nome**: INOVA TECH — Smart Driver
- **Objetivo**: Sistema de monitoramento biométrico e cognitivo em tempo real para prevenção de acidentes e combate à fadiga em motoristas profissionais e operadores industriais.
- **Arquitetura Técnica Global**: 
  - **Frontend**: Site estático puro (HTML5 + CSS3 + Vanilla JavaScript), compatível com a extensão **Live Server ("Go Live")** do VS Code e hospedado na **Vercel** (`https://inovatech-tcc.vercel.app/`).
  - **Backend**: Servidor Node.js (Express + WebSocket nativo na porta `3001`), hospedável no **Render** (`Web Service Node`) ou executável localmente no PC da apresentação.
  - **Firmware IoT**: ESP32 programado em C++ (PlatformIO / Arduino IDE) com envio de telemetria periódica via HTTP JSON e/ou WebSocket.
- **Sincronização**: Qualquer alteração de CSS ou estrutura deve ser mantida **estritamente sincronizada** entre `index.html` e `style.css`.
- **Regra Obrigatória de Armazenamento (OneDrive)**: Todas as dependências (`node_modules`), scripts, arquivos de configuração e dados devem ser mantidos 100% dentro da pasta do projeto no OneDrive (`OneDrive - Instituição Adventista de Ensino\Projetos 2026\INOVATECH\INOVATECH TCC\backend`), sem qualquer instalação global no Windows/AppData.

---

## 2. Padrão de Design do Usuário (Design System & Aesthetic Guidelines)

### A. Estética Visual
- **Linguagem**: *Dark-Tech / Industrial Blueprint / Cockpit HUD de Alta Precisão*. Combina a disciplina tipográfica suíça com a estética de telemetria aeroespacial/industrial avançada.
- **Anti-Slop (Taste-Skill integrada)**: Fuga absoluta de layouts genéricos de IA (sem gradientes roxos aleatórios, sem botões com sombras excessivas, sem caixas desproporcionais).

### B. Paleta de Cores Calibrada
- **Modo Escuro (Padrão)**:
  - Fundo geral da página (`--page`): `#020b12`
  - Fundo do contêiner (`--bg`): `#031522`
  - Cabeçalho (`--header`): `#061c2b`
  - Cor de destaque / Ciano elétrico (`--cyan`): `#08deeb`
  - Linhas e realces (`--line`): `#12e3ee`
  - Bordas técnicas (`--border`): `#0d2a3a` / `#16495b` / `#1c6073`
  - Texto principal (`--text`): `#e6f4f6`
  - Texto secundário (`--muted`): `#8ca4ad`
  - Cards e painéis (`--card`): `#0b2233` / linear gradient `(#123349, #071e2e)`
- **Modo Claro (Light Theme)**:
  - Fundo mantido idêntico ao Modo Escuro (`--page: #020b12`, `--bg: #031522`, `--header: #061c2b`, overlay escuro `.site-bg-video-overlay`), com cards em vidro claro/branco translúcido e tipografia adaptada para alto contraste.
  - Backup para reversão: Fundo original `#e5eff1`, contêiner `#f4f8f9`, cabeçalho `#ffffff`.
  - Ciano contrastante: `#087d88`
  - Texto nos cards: `#0e222a`
  - Muted nos cards: `#4a636c`
  - Cards: `#ffffff` / vidro claro com borda suave `#bfd6dc`

### C. Tipografia & Hierarquia
- **Títulos (`h1`, `h2`, `h3`, Marca)**: `'Barlow Condensed', sans-serif`, caixa alta (uppercase), tracking/letter-spacing amplo.
- **Textos Técnicos, Códigos, Rótulos e Métricas**: `'Space Mono', monospace`.
- **Rótulos dos Cards (`.card-label`)**: Sempre em caixa alta com prefixo de glifo técnico (ex: `◉`, `ϟ`, `⌁`, `◫`, `⚡`).

---

## 3. Regras Críticas da Home & Hero

1. **Vídeo de Fundo Completo (`oculos.mp4`)**:
   - O vídeo deve ocupar todo o viewport de fundo (`position: fixed; inset: 0; width: 100vw; height: 100vh; object-fit: cover`).
   - Acompanhado de overlay dinâmico com degradê vertical para garantir total legibilidade dos textos e botões tanto no modo escuro quanto claro.
2. **Sem Caixas ou Bordas Azuis no Hero**:
   - O elemento `.hero` e a seção `#home` **não devem ter bordas nem sombras azuis limitantes** (`border: none !important; box-shadow: none !important; outline: none !important;`).
3. **Preservação do `margin-top` com Fundo Transparente**:
   - **REGRA DE OURO**: O espaçamento do topo (`margin-top: 28px;`) da Home **NUNCA DEVE SER EXCLUÍDO OU ZERADO**.
   - Apenas o fundo dessa região (e dos contêineres pais `header`, `.shell`, `main`) deve ser **100% transparente** (`background: transparent !important;`), permitindo que o vídeo apareça por trás de toda a margem superior.
4. **Controle de Reprodução do Vídeo (Máximo 2 Vezes)**:
   - O vídeo não possui atributo `loop` infinito no HTML.
   - O listener em JavaScript conta o evento `ended`. Ao completar a **2ª reprodução**, o vídeo para permanentemente no último frame e **não reproduz uma 3ª vez**.

---

## 4. Estrutura das Páginas do Site

1. **`#home` (Home)**:
   - Vídeo de fundo em tela cheia com overlay protetor.
   - Hero com logo InovaTech centralizada, chamada principal e botões de ação direta.
   - Grid de 3 cards em acabamento vidro fosco (`backdrop-filter: blur(8px)`): Público-Alvo, Diferencial e Objetivo.
2. **`#painel` (Painel de Controle da Central — Antiga Telemetria)**:
   - Aba restrita no menu de navegação: `🔒 PAINEL DE CONTROLE` (`data-view="painel"`).
   - **Senha de Acesso Operacional**: `Inovatech266` com persistência em `sessionStorage` e modal de autenticação estilizado com animação de shake em caso de erro.
   - **Os 6 Cards de Controle Operacional**:
     1. **Status Geral do Motorista & Alerta Crítico**:
        - `NORMAL`: Postura erguida, azul elétrico suave (`#0052ff` / `#08deeb`).
        - `PRÉ-ALERTA`: Cabeça baixa detectada, indicador âmbar neon com cronômetro progressivo de `0.0s` a `2.5s` (2500ms).
        - `ALARME ATIVADO`: Disparado após 2,5s ininterruptos de cabeça baixa. Painel pulsa em vermelho neon, emite alarme sonoro via Web Audio API e notifica a Central.
     2. **Monitoramento Ao Vivo da Cabine (Câmera Wi-Fi Linklemo 24/7)**:
        - Integração com `navigator.mediaDevices.getDisplayMedia()` para capturar a janela do aplicativo Linklemo rodando no Windows.
        - Monitoramento contínuo visível a qualquer momento.
        - **Destaque Automático**: Quando o estado de ALARME ATIVADO é atingido, o card da câmera recebe borda vermelha piscante, zoom e selo de advertência `⚠️ VERIFICAR MOTORISTA AGORA!` para ação imediata do operador.
     3. **Telemetria do Sensor Infravermelho (GPIO 32 / D32)**:
        - Leitura lógica (HIGH = Erguida / LOW = Cabeça Baixa).
        - Cronômetro em milissegundos comparado ao limite do filtro de segurança de 2500ms.
     4. **Telemetria do Acelerômetro (I2C GPIO 21/22)**:
        - Eixos Pitch, Roll e detecção de movimentos bruscos ("pescadas" de sono).
        - Exibe estado "Aguardando módulo físico / Simulação ativa" quando não plugado.
     5. **Atuadores de Segurança & Comandos da Central (GPIO 25 e GPIO 18)**:
        - Status do Buzzer Ativo nos óculos (GPIO 25): LIGADO / DESLIGADO.
        - Status do Módulo Relé Veicular (GPIO 18): ACIONADO / EM ESPERA.
        - Botões interativos da Central: "Disparar Alerta Remoto / Teste" e "Resetar / Silenciar Alarme".
     6. **Conectividade, Contadores & Log de Eventos**:
        - Status da conexão do ESP32 (Online / Offline, timestamp do último pacote).
        - Contador total de episódios de fadiga na sessão.
        - Log cronológico em tempo real com data/hora de cada transição de postura.
        - Painel de simulação interativo no rodapé para forçar cenários no navegador.
3. **`#manual` (Manual de Operações)**:
   - Capa estilizada para o vídeo explicativo do módulo.
   - Especificação técnica da unidade central INOVATECH-500.
   - 4 passos operacionais numerados (`01` Conexão, `02` Configuração, `03` Calibração, `04` Monitoramento HUD).
4. **`#ecosystem` (Ecossistema de Segurança)**:
   - Card de resultado final com o protótipo real dos óculos montados (`oculos.png`).
   - Tabela técnica de componentes (óculos, Arduino/ESP32, webcam, sensor ultrassônico/IR, fiação, buzzer).
   - Modal interativo para adicionar novo item e cálculo dinâmico de custo total em tempo real com botão de exclusão.
5. **`#about` (Quem Somos)**:
   - Apresentação visual da equipe técnica multidisciplinar.
   - **Redes Sociais**: Apenas o botão estilizado do Instagram (`.btn-instagram`), com link direto para cada membro:
     - Matheus Souza (Hardware & Firmware)
     - Matheus Novaes (Visão Computacional & IR)
     - Otávio Lima (Biometria & Sinais Vitais)
     - Thiago Tivium (Engenharia Mecânica & 3D)
     - Matheus Garcia (Sistemas Embarcados & HUD)
     - Nathan Arkhanjo (Telemetria IoT & Cloud)
   - Formulário de contato operacional via FormSubmit (`matheusgarcia3346@gmail.com`).
6. **`#references` (Referências & Artigos Científicos)**:
   - 4 cards temáticos embasando cientificamente o projeto: ABRAMET, USP, SciELO e PRF.
7. **`#game` (O Jogo - Smart Driver)**:
   - Simulador interativo "A Jornada Noturna: Sobreviva ao Turno" desenvolvido no GDevelop.

---

## 5. Arquitetura do Backend e Comunicação em Tempo Real

### A. Estrutura de Arquivos (`backend/`)
- `backend/package.json`: Configurado com Express, ws, cors, dotenv. Scripts: `"start": "node server.js"`, `"simulate": "node test-esp32-sim.js"`.
- `backend/server.js`: Servidor unificado HTTP REST + WebSocket na porta `3001` (rota `/ws`).
  - Endpoints REST:
    - `GET /health`: Healthcheck do servidor.
    - `POST /api/telemetry`: Ingestão periódica de JSON enviado pelo ESP32.
    - `GET /api/telemetry/current`: Último pacote de telemetria recebido.
    - `GET /api/telemetry/events`: Histórico dos últimos 100 eventos com timestamp.
    - `POST /api/command`: Envio de comando da Central para o ESP32 (`TEST_ALARM`, `RESET_ALARM`).
    - `GET /api/command/pending`: Leitura de comandos enfileirados pelo ESP32.
  - Servidor WebSocket:
    - Broadcast instantâneo de cada novo pacote para todos os navegadores conectados no painel.
- `backend/test-esp32-sim.js`: Script de teste automatizado para validar o painel sem necessidade da placa física plugada.

### B. Hardware ESP32 & Lógica Temporal
- **Sensor IR de Obstáculo (GPIO 32)**: Fixado na haste dos óculos apontado para a região do queixo/peito.
  - `LOW` = Proximidade detectada (Cabeça baixa).
  - `HIGH` = Postura normalizada (Cabeça erguida).
- **Filtro de Segurança Temporal (2500 ms)**:
  - Contagem contínua via `millis()` no ESP32.
  - Se a cabeça permanecer baixa por < 2500ms: Estado `PRE_ALERTA`, sem atuar relé.
  - Ao atingir >= 2500ms: Estado `ALARME_ATIVADO`, acionamento instantâneo do Buzzer Ativo (GPIO 25) e Módulo Relé Veicular (GPIO 18).
  - Ao erguer a cabeça: Interrupção imediata dos atuadores e retorno para `NORMAL`.
- **Acelerômetro I2C (GPIO 21/22 - SDA/SCL)**: Monitoramento inercial angular (Pitch/Roll) para detecção de solavancos/pescadas de sono.

---

## 6. Planejamento da Apresentação — Semana do TI (Outubro/2026)
- **Turma**: 31C TI (Apresentação na Quarta-feira).
- **Ambiente**: Sala de aula sorteada com Computador da escola conectado a uma TV de grande porte.
- **Janela de Preparação**: Chegada às 9h00 da manhã; Início da visitação dos alunos e banca às 13h00 (4 horas para montagem, configuração e testes).
- **Rede Wi-Fi**: Cada sala terá um Wi-Fi dedicado fornecido pelo departamento de TI da escola.
- **Estratégia de Execução**:
  1. **Modo Nuvem (Render + Vercel)**:
     - Frontend na Vercel: `https://inovatech-tcc.vercel.app/`
     - Backend no Render: Web Service Node configurado a partir do repositório `Matheuseas23/inovatech_tcc` (Root Directory: `backend`, Build: `npm install`, Start: `npm start`, Plano Free).
     - Permite que a banca e os alunos acompanhem a telemetria pelo próprio celular.
  2. **Modo Local de Contingência (Infalível)**:
     - Se a internet da escola estiver instável, o backend roda diretamente no PC da sala (`npm start`), servindo o painel em `http://localhost:3001` na TV com zero lag e sem necessidade de conexão externa.
  3. **Demonstração Prática na TV**:
     - Painel em tela cheia na TV.
     - Janela do app Linklemo capturada ao vivo na cabine.
     - Integrante usando os óculos: ao abaixar a cabeça por 2,5s, o buzzer apita, o relé comuta, a TV pulsa em vermelho neon e a câmera do motorista é destacada com o alerta `⚠️ VERIFICAR MOTORISTA AGORA!`.

---

## 7. Diretrizes de Comunicação e Trabalho
- **Idioma**: Português do Brasil estritamente.
- **Execução de Comandos**: Manter comandos ao mínimo necessário, priorizando orientações claras e edições diretas nos arquivos.
- **Preservação de Contexto**: Todas as alterações, senhas e requisitos descritos neste documento são a verdade canônica do projeto.
