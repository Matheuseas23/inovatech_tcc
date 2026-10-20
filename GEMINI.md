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
   - **Senha de Acesso Operacional**: `inovatech26` com persistência em `sessionStorage` e modal de autenticação estilizado com animação de shake em caso de erro.
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
- **Persistência Integral de Dados**:
  - **No Servidor**: Todos os eventos e telemetrias são gravados em `backend/data/historico_eventos.json` e `backend/data/ultimo_estado.json` de forma contínua, sobrevivendo a reinicializações.
  - **No Site (Navegador)**: Eventos, logs e contadores de alarmes são persistidos no `localStorage` do navegador, com sincronização bidirecional WebSocket/REST e botão de exportação oficial (.JSON).
  - **Privacidade Operacional**: Painel restrito ao grupo via chave `inovatech26`.

- **Firmware Dual-Core FreeRTOS**:
  - **Core 1 (Tempo Real Estrito a 100 Hz)**: Sensor IR (GPIO 32), Buzzer (GPIO 25), Relé (GPIO 18) e filtro de segurança (2500 ms). Totalmente independente de rede, imune a quedas de Wi-Fi ou lag.
  - **Core 0 (Telemetria IoT em Background)**: Conexão Wi-Fi com auto-reconnect e envio periódico de telemetria para Render (HTTPS com `WiFiClientSecure` e `setInsecure()`) ou servidor Local (HTTP).
- **Lançador Local de Contingência (`iniciar-painel.bat`)**:
  - Script com duplo clique que detecta o IP local, inicia o Node.js e abre o frontend na TV em `http://localhost:3001` com zero latência.
- **Entregas Solicitadas pelo Professor**:
  - 2 cópias em PDF do artigo científico (1 com nomes, 1 cópia cega sem nomes).
  - Lista de domínios para liberação no firewall da escola (Vercel, Render, GitHub, APIs Google).
  - Levar 1 extensão elétrica com régua de tomadas por grupo.
- **Demonstração Prática na TV**:
  - Painel em tela cheia na TV.
  - Janela do app Linklemo capturada ao vivo na cabine.
  - Integrante usando os óculos: ao abaixar a cabeça por 2,5s, o buzzer apita, o relé comuta, a TV pulsa em vermelho neon e a câmera do motorista é destacada com o alerta `⚠️ VERIFICAR MOTORISTA AGORA!`.

---

## 7. Diretrizes de Comunicação e Trabalho
- **Idioma**: Português do Brasil estritamente.
- **Execução de Comandos**: Manter comandos ao mínimo necessário, priorizando orientações claras e edições diretas nos arquivos.
- **Preservação de Contexto**: Todas as alterações, senhas e requisitos descritos neste documento são a verdade canônica do projeto.

---

## 8. Status Atual de Hardware & Gravação da ESP32 (Outubro/2026)
- **Status do Código & Frontend**: 100% revisado, polido, validado no navegador e commitado no GitHub (`origin/main`).
- **Autenticação**: Senha mestra do sistema `inovatech26` (minúsculo).
- **Diagnóstico USB Realizado**:
  - Testado diretamente na USB do notebook: a placa ESP32 recebe energia e acende o LED perfeitamente.
  - O Windows não tocou o som de conexão USB e nenhuma porta COM apareceu porque o cabo utilizado é **apenas de carga** (sem os fios internos de dados D+/D-).
- **Próximo Passo Imediato (ao religar o PC)**:
  1. Conectar a ESP32 no notebook usando um cabo USB de dados (ex: cabo de celular que passa fotos/arquivos).
  2. Ouvir o som de conexão do Windows (*"turu-tum"*).
  3. Assistente detectará a porta COM ativa (ex: `COM3`, `COM4`) e executará o upload do firmware pelo PlatformIO (`Smart Driver/src/main.cpp`).

---

