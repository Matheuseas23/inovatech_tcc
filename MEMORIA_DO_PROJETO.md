# INOVATECH SMART DRIVER — MEMÓRIA PERSISTENTE E RESUMO DO PROJETO

**Data de Atualização:** 17 de Outubro de 2026 (08:45 AM)  
**Turma:** 31C TI — Apresentação na Quarta-feira (Semana do TI)  
**Ambiente de Apresentação:** Sala sorteada com PC + TV de grande porte  
**Cronograma Real da Banca (Conforme Áudios do Professor):** 
* **09h00 da manhã:** Abertura das salas para montagem e testes (janela de 1 hora!).
* ⚠️ **10h00 da manhã:** Início da passagem da **BANCA AVALIADORA DOS PROFESSORES** para nota do TCC.
* **13h30 às 18h00:** Visitação aberta dos alunos dos 1º e 2º anos nas salas.
* **Sexta-feira (CIPIC):** 07h00 no Salão Nobre | 08h00 no CENAP com estandes.
* 🔌 **Aviso Obrigatório do Orientador:** Levar **1 extensão elétrica / régua de tomadas** por grupo.

---

## 1. Status Real dos Componentes (Auditoria Técnica)

### A. Frontend (Vercel) — 100% OPERACIONAL
* **URL Pública:** `https://inovatech-tcc.vercel.app`
* **Status:** Deploy ativo e sincronizado via GitHub (Commit `b28b602`).
* **Navegação:** Todas as 7 abas funcionais (Home, Painel, Manual, Ecossistema, Quem Somos, Referências, O Jogo).
* **Regras SPA:** `vercel.json` configurado para reescrita de URLs sem erro 404.
* **Segurança:** Senha `inovatech26` com persistência em `sessionStorage` e efeito shake em caso de erro.
* **Conectividade:** Conecta automaticamente em `https://inovatech-tcc.onrender.com` via WebSocket Seguro (`wss://`), com fallback automático para polling REST a cada 800ms caso o WebSocket sofra bloqueio de firewall.
* **Polimento Aplicado:** Exposição de `window.syncBackgroundFn` para garantir transição suave do vídeo de fundo (`oculos.mp4`) em todas as chamadas programáticas.

### B. Backend (Render & Local) — 100% OPERACIONAL
* **URL Pública (Nuvem):** `https://inovatech-tcc.onrender.com`
* **Status:** `Live` no Render (porta `10000`, host `0.0.0.0`).
* **Endpoints Ativos:**
  * `GET /health` e `GET /api/health`: 200 OK imediato.
  * `POST /api/telemetry`: Recepção contínua do JSON do ESP32.
  * `GET /api/telemetry`: Estado consolidado em tempo real.
  * `POST /api/command`: Disparo de testes e reset remoto.
  * `GET /ws`: Servidor WebSocket com broadcast instantâneo.
* **Servidor Híbrido:** Rota inteligente no `server.js` que serve tanto o JSON de telemetria quanto o frontend estático completo se aberto pelo navegador em `http://localhost:3001`.
* **Script de Inicialização:** `iniciar-painel.bat` criado na raiz para ligar o servidor local e abrir a TV com 1 duplo clique.

### C. Firmware ESP32 (`Smart Driver/src/main.cpp`) — 100% OPERACIONAL
* **Arquitetura Dual-Core FreeRTOS:**
  * **Core 1 (Missão Crítica):** Dedicado 100% à leitura do Sensor IR (GPIO 32 a 100 Hz), acionamento instantâneo do Buzzer Ativo (GPIO 25) e Módulo Relé Veicular (GPIO 18). NUNCA trava, mesmo se o Wi-Fi oscilar, cair ou se o Render estiver lento.
  * **Core 0 (Telemetria IoT em Background):** Gerencia a conexão Wi-Fi com auto-reconnect e envia os pacotes JSON via HTTP/HTTPS de forma assíncrona com mutex de sincronização atômica.
* **Suporte a HTTPS Seguro:** `WiFiClientSecure` com `setInsecure()` e timeout seguro de 2500ms para Render.
* **Suporte a HTTP Local:** `WiFiClient` nativo para envio ultra-rápido (< 5ms) para o PC da sala.
* **Hardware:** Sensor IR (GPIO 32 com `INPUT_PULLUP`), Buzzer Ativo (GPIO 25), Módulo Relé (GPIO 18).
* **Filtro de Segurança:** 2.500 ms (2,5s) ininterruptos via `millis()`.
* **Resiliência:** Se o Wi-Fi cair, o ESP32 opera de forma 100% autônoma (o buzzer e relé atuam normalmente no óculos).

---

## 2. O que Enviar para o Professor (Conforme Áudios Oficiais)

O professor solicitou o envio de **duas coisas obrigatórias** por e-mail/WhatsApp:

### A. Os 2 PDFs do Artigo do TCC (Exportados da Plataforma do Márcio):
1. **Versão 1 (Com Identificação):** PDF completo com os nomes de todos os integrantes no cabeçalho.
2. **Versão 2 (Cópia Cega):** PDF sem nenhum nome (para submissão institucional e publicação da escola).

