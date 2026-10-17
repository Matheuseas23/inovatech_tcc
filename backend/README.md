# INOVA TECH SMART DRIVER — BACKEND & TELEMETRIA EM TEMPO REAL

Backend oficial em Node.js (Express + WebSockets) para o projeto **InovaTech Smart Driver (TCC)**. Responsável por processar a telemetria do microcontrolador ESP32, aplicar o filtro de segurança temporal de 2,5 segundos e transmitir dados e alertas instantaneamente para o **Painel de Controle da Central** (seja na Vercel ou rodando localmente).

---

## 1. Como Executar Localmente

### Pré-requisitos
- Node.js instalado (v18 ou superior).
- Todo o código e dependências estão armazenados **100% dentro desta pasta no OneDrive** (`INOVATECH TCC/backend`).

### Passos de Inicialização:
1. Abra um terminal na pasta `backend`:
   ```bash
   cd "backend"
   ```
2. Instale as dependências (já instaladas localmente no OneDrive):
   ```bash
   npm install
   ```
3. Inicie o servidor:
   ```bash
   npm start
   ```
   *Ou para desenvolvimento com auto-reload:*
   ```bash
   npm run dev
   ```

O servidor iniciará em:
- **HTTP REST API:** `http://localhost:3001`
- **WebSocket:** `ws://localhost:3001/ws`

---

## 2. Como Testar Sem o ESP32 Físico (Simulador Integrado)

Com o servidor rodando em um terminal, abra um segundo terminal na pasta `backend` e execute:
```bash
npm run simulate
```
Isso iniciará um simulador inteligente que envia pacotes contínuos simulando o motorista focado, o adormecimento com queda de cabeça (atingindo os 2.500 ms) e o disparo do alarme com normalização subsequente.

---

## 3. Como Disponibilizar Online Gratuitamente (Deploy Remoto)

Para que o ESP32 possa enviar dados pela internet de qualquer lugar (mesmo usando o 4G do celular ou Wi-Fi externo) e o site na Vercel receba tudo:

### Opção A: Render.com (Recomendada - Grátis)
1. Crie uma conta gratuita em [render.com](https://render.com).
2. Clique em **New +** -> **Web Service**.
3. Conecte seu repositório GitHub (`Matheuseas23/inovatech_tcc`).
4. Configure os campos:
   - **Root Directory:** `backend`
   - **Environment:** `Node`
   - **Build Command:** `npm install`
   - **Start Command:** `node server.js`
   - **Instance Type:** `Free`
5. Clique em **Create Web Service**.
6. O Render fornecerá uma URL HTTPS (ex: `https://inovatech-backend.onrender.com`).
   - Sua API REST será: `https://inovatech-backend.onrender.com/api/telemetry`
   - Seu WebSocket será: `wss://inovatech-backend.onrender.com/ws`

### Opção B: Túnel Rápido para Bancas (ngrok / localtunnel)
Para demonstrações imediatas com o backend rodando no seu computador:
```bash
npx localtunnel --port 3001
```
Gera uma URL pública imediata na internet para usar no ESP32 e na Vercel.

---

## 4. Documentação de Endpoints da API

| Método | Endpoint | Descrição | Exemplo de Payload / Resposta |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/health` | Verifica integridade do backend | `{ "status": "ONLINE", "uptime_segundos": 120 }` |
| `GET` | `/api/telemetry`| Retorna estado atual consolidado | `{ "estado": "NORMAL", "buzzer": 0, ... }` |
| `POST`| `/api/telemetry`| ESP32 envia leituras de telemetria | `{"sensor_ir": 0, "tempo_cabeca_baixa_ms": 1200}` |
| `POST`| `/api/command`  | Central envia comando para ESP32 | `{"comando": "DISPARAR_TESTE"}` |
| `GET` | `/api/commands` | ESP32 consome comandos pendentes | `{"comandos": [...]}` |
| `GET` | `/api/events`   | Histórico cronológico de eventos | `[{"id": "...", "tipo": "ALARME_ATIVADO"}]` |
| `WS`  | `/ws`           | Conexão WebSocket bidirecional | Mensagens JSON em tempo real (< 20ms) |

---

## 5. Código de Exemplo para o ESP32 (`src/main.cpp`)

No microcontrolador ESP32, basta incluir a biblioteca de Wi-Fi e HTTP para enviar a telemetria ao backend:

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* WIFI_SSID     = "SUA_REDE_WIFI";
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";

// URL do Backend (local na mesma rede ou online no Render):
// Exemplo Local: "http://192.168.1.15:3001/api/telemetry"
// Exemplo Remoto: "https://inovatech-backend.onrender.com/api/telemetry"
const char* BACKEND_URL   = "http://192.168.1.15:3001/api/telemetry";

void enviarTelemetriaWeb(int ir, unsigned long tempoMs, const char* status, int buzzer, int rele) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(BACKEND_URL);
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<256> doc;
    doc["dispositivo"] = "ESP32_SMART_DRIVER_01";
    doc["sensor_ir"] = ir;
    doc["tempo_cabeca_baixa_ms"] = tempoMs;
    doc["estado"] = status;
    doc["buzzer"] = buzzer;
    doc["rele"] = rele;

    String jsonStr;
    serializeJson(doc, jsonStr);

    int httpResponseCode = http.POST(jsonStr);
    http.end();
  }
}
```