## 9. Arquitetura Modular Tripla & Filtros de Precisão (Registro Oficial)
- **Data e Horário de Homologação**: Segunda-feira, 20 de Outubro de 2026, 00:30 BRT.
- **Divisão do Sistema em 3 Páginas Autônomas (Zero pastas externas)**:
  1. **`index.html` (Apresentação do Projeto para o Professor/Banca)**:
     - Portal público contendo: Home com Hero e vídeo, Manual de Operações, Ecossistema com tabela de custos, Quem Somos (Equipe com Instagram oficial de cada membro), Referências Científicas e O Jogo no GDevelop.
     - O painel de controle e telemetria pesada de rede foram removidos desta página para garantir que professores acessando via QR Code vejam unicamente o site de apresentação acadêmica do TCC, com navegação fluida e sem consumir banda da rede escolar.
     - **Modal Seletor Inicial (`#portalAccessModal`)**: Modal visual Dark-Tech que surge ao abrir a página com 3 botões estilizados:
       - *[ Apresentação do Projeto (Acesso Livre) ]*: Fecha o modal e navega normalmente na documentação.
       - *[ Central Tech (Torre de Controle) ]*: Exibe ícone de cadeado (`lock`), solicita senha `inovatech26` e redireciona para `central.html`.
       - *[ App do Motorista (Cabine Veicular) ]*: Exibe ícone de cadeado (`lock`), solicita senha `inovatech26` e redireciona para `motorista.html`.
       - **Chave de Ocultação Temporária**: Controlada pela flag `const MOSTRAR_PORTAL_SELETOR = true;` no JavaScript de `index.html`. Quando a banca do professor for iniciar, basta definir como `false` para que qualquer pessoa com o link/QR Code entre direto na apresentação sem ver a caixa de pergunta.
  2. **`central.html` (CENTRAL TECH — Torre de Controle Operacional para TV/PC)**:
     - **Seleção de Frota Dinâmica**:
       - 🟢 **Matheus Garcia**: Veículo Scania R-450 Highline (Placa INO-2026) na Rodovia BR-116 KM 284 -> **EM OPERAÇÃO** (Único acessível na demonstração). Ao clicar no card, abre instantaneamente o Cockpit de Monitoramento Ao Vivo.
       - ⚪ **Matheus Oliveira**: Inativo / Fora de Turno (Acesso Bloqueado com ícone de cadeado).
       - ⚪ **Nathan Arkhanjo**: Inativo / Fora de Turno (Acesso Bloqueado com ícone de cadeado).
       - ⚪ **Thiago Tivium**: Inativo / Fora de Turno (Acesso Bloqueado com ícone de cadeado).
     - **Cockpit Completo de 6 Cards**:
       - Card 1: Estado do Condutor em Tempo Real com tolerância de 4,00s e recuperação automática de 3,00s.
       - Card 2: Câmera Wi-Fi Linklemo da Cabine ao vivo 24/7 com alerta vermelho piscante e selo de intervenção em emergência.
       - Card 3: Telemetria IR (GPIO 32) com contagem precisa de milissegundos.
       - Card 4: Telemetria do Acelerômetro (I2C Pitch/Roll).
       - Card 5: Atuadores de Segurança (Buzzer GPIO 25 e Relé GPIO 18) com botões remotos de teste e silenciamento.
       - Card 6: Conectividade WebSocket/REST, contadores de alarmes e log cronológico em tempo real.
     - **Protocolo de Escalação Crítica SOS em 3 Níveis Calibrado**:
       - Nível 1: Cabeça baixa > 4,00s -> Alarme sonoro veicular, ativação do Buzzer nos óculos e áudio de voz feminina no app alertando sonolência após 4 segundos. Cronômetro dinâmico de 0.0s a 20.0s.
       - Nível 2: Persistência por **20,0s** sem reação -> **SUSPEITA DE ACIDENTE**: Notificação de Alerta Crítico na Central, acionamento do display de **Localização Satelital GNSS em Tempo Real** com mapa interativo dark-tech indicando as coordenadas do Scania R-450 na Rodovia BR-116 KM 284.
       - Nível 3: Persistência por **35,0s** sem reação -> **PROTOCOLO DE EMERGÊNCIA MÁXIMA SOS & INTERVENÇÃO IMEDIATA**: Painel entra em modo SOS com sirene militar contínua, corte gradual de tração via Relé GPIO 18 (Safe-Stop de 64 km/h a 0 km/h), acionamento de pisca-alerta/buzina e despacho automático para resgate rodoviário SAMU 192 / PRF.
  3. **`motorista.html` (App do Motorista — Cabine Veicular para Smartphone)**:
     - Interface tática otimizada para fixação no suporte do celular no painel do caminhão.
     - Identificação de condutor: *Matheus Garcia (ID: DRV-0194)*, placa *INO-2026* e status de ignição conectada.
     - Botão ergonômico para Ligar/Desligar Turno de Operação com feedback sonoro imediato, sintetização de voz de início de viagem e sincronização instantânea em tempo real com a Central Tech.
     - Card de Estado do Condutor ao vivo em formato gigante e legível à distância com tolerância de 4,00s e recuperação automática de 3,00s.
     - Botão minimalista de **Viagem Noturna / Apagar Tela** contendo estritamente o ícone do smartphone (`smartphone`), sem texto.
     - **Alerta Coffee Call em Tela Cheia**: Ao atingir a fadiga, cobre a tela com visual pulsante e toca mensagem sintetizada em voz feminina em português sincronizada com vibração contínua no celular e cronômetro de confirmação calibrado em 20.0s (com indicação de Suspeita de Acidente após 20s e SOS aos 35s).
- **Calibração de Tolerância e Recuperação Temporal**:
  - **Tolerância Inicial de Cabeça Baixa**: **4,00 segundos (4000 ms)** tanto no backend Node.js (`server.js`) quanto nos scripts do navegador. Evita falsos positivos em pequenas olhadas no painel do caminhão.
  - **Tempo de Suspeita de Acidente na Central (Mapa GNSS Satelital)**: **20,0 segundos** ininterruptos de alarme sem reação.
  - **Tempo de Protocolo SOS Máximo & Safe-Stop**: **35,0 segundos** ininterruptos de alarme sem reação.
  - **Cálculo de Tempo Relativo Seguro**: Tempo calculado estritamente de forma relativa com sanitização contra clock jump e timestamps de época (evitando o bug de valores astronômicos).
  - **Recuperação Automática de Postura**: Se o motorista mantiver a cabeça erguida por **3,00 segundos contínuos**, todos os alarmes (sonoros, visuais, Coffee Call e vibração) cessam instantaneamente de forma automática, dispensando a necessidade de apertar a tela durante a condução.
- **Diretriz de Design Rigorosa (Ícones vs Emojis)**: Proibido o uso de emojis gráficos Unicode na interface. Todos os símbolos operacionais utilizam **Google Fonts Material Symbols** (`lock`, `visibility`, `dashboard`, `smartphone`, `local_cafe`, `power_settings_new`, `pan_tool`, `location_on`, `medical_services`, `support_agent`).