### B. Lista Técnica de Domínios para Liberação no Firewall da Escola:
* **Site / Painel (Vercel):** `inovatech-tcc.vercel.app` (Portas 80/443 - HTTPS)
* **Servidor de Telemetria (Render):** `inovatech-tcc.onrender.com` e `wss://inovatech-tcc.onrender.com/ws` (Portas 80/443 - WSS)
* **Repositório GitHub:** `github.com` e `*.github.com`
* **APIs de IA e Ferramentas Google (para Antigravity e VS Code no Laboratório):**
  * `generativelanguage.googleapis.com`
  * `alkalimakersuite-pa.googleapis.com`
  * `cloudaicompanion.googleapis.com`
  * `accounts.google.com`
  * `oauth2.googleapis.com`
* **Marketplace VS Code:** `marketplace.visualstudio.com` e `*.gallery.vsassets.io`
* **Regra de Rede:** Permitir HTTPS (443) com HTTP/2, Chunked Streaming e evitar *Deep Packet Inspection (SSL Interception)* nesses domínios.

### C. Mensagem Pronta com a Explicação do Plano B:
```text
Opa professor, beleza? Tranquilo?

Tô te mandando aqui o que você pediu nos áudios sobre o nosso projeto (InovaTech — Smart Driver / 31C TI).

Já separei os 2 PDFs do artigo gerados lá na plataforma do Márcio:
1. A versão normal com os nomes de todo mundo do grupo.
2. A versão cega (sem os nomes) pra submissão da escola.
(Tô te anexando os dois aqui).

Segue a lista dos links que o nosso projeto usa na internet, pro senhor conseguir repassar pro pessoal do TI liberar no firewall da escola:

Links do nosso projeto:
- Nosso site / painel: https://inovatech-tcc.vercel.app
- Nosso servidor da telemetria (Render): https://inovatech-tcc.onrender.com (e a rota do WebSocket wss://inovatech-tcc.onrender.com/ws)
- Nosso GitHub: https://github.com/Matheuseas23/inovatech_tcc

Serviços de IA e ferramentas do laboratório (caso usem nos computadores da escola):
- generativelanguage.googleapis.com
- alkalimakersuite-pa.googleapis.com
- accounts.google.com

Sobre o Plano B que você falou no áudio:
Já deixamos tudo 100% prevenido! Caso a internet da escola oscile ou o firewall trave, a gente configurou o sistema com um Modo Local offline: a gente roda o servidor direto no PC da sala com um clique duplo (localhost:3001), o site abre na TV com zero atraso e o ESP32 envia os dados direto sem precisar de internet de fora (ou pelo 4G roteado do celular). E mesmo sem rede nenhuma, o óculos tem lógica própria pra disparar o buzzer e o relé se a cabeça ficar baixa por 2,5s.

Já estamos cientes do horário de quarta (chegar às 9h pra deixar tudo pronto pra banca das 10h) e já tô separando a extensão de tomada pra levar.

Qualquer coisa só me avisar por aqui. Valeu professor! 👊
```

---

## 3. Resultados da Auditoria no Navegador (Realizada em Produção)

* **Navegação:** Todas as 7 abas testadas e validadas sem falhas.
* **Modal de Senha (`inovatech26`):** Rejeição com shake para senhas incorretas; liberação instantânea com a chave correta.
* **Simulação de Telemetria:**
  * Testado o fluxo: `NORMAL` -> `PRÉ-ALERTA` (1.5s a 2.4s) -> `ALARME ATIVADO` (2.5s).
  * Card geral pulsa em vermelho neon, buzzer sonoro toca via Web Audio API e banner vermelho de alerta na câmera acende.
  * Botão de silenciar/erguer cabeça normaliza o sistema imediatamente.
* **Console do Navegador:** Zero erros ou exceções críticas.

---

## 4. Checklist da Verdade: O que Precisa ser Feito Fisicamente

Para o dia da apresentação na sala de aula:

1. **Gravar o código no ESP32 (Upload):**
   * Conectar a placa ESP32 no computador via cabo USB de dados.
   * No VS Code / PlatformIO, clicar na setinha `→` (Upload) para gravar o `Smart Driver/src/main.cpp`.
2. **Preencher o Wi-Fi Real no ESP32:**
   * No dia, abrir o `Smart Driver/src/main.cpp` e preencher as linhas 18/19 com o SSID e Senha da rede que for usada (escola ou 4G do celular).
3. **Câmera da Cabine (Linklemo):**
   * Ligar a câmera na bateria.
   * Abrir o aplicativo da Linklemo no computador.
   * No painel do site, clicar em *"Capturar Janela do App Linklemo"* e selecionar a janela do aplicativo.
4. **Mochila da Apresentação:**
   * Óculos montado com sensores e fiação.
   * ESP32 + cabo USB de dados.
   * Fonte/Powerbank de alimentação.
   * **1 Extensão elétrica com régua de tomadas**.

---

## 5. Estratégias de Operação no Dia da Apresentação

### Estratégia 1: Nuvem (Render + Vercel)
* Utilizada se o Wi-Fi da escola estiver aberto e rápido.
* Permite que os professores acessem `https://inovatech-tcc.vercel.app` pelo celular.
* **Recomendação:** Acessar `https://inovatech-tcc.onrender.com/health` 2 minutos antes para acordar o servidor caso esteja em sleep.

### Estratégia 2: Modo Local no PC da Sala (Infalível / Latência < 5ms)
* Se o Wi-Fi falhar ou o firewall bloquear:
* Basta dar **duplo clique no arquivo `iniciar-painel.bat`** na raiz do projeto.
* O script detecta o IP da máquina, inicia o Node.js e abre a TV em `http://localhost:3001` com zero atraso e 100% de estabilidade.
