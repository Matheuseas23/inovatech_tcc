/**
 * ==============================================================================
 * SIMULADOR DE ESP32 — INOVA TECH SMART DRIVER
 * ==============================================================================
 * Executa um ciclo automatizado simulando a placa ESP32 enviando telemetria
 * realística para o Backend (Sensor IR, Acelerômetro, Buzzer e Relé).
 * 
 * Uso: node test-esp32-sim.js
 * ==============================================================================
 */

const http = require('http');

const BACKEND_HOST = process.env.BACKEND_HOST || 'localhost';
const BACKEND_PORT = process.env.BACKEND_PORT || 3001;

console.log(`\nIniciando Simulador ESP32 InovaTech conectando em http://${BACKEND_HOST}:${BACKEND_PORT}/api/telemetry ...\n`);

let estadoSimulado = {
  dispositivo: 'ESP32_SMART_DRIVER_01',
  sensor_ir: 1, // 1 = HIGH (normal), 0 = LOW (cabeça baixa)
  tempo_cabeca_baixa_ms: 0,
  buzzer: 0,
  rele: 0,
  acelerometro: {
    pitch: 2.1,
    roll: -0.8,
    yaw: 0.0,
    pescada_detectada: false,
    status: 'Simulado em Tempo Real'
  }
};

let tempoInicioInclinacao = 0;
let modoSimulacao = 'NORMAL'; // 'NORMAL', 'SONO_INICIADO', 'ACORDOU'
let cicloSegundos = 0;

function enviarTelemetria() {
  cicloSegundos++;

  // Simula ciclos reais de comportamento:
  // 0s a 10s: Normal (dirigindo focado)
  // 11s a 16s: Motorista começa a ter sono e tomba a cabeça (ativa contagem e estoura alarme aos 2.5s)
  // 17s em diante: Acorda com o buzzer, ergue a cabeça e normaliza
  const cicloModulo = cicloSegundos % 22;

  if (cicloModulo < 8) {
    // Normal
    estadoSimulado.sensor_ir = 1;
    estadoSimulado.tempo_cabeca_baixa_ms = 0;
    estadoSimulado.buzzer = 0;
    estadoSimulado.rele = 0;
    estadoSimulado.acelerometro.pitch = Number((Math.sin(cicloSegundos) * 3 + 2).toFixed(1));
    estadoSimulado.acelerometro.roll = Number((Math.cos(cicloSegundos) * 2).toFixed(1));
    estadoSimulado.acelerometro.pescada_detectada = false;
    tempoInicioInclinacao = 0;
  } else if (cicloModulo >= 8 && cicloModulo <= 15) {
    // Cabeça baixa por sono
    estadoSimulado.sensor_ir = 0;
    if (tempoInicioInclinacao === 0) {
      tempoInicioInclinacao = Date.now();
    }
    const msDecorridos = Date.now() - tempoInicioInclinacao;
    estadoSimulado.tempo_cabeca_baixa_ms = msDecorridos;

    // Pitch cai para frente (-25 graus)
    estadoSimulado.acelerometro.pitch = -26.5;
    estadoSimulado.acelerometro.pescada_detectada = (msDecorridos >= 2500);

    if (msDecorridos >= 2500) {
      estadoSimulado.buzzer = 1;
      estadoSimulado.rele = 1;
    }
  } else {
    // Normalizado após alarme
    estadoSimulado.sensor_ir = 1;
    estadoSimulado.tempo_cabeca_baixa_ms = 0;
    estadoSimulado.buzzer = 0;
    estadoSimulado.rele = 0;
    estadoSimulado.acelerometro.pitch = 5.0; // Sobressalto de cabeça erguida
    estadoSimulado.acelerometro.pescada_detectada = false;
    tempoInicioInclinacao = 0;
  }

  const payload = JSON.stringify(estadoSimulado);

  const req = http.request({
    hostname: BACKEND_HOST,
    port: BACKEND_PORT,
    path: '/api/telemetry',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload)
    }
  }, (res) => {
    let data = '';
    res.on('data', chunk => data += chunk);
    res.on('end', () => {
      try {
        const resp = JSON.parse(data);
        console.log(`[ESP32 SIM] Status: ${resp.estado_processado} | IR: ${estadoSimulado.sensor_ir === 1 ? 'ERTO(1)' : 'BAIXO(0)'} | Tempo: ${estadoSimulado.tempo_cabeca_baixa_ms}ms | Buzzer: ${estadoSimulado.buzzer}`);
      } catch (e) {
        console.log(`[ESP32 SIM] Pacote enviado com status HTTP ${res.statusCode}`);
      }
    });
  });

  req.on('error', (err) => {
    console.error(`[ESP32 SIM ERRO]: Não foi possível conectar ao backend (${err.message}). Certifique-se de iniciar 'npm start' na pasta backend.`);
  });

  req.write(payload);
  req.end();
}

// Envia pacote a cada 400ms (taxa típica de telemetria IoT do ESP32)
setInterval(enviarTelemetria, 400);
