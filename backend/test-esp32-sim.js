/**
 * ==============================================================================
 * SIMULADOR DE ESP32 — INOVA TECH SMART DRIVER
 * ==============================================================================
 * Imita o firmware real (Smart Driver/src/main.cpp) sem precisar da placa:
 * - Envia o mesmo JSON de telemetria por UDP na porta 3002 (5 pacotes/s).
 * - Recebe a resposta {"srv":"INOVATECH","cmds":[...]} e executa os comandos
 *   da Central (TEST_ALARM, SILENCIAR, CALIBRAR, TOGGLE_RELE1/2).
 * - Repete um ciclo: 6s dirigindo normal -> cabeça baixa por 5s (bip + Relé 2 em 1s, alarme aos 1,5s)
 *   -> cabeça erguida (recuperação automática de 3s no servidor).
 *
 * Uso: npm run simulate   (com o servidor já rodando em outra janela)
 * ==============================================================================
 */

const dgram = require('dgram');

const SERVIDOR_HOST = process.env.BACKEND_HOST || '127.0.0.1';
const SERVIDOR_PORTA_UDP = Number(process.env.BACKEND_UDP_PORT || 3002);
const LIMIAR_MS = 1500;
const CICLO_MS = 16000;

const socket = dgram.createSocket('udp4');
const inicioSimulador = Date.now();
let seq = 0;
let inicioCabecaBaixa = 0;
let testeAtivoAte = 0;
let rele1Manual = false;
let rele2Manual = false;

function cabecaBaixaNoCiclo(agora) {
  const t = (agora - inicioSimulador) % CICLO_MS;
  return t >= 6000 && t < 11000;
}

function montarTelemetria() {
  const agora = Date.now();
  const cabecaBaixa = cabecaBaixaNoCiclo(agora);
  if (cabecaBaixa && !inicioCabecaBaixa) inicioCabecaBaixa = agora;
  if (!cabecaBaixa) inicioCabecaBaixa = 0;

  const tempo = inicioCabecaBaixa ? agora - inicioCabecaBaixa : 0;
  const teste = agora < testeAtivoAte;
  const alarme = (cabecaBaixa && tempo >= LIMIAR_MS) || teste;
  const bip = cabecaBaixa && !alarme && tempo >= 1000 && tempo < 1300;
  const avisoPiscaAlerta = cabecaBaixa && tempo >= 1000;
  const estado = alarme ? 'ALARME_ATIVADO' : (cabecaBaixa ? 'PRE_ALERTA' : 'NORMAL');
  const inclinacao = cabecaBaixa ? 38 + Math.random() * 3 : 2 + Math.random() * 2;

  return {
    dispositivo: 'ESP32_SMART_DRIVER_01',
    seq: seq++,
    uptime_ms: agora - inicioSimulador,
    sensor_ir: cabecaBaixa ? 0 : 1,
    tempo_cabeca_baixa_ms: tempo,
    limiar_ms: LIMIAR_MS,
    buzzer: alarme || bip ? 1 : 0,
    rele: alarme || rele1Manual ? 1 : 0,
    rele_1: alarme || rele1Manual ? 1 : 0,
    rele_2: alarme || avisoPiscaAlerta || rele2Manual ? 1 : 0,
    alarme_teste: teste ? 1 : 0,
    estado,
    acelerometro: {
      pitch: Number((cabecaBaixa ? -inclinacao : inclinacao / 2).toFixed(1)),
      roll: Number((Math.random() * 2 - 1).toFixed(1)),
      inclinacao: Number(inclinacao.toFixed(1)),
      pescada_detectada: false,
      online: true,
      calibrado: true,
      status: 'ONLINE (SIMULADOR)'
    },
    wifi: { ssid: 'SIMULADOR', rssi: -50, ip: '127.0.0.1' }
  };
}

let ultimoEstado = '';
function enviarTelemetria() {
  const pacote = montarTelemetria();
  socket.send(Buffer.from(JSON.stringify(pacote)), SERVIDOR_PORTA_UDP, SERVIDOR_HOST);
  if (pacote.estado !== ultimoEstado) {
    ultimoEstado = pacote.estado;
    console.log(`[ESP32 SIM] ${pacote.estado.padEnd(14)} | IR: ${pacote.sensor_ir} | Tempo: ${pacote.tempo_cabeca_baixa_ms} ms | Buzzer: ${pacote.buzzer}`);
  }
  const emAlerta = pacote.estado !== 'NORMAL';
  setTimeout(enviarTelemetria, emAlerta ? 100 : 200);
}

let servidorAvisado = false;
socket.on('message', (msg) => {
  let resposta;
  try { resposta = JSON.parse(msg.toString('utf8')); } catch (e) { return; }
  if (resposta.srv !== 'INOVATECH') return;
  if (!servidorAvisado) {
    servidorAvisado = true;
    console.log('[ESP32 SIM] Servidor respondeu: comunicação UDP bidirecional OK.');
  }
  (resposta.cmds || []).forEach((cmd) => {
    console.log(`[ESP32 SIM] Comando recebido da Central: ${cmd}`);
    if (cmd === 'TEST_ALARM') testeAtivoAte = Date.now() + 30000;
    if (cmd === 'SILENCIAR') { testeAtivoAte = 0; rele1Manual = false; rele2Manual = false; }
    if (cmd === 'TOGGLE_RELE1') rele1Manual = !rele1Manual;
    if (cmd === 'TOGGLE_RELE2') rele2Manual = !rele2Manual;
  });
});

socket.on('error', (err) => console.error('[ESP32 SIM ERRO]:', err.message));

socket.bind(0, () => {
  console.log(`\nSimulador ESP32 InovaTech enviando UDP para ${SERVIDOR_HOST}:${SERVIDOR_PORTA_UDP}`);
  console.log('Ciclo: 6s normal -> 5s cabeça baixa (alarme aos 1,5s) -> cabeça erguida.\n');
  enviarTelemetria();
});
