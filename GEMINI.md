# INOVATECH SMART DRIVER — MEMÓRIA DO PROJETO & DIRETRIZES DE DESIGN

Este arquivo serve como memória persistente e base de treinamento do assistente para todas as interações no projeto **INOVATECH SMART DRIVER (TCC)**. Todas as regras, padrões estéticos e preferências do usuário aqui documentados devem ser seguidos rigorosamente.

---

## 1. Visão Geral do Projeto
- **Nome**: INOVA TECH — Smart Driver
- **Objetivo**: Sistema de monitoramento biométrico e cognitivo em tempo real para prevenção de acidentes e combate à fadiga em motoristas profissionais e operadores industriais.
- **Arquitetura Técnica**: Site estático puro (HTML5 + CSS3 + Vanilla JavaScript), compatível com a extensão **Live Server ("Go Live")** do VS Code sem dependência de build tools.
- **Sincronização**: Qualquer alteração de CSS ou estrutura deve ser mantida **estritamente sincronizada** entre `index.html` e `style.css`.

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
  - Fundo geral: `#dce8ea` / `#efefef`
  - Contêiner: `#eef5f6` / `#f5f5f5`
  - Cabeçalho: `#ffffff`
  - Ciano contrastante: `#087d88` / `#0066cc`
  - Texto: `#10252b` / `#1a1a1a`
  - Muted: `#52686e` / `#666666`
  - Cards: `#ffffff` com borda suave `#b9ced1`

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
2. **`#telemetry` (Página Especial de Telemetria)**:
   - Aba exclusiva no menu de navegação (`TELEMETRIA`).
   - Relógio operacional ao vivo (`#telemetryClock`) atualizado a cada segundo em tempo real via JavaScript.
   - Cards biométricos vitais: Frequência Cardíaca (BPM), Presença Cognitiva e Índice de Fadiga (PERCLOS).
   - 3 módulos explicativos: Transmissão BLE, Algoritmo PERCLOS e Resposta Háptica & Sonora.
3. **`#manual` (Manual de Operações)**:
   - Capa estilizada para o vídeo explicativo do módulo.
   - Especificação técnica da unidade central INOVATECH-500.
   - 4 passos operacionais numerados (`01` Conexão, `02` Configuração, `03` Calibração, `04` Monitoramento HUD).
4. **`#ecosystem` (Ecossistema de Segurança)**:
   - Card de resultado final com o protótipo real dos óculos montados (`oculos.png`).
   - Tabela técnica de componentes (óculos, Arduino, webcam, sensor ultrassônico, fiação, microfone).
   - Modal interativo para adicionar novo item e cálculo dinâmico de custo total em tempo real com botão de exclusão.
5. **`#about` (Quem Somos)**:
   - Apresentação visual da equipe técnica multidisciplinar.
   - **Redes Sociais**: **Apenas o botão estilizado do Instagram** (`.btn-instagram`), com link direto para cada membro:
     - **Matheus Souza** (Hardware & Firmware): `https://www.instagram.com/m.souzavl/?utm_source=ig_web_button_share_sheet`
     - **Matheus Novaes** (Visão Computacional & IR): `https://www.instagram.com/matheusoliveira__17/?utm_source=ig_web_button_share_sheet`
     - **Otávio Lima** (Biometria & Sinais Vitais): `https://www.instagram.com/_otxxvio/?utm_source=ig_web_button_share_sheet`
     - **Thiago Tivium** (Engenharia Mecânica & 3D): `https://www.instagram.com/t.tivium/?utm_source=ig_web_button_share_sheet`
     - **Matheus Garcia** (Sistemas Embarcados & HUD): `https://www.instagram.com/_mth.garcia/`
     - **Nathan Arkhanjo** (Telemetria IoT & Cloud): Instagram oficial
   - Formulário de contato operacional via FormSubmit (`matheusgarcia3346@gmail.com`).
6. **`#references` (Referências & Artigos Científicos)**:
   - 4 cards temáticos embasando cientificamente o projeto: ABRAMET (riscos da fadiga), USP (morbimortalidade), SciELO (distúrbios do sono) e PRF (Lei do Descanso).
   - Botões de acesso direto aos portais e artigos científicos.
7. **`#game` (O Jogo - Smart Driver)**:
   - Simulador interativo "A Jornada Noturna: Sobreviva ao Turno" desenvolvido no GDevelop.
   - Área reservada para vídeo demonstrativo e botão de acesso direto à plataforma web do jogo.

---

## 5. Diretrizes de Comunicação e Trabalho
- **Idioma**: Todas as respostas devem ser fornecidas em **Português**, mantendo tom técnico, claro e colaborativo.
- **Execução de Comandos**: Não executar comandos no terminal de forma desnecessária; priorizar edições diretas nos arquivos de código (`index.html`, `style.css`).
- **Preservação de Contexto**: Manter sempre a integridade dos dados e regras previamente validados pelo usuário.
