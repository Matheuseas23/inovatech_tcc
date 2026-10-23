# INOVA TECH SMART DRIVER — BACKEND & TELEMETRIA EM TEMPO REAL

Backend oficial em Node.js (Express + WebSocket + UDP) do projeto **InovaTech Smart Driver (TCC)**. Recebe a telemetria da ESP32 (óculos), controla o alerta travado com escalonamento (crítico aos 9 s, possível acidente aos 15 s) e sincroniza em tempo real a **Central Tech** (`central.html`) e o **App do Motorista** (`motorista.html`).

> **Regra de ouro:** quem decide NORMAL / PRÉ-ALERTA / ALARME é a ESP32 (tolerância de 3,5 s). O servidor não cronometra a cabeça baixa por conta própria; ele só cuida do alerta travado, do escalonamento e da recuperação automática (cabeça erguida por 3 s).

---

## 1. Como Executar Localmente (dia da apresentação)

- **Jeito mais fácil:** duplo clique em `iniciar-painel.bat` (raiz do projeto). Ele libera o firewall, instala as dependências na primeira vez, sobe o servidor e abre a Central.
- **Pelo terminal**, na pasta `backend`:
  ```bash
  npm install   # só na primeira vez (fica tudo dentro de backend/node_modules)
  npm start
  ```

Endereços:
- **Central Tech (TV/PC):** `http://localhost:3001/central.html`
- **App do Motorista (celular na mesma rede):** `http://IP-DO-PC:3001/motorista.html` (o servidor lista os IPs ao iniciar)
- **WebSocket:** `ws://localhost:3001/ws`
- **UDP da ESP32:** porta `3002`

As páginas descobrem sozinhas o servidor (`conexao-backend.js`): o próprio endereço da página → `localhost:3001` → nuvem Render. Para forçar um servidor: `central.html?backend=http://192.168.0.10:3001` ou o campo "URL do Backend" no Card 6.

### Variáveis opcionais (`backend/.env`)
| Variável | Efeito |
| :--- | :--- |
| `PORT` | Porta HTTP/WebSocket (padrão `3001`) |
| `PONTE_SERIAL=COM5` | Lê também a telemetria pela USB (deixe desligado na hora de gravar o firmware) |
| `ESPELHAR_NUVEM=0` | Não espelha o estado no Render |
| `DATA_DIR` | Pasta alternativa para `historico_eventos.json` / `ultimo_estado.json` |

---

## 2. Como a ESP32 conversa com o servidor (zero configuração de IP)

1. A ESP32 envia o JSON de telemetria por **UDP broadcast** na porta `3002` (5 pacotes/s, 10/s durante alertas).
2. O servidor responde por UDP com `{"srv":"INOVATECH","cmds":[...]}`.
3. A ESP32 aprende o IP do PC pela resposta e passa a mandar direto para ele.
4. Os comandos da Central chegam nessa resposta: `TEST_ALARM`, `SILENCIAR`, `CALIBRAR`, `TOGGLE_RELE1`, `TOGGLE_RELE2`.
5. Se nenhum servidor local responder por 5 s, a ESP32 envia por **HTTPS para o Render** (a resposta do `POST /api/telemetry` também traz os comandos).

Com o PC ligado, o servidor local **espelha o estado no Render**. Assim quem abre o site pela internet (ex.: celular pela Vercel) vê tudo ao vivo, e os comandos feitos por lá (ex.: "Estou alerta") voltam para o servidor local executar.

---

## 3. Como Testar Sem a ESP32 Física (Simulador)

Com o servidor rodando, abra um segundo terminal na pasta `backend`:
```bash
npm run simulate
```
O simulador fala exatamente o protocolo do firmware (UDP + comandos): 6 s normal → 5 s de cabeça baixa (alarme aos 3,5 s) → cabeça erguida (recuperação automática em 3 s). Os botões "Disparar Alerta / Teste" e "Silenciar" da Central aparecem no terminal do simulador.

---

## 4. Deploy na Nuvem (Render)

1. **New +** → **Web Service** → repositório `Matheuseas23/inovatech_tcc`.
2. **Root Directory:** `backend` · **Environment:** `Node` · **Build:** `npm install` · **Start:** `npm start` · **Instance:** `Free`.
3. No plano grátis o Render "dorme": abra `https://inovatech-tcc.onrender.com/health` uns 2 minutos antes da banca.

---

## 5. Endpoints da API

| Método | Endpoint | Descrição |
| :--- | :--- | :--- |
| `GET` | `/api/health` | Integridade do servidor (`status`, `servidor`, `esp32_conectado`) |
| `GET` | `/api/telemetry` | Estado consolidado + 10 eventos recentes |
| `POST` | `/api/telemetry` | Telemetria da ESP32 (resposta: `{"srv":"INOVATECH","cmds":[...]}`) |
| `POST` | `/api/command` | `{"comando": "DISPARAR_TESTE" \| "SIMULAR_ALERTA" \| "SILENCIAR_ALARME" \| "NORMALIZAR" \| "CALIBRAR_MPU" \| "TOGGLE_RELE1" \| "TOGGLE_RELE2"}` (`SIMULAR_ALERTA` aceita `duracao_ms`) |
| `POST` | `/api/driver/shift` | `{"status_viagem": "EM_VIAGEM" \| "INATIVO"}` |
| `POST` | `/api/gps` | `{"gps": {"lat": -23.5, "lon": -46.6, "precisao": 5}}` |
| `GET` | `/api/events` | Histórico (máx. 100 eventos) |
| `POST` | `/api/events/clear` | Limpa o histórico |
| `WS` | `/ws` | Mensagens: `ESTADO_INICIAL`, `TELEMETRIA_UPDATE`, `ALERTA_FISICO_E_VIRTUAL_CELULAR`, `ESCALONAMENTO_EMERGENCIA`, `ALERTA_ENCERRADO`, `NOVO_EVENTO`, `STATUS_MOTORISTA`, `GPS_UPDATE` |

Pelo WebSocket, os clientes enviam `{"tipo":"COMANDO","comando":"..."}`, `{"tipo":"STATUS_MOTORISTA",...}` e `{"tipo":"GPS_UPDATE","gps":{...}}`.

### Exemplo de telemetria enviada pela ESP32
```json
{
  "dispositivo": "ESP32_SMART_DRIVER_01", "seq": 812, "uptime_ms": 163200,
  "sensor_ir": 0, "tempo_cabeca_baixa_ms": 2140, "limiar_ms": 3500,
  "buzzer": 1, "rele": 0, "rele_1": 0, "rele_2": 0, "alarme_teste": 0,
  "estado": "PRE_ALERTA",
  "acelerometro": { "pitch": -24.1, "roll": 1.3, "inclinacao": 24.4, "pescada_detectada": false,
                    "online": true, "calibrado": true, "status": "ONLINE (MPU-6050 ATIVO)" },
  "wifi": { "ssid": "REDMI Note 15 5G", "rssi": -51, "ip": "192.168.43.20" }
}
```

> Por segurança, o servidor não entrega pela web as pastas `backend/`, `Smart Driver/` (o firmware tem as senhas de Wi-Fi), `.git/` nem arquivos `.md` e `.bat`.
