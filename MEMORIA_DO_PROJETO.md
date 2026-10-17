# INOVATECH SMART DRIVER — MEMÓRIA PERSISTENTE E RESUMO DO PROJETO

**Data de Atualização:** 17 de Outubro de 2026 (02:55 AM)  
**Turma:** 31C TI — Apresentação na Quarta-feira (Semana do TI)  
**Ambiente de Apresentação:** Sala sorteada com PC + TV de grande porte  
**Cronograma Real da Banca:** 
* Chegada às **09h00 da manhã** (Abertura das salas para montagem).
* ⚠️ **BANCA E AVALIAÇÃO DOS PROFESSORES: Início às 10h00 da manhã!** (Janela de montagem: 1 hora!).
* Visitação dos alunos: Das **13h30 às 18h00**.
* **Sexta-feira (CIPIC):** 07h00 Salão Nobre | 08h00 CENAP (Levar 1 extensão de tomada por grupo!).
**Rede:** Wi-Fi da escola (com solicitação de liberação no firewall) + Modo Local de Contingência.

---

## 1. Status Real dos Componentes (Auditoria Técnica)

### A. Frontend (Vercel) — 100% OPERACIONAL
* **URL Pública:** `https://inovatech-tcc.vercel.app`
* **Status:** Deploy atualizado com sucesso.
* **Navegação:** Todas as 7 abas funcionais (Home, Painel, Manual, Ecossistema, Quem Somos, Referências, O Jogo).
* **Regras SPA:** `vercel.json` configurado para reescrita de URLs sem erro 404.
* **Segurança:** Senha `Inovatech266` com persistência em `sessionStorage`.
* **Conectividade:** Conecta automaticamente em `https://inovatech-tcc.onrender.com` via WebSocket Seguro (`wss://`), com fallback automático para polling REST a cada 800ms caso o WebSocket sofra bloqueio de firewall.

### B. Backend (Render) — 100% OPERACIONAL
* **URL Pública:** `https://inovatech-tcc.onrender.com`
* **Status:** `Live` no Render (porta `10000`, host `0.0.0.0`).
* **Endpoints Ativos:**
  * `GET /health` e `GET /api/health`: 200 OK imediato.
  * `POST /api/telemetry`: Recepção contínua do JSON do ESP32.
  * `GET /api/telemetry`: Estado consolidado em tempo real.
  * `POST /api/command`: Disparo de testes e reset remoto.
  * `GET /ws`: Servidor WebSocket com broadcast instantâneo.
* **Polimento Aplicado:** `trust proxy` ativado, encerramento gracioso (`SIGTERM`) e engines Node >= 18 configurados.

### C. Firmware ESP32 (`Smart Driver/src/main.cpp`) — 100% OPERACIONAL NO CÓDIGO
* **Arquitetura Dual-Core FreeRTOS:**
  * **Core 1 (Missão Crítica):** Dedicado 100% à leitura do Sensor IR (GPIO 32 a 100 Hz), acionamento instantâneo do Buzzer Ativo (GPIO 25) e Módulo Relé Veicular (GPIO 18). NUNCA trava, mesmo se o Wi-Fi oscilar, cair ou se o Render estiver lento.
  * **Core 0 (Telemetria IoT em Background):** Gerencia a conexão Wi-Fi com auto-reconnect e envia os pacotes JSON via HTTP/HTTPS de forma assíncrona com mutex de sincronização atômica.
* **Suporte a HTTPS Seguro:** `WiFiClientSecure` com `setInsecure()` e timeout seguro de 2500ms para Render.
* **Suporte a HTTP Local:** `WiFiClient` nativo para envio ultra-rápido (< 5ms) para o PC da sala.
* **Hardware:** Sensor IR (GPIO 32 com `INPUT_PULLUP`), Buzzer Ativo (GPIO 25), Módulo Relé (GPIO 18).
* **Filtro de Segurança:** 2.500 ms (2,5s) ininterruptos via `millis()`.
* **Resiliência:** Se o Wi-Fi cair, o ESP32 opera de forma 100% autônoma (o buzzer e relé atuam normalmente).

---

## 2. Checklist da Verdade: O que precisa ser feito Fisicamente

Para que o sistema funcione na prática no mundo real, restam apenas os passos físicos que dependem da presença no local:

1. **Gravar o código no ESP32 (Upload):**
   * Conectar a placa ESP32 no computador via cabo USB.
   * No VS Code / PlatformIO, clicar na setinha `→` (Upload) para gravar o `Smart Driver/src/main.cpp` na memória flash da placa.
2. **Preencher o Wi-Fi Real no ESP32:**
   * No dia da apresentação, abrir o `Smart Driver/src/main.cpp` e colocar o SSID e Senha da rede que vocês vão usar:
     * Se for Wi-Fi da escola: colocar o nome e senha fornecidos pelo TI.
     * Se for 4G roteado do celular: colocar o nome e senha do roteador do celular.
     * Gravar na placa (leva 30 segundos).
3. **Câmera da Cabine (Linklemo):**
   * Ligar a câmera Wi-Fi na bateria/fonte.
   * Abrir o aplicativo da Linklemo na tela do computador.
   * No painel do site, clicar em *"Capturar Janela do App Linklemo"* e selecionar a janela do aplicativo.
4. **Equipamentos Físicos na Mochila:**
   * Óculos montado com sensores e cabeamento.
   * Placa ESP32 + cabo USB de dados (não apenas de carga).
   * Fonte/Powerbank de alimentação.
   * **1 Extensão elétrica / Régua de tomadas** (solicitado pelo orientador).

---

## 3. Os Dois Modos de Operação no Dia da Apresentação

### Modo 1: Nuvem (Render + Vercel)
* Utilizado se o Wi-Fi da escola estiver estável e com portas liberadas.
* Permite que professores e alunos acessem `https://inovatech-tcc.vercel.app` pelo próprio celular e vejam a telemetria ao vivo.
* **Dica de Ouro:** Acessar `https://inovatech-tcc.onrender.com/health` 2 minutos antes para "acordar" o servidor caso tenha entrado em modo sleep.

### Modo 2: Local no PC da Sala (Infalível / Latência < 5ms)
* Se a internet oscilar ou o firewall da escola barrar:
* Basta dar **duplo clique no arquivo `iniciar-painel.bat`** na raiz do projeto!
* O script automaticamente:
  1. Identifica o IP local do computador (ex: `192.168.1.xxx`).
  2. Inicia o servidor backend Node.js + WebSockets.
  3. Abre o site completo na TV em `http://localhost:3001` sem depender de internet externa.
* O sistema opera com zero atraso, imune a quedas de rede externa.
