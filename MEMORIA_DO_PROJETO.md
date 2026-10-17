# INOVATECH SMART DRIVER — MEMÓRIA PERSISTENTE E RESUMO DO PROJETO

**Data de Atualização:** 17 de Outubro de 2026  
**Turma:** 31C TI — Apresentação na Quarta-feira (Semana do TI)  
**Ambiente de Apresentação:** Sala sorteada com PC + TV de grande porte  
**Janela de Preparação:** Chegada às 9h00 da manhã | Apresentação às 13h00 (4h de montagem e testes)  
**Rede:** Wi-Fi dedicado por sala fornecido pelo TI da escola  

---

## 1. O que foi Desenvolvido e Implementado

### A. Backend Unificado (Node.js + Express + WebSocket)
- **Localização:** Pasta `backend/` dentro do projeto no OneDrive (`INOVATECH TCC/backend`).
- **Porta Padrão:** `3001` (com suporte automático à variável `process.env.PORT` para o Render).
- **Protocolo em Tempo Real:** WebSocket nativo no endpoint `/ws` com broadcast instantâneo de pacotes para o frontend.
- **Endpoints REST Operacionais:**
  - `GET /health`: Healthcheck do servidor.
  - `POST /api/telemetry`: Recepção contínua da telemetria enviada pelo ESP32 a cada 200ms.
  - `GET /api/telemetry/current`: Último estado registrado do motorista.
  - `GET /api/telemetry/events`: Histórico das últimas 100 mudanças de postura.
  - `POST /api/command`: Envio de comandos da Central para o ESP32 (`TEST_ALARM`, `RESET_ALARM`).
  - `GET /api/command/pending`: Fila de comandos pendentes consumida pelo ESP32.
- **Simulador Realista:** `test-esp32-sim.js` (`npm run simulate`), simulando postura normal, cabeça baixa, cronômetro progressivo até 2,5s, disparo de alarme e retorno à postura erguida.

### B. Novo Painel de Controle da Central (`#painel`)
- **Proteção por Senha:** Chave de acesso `Inovatech266` com persistência na sessão (`sessionStorage`) e modal de autenticação estilizado.
- **Os 6 Cards Operacionais:**
  1. **Status Geral do Motorista & Alerta Crítico:**
     - `NORMAL`: Azul Elétrico (`#0052ff` / `#08deeb`).
     - `PRÉ-ALERTA`: Âmbar Neon com cronômetro progressivo de `0.0s` a `2.5s` e barra visual.
     - `ALARME ATIVADO`: Vermelho Neon pulsante com alarme sonoro via Web Audio API.
  2. **Câmera Wi-Fi Linklemo da Cabine (24/7):**
     - Botão para capturar a janela do app Linklemo no PC via `navigator.mediaDevices.getDisplayMedia()`.
     - Destaque automático imediato com borda vermelha piscante e aviso `⚠️ VERIFICAR MOTORISTA AGORA!` ao disparar o alarme de 2,5s.
  3. **Sensor Infravermelho IR (GPIO 32):**
     - Leitura lógica (HIGH = Erguida / LOW = Cabeça Baixa) e contador em milissegundos.
  4. **Acelerômetro I2C (GPIO 21/22):**
     - Monitoramento inercial dos eixos Pitch / Roll e detecção de "pescadas" de sono.
  5. **Atuadores & Comandos da Central:**
     - Monitoramento do Buzzer (GPIO 25) e Relé (GPIO 18) com botões de Teste e Silenciar.
     - Botões da Central para teste remoto e reset.
  6. **Conectividade e Histórico de Eventos:**
     - Status da placa (Online/Offline), contador de alarmes e log cronológico com timestamp.

---

## 2. Hardware ESP32 & Lógica de Segurança

- **Localização do Firmware:** Pasta `Smart Driver/` unificada dentro do repositório principal (Monorepo), com configuração automática no `.vscode/settings.json` para o PlatformIO.
- **Sensor IR de Obstáculo:** GPIO 32 (LOW = cabeça baixa / aproximação peito/queixo).
- **Filtro Temporal de Segurança:** 2.500 ms (2,5 segundos) ininterruptos contados via `millis()`.
- **Atuadores Imediatos:** Buzzer Ativo (GPIO 25) e Módulo Relé Veicular (GPIO 18) acionados somente após 2500ms contínuos.
- **Corte Imediato:** Qualquer retorno à postura erguida (HIGH) desliga imediatamente o buzzer e o relé.
- **Telemetria Contínua:** Envio periódico via HTTP POST a cada 250ms para `/api/telemetry` com timeout rápido de 150ms (modo autônomo e resiliente a quedas de Wi-Fi).

---

## 3. Guia de Execução e Apresentação na Semana do TI

### Estratégia 1: Nuvem (Render + Vercel)
- **Frontend Vercel:** `https://inovatech-tcc.vercel.app/`
- **Backend Render:**
  - Repositório: `Matheuseas23/inovatech_tcc`
  - Language: `Node`
  - Root Directory: `backend`
  - Build Command: `npm install`
  - Start Command: `npm start`
  - Instance Type: `Free` ($0/mês)

### Estratégia 2: Modo Local no PC da Sala (Infalível contra quedas de internet)
- Na sala sorteada, no PC conectado à TV, abrir a pasta no terminal e rodar:
  ```powershell
  cd backend
  npm start
  ```
- O servidor roda em `http://localhost:3001` com zero atraso (latência < 5ms).
- O ESP32 envia para o IP local do PC na rede da sala.

---

## 4. Regra de Ouro do Repositório (OneDrive)
Todas as dependências (`node_modules`), pacotes e arquivos de configuração ficam **100% dentro da pasta do projeto no OneDrive**, garantindo portabilidade entre qualquer computador da equipe sem necessidade de instalações globais no Windows.
