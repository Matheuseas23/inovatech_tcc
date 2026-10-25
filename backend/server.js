/**
 * ==============================================================================
 * INOVATECH SMART DRIVER — BACKEND API & WEBSOCKET ENGINE (TCC)
 * ==============================================================================
 * Servidor central de telemetria em tempo real entre a ESP32 (óculos), a
 * Central Tech (central.html) e o App do Motorista (motorista.html).
 *
 * Regras de ouro desta versão:
 * - A ESP32 é a fonte da verdade do estado postural (NORMAL / PRÉ-ALERTA / ALARME).
 *   O servidor NÃO cronometra a cabeça baixa por conta própria.
 * - O servidor é dono do "alerta travado" (escalonamento 9s crítico -> 15s SOS) e
 *   da recuperação automática (cabeça erguida por 3s encerra o alerta).
 * - Comandos da Central chegam à ESP32 pela resposta UDP (rede local) ou pela
 *   resposta do POST /api/telemetry (quando a ESP32 fala direto com a nuvem).
 * - Rodando no PC, o servidor espelha o estado na nuvem (Render) e recebe de volta
 *   os comandos de quem abriu o site pela internet (celular via Vercel).
 *
 * Portas: 3001 HTTP + WebSocket (/ws) | 3002 UDP (telemetria da ESP32)
 * ==============================================================================
 */

const express = require('express');
const http = require('http');
const https = require('https');
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
const dgram = require('dgram');
const { execFile, spawn } = require('child_process');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3001;
const UDP_PORT = 3002;
const IS_RENDER_CLOUD = Boolean(process.env.RENDER || process.env.RENDER_EXTERNAL_URL);
const RENDER_CLOUD_URL = process.env.RENDER_CLOUD_URL || 'https://inovatech-tcc.onrender.com/api/telemetry';
const ESPELHAR_NUVEM = !IS_RENDER_CLOUD && process.env.ESPELHAR_NUVEM !== '0';

// ------------------------------------------------------------------------------
// CALIBRAÇÃO OFICIAL (mesmos valores exibidos na Central e no App do Motorista)
// ------------------------------------------------------------------------------
const LIMIAR_PADRAO_MS = 1500;           // Tolerância da ESP32 (sobrescrita pelo limiar_ms enviado pela placa)
const LIMIAR_SIMULACAO_MS = 1500;        // Botão "Simular Alerta" (modo rápido de demonstração)
const RECUPERACAO_MS = 3000;             // Cabeça erguida por 3s encerra o alerta automaticamente
const ESCALONAMENTO = { critico_ms: 9000, acidente_ms: 15000 };
const COOLDOWN_NORMALIZACAO_MS = 2500;   // Evita re-disparo imediato logo após normalizar
const TIMEOUT_ESP32_MS = 6000;
const TIMEOUT_ESPELHO_MS = 5000;

// Configuração para suporte a proxies reversos do Render / Nginx
app.set('trust proxy', 1);

app.use(cors({
  origin: '*', // Permite Vercel, localhost e qualquer origem autorizada
  methods: ['GET', 'POST', 'OPTIONS'],
  allowedHeaders: ['Content-Type', 'Authorization']
}));
app.use(express.json({ limit: '64kb' }));

// Servir frontend estático para apresentação local e contingência.
// Bloqueia pastas internas (firmware com senhas de Wi-Fi, dados, git, ADB).
const frontendRoot = path.join(__dirname, '..');
const PASTAS_PRIVADAS = /^\/(backend|smart(%20| )driver|\.git|\.agents|\.vscode|espelhamento-celular|audios_professor)(\/|$)/i;
const ARQUIVOS_PRIVADOS = /\.(md|bat|ini|json\.bak)$/i;
app.use((req, res, next) => {
  if (PASTAS_PRIVADAS.test(req.path) || ARQUIVOS_PRIVADOS.test(req.path)) return res.status(404).end();
  next();
});
if (fs.existsSync(path.join(frontendRoot, 'index.html'))) {
  app.use(express.static(frontendRoot, { dotfiles: 'ignore', index: false }));
}

const server = http.createServer(app);
const wss = new WebSocket.Server({ server, path: '/ws' });

// ------------------------------------------------------------------------------
// ESTADO GLOBAL DO SISTEMA EM MEMÓRIA
// ------------------------------------------------------------------------------
const estado = {
  dispositivo: 'ESP32_SMART_DRIVER_01',
  online: false,
  ultimoVisto: null,
  fonte_conexao: null, // 'UDP' | 'HTTP' | 'SERIAL' | 'WS' | 'NUVEM' (repassada pelo Render ao PC)
  servidor: IS_RENDER_CLOUD ? 'NUVEM' : 'LOCAL',
  sensor_ir: 1, // 1 = HIGH (Cabeça erguida), 0 = LOW (Cabeça baixa)
  ir_confiavel: true, // false = IR sem sinal (ex.: sem alimentação); a ESP32 o ignora
  tempo_cabeca_baixa_ms: 0,
  limiar_seguranca_ms: LIMIAR_PADRAO_MS,
  estado: 'NORMAL', // 'NORMAL', 'PRE_ALERTA', 'ALARME_ATIVADO'
  buzzer: 0,
  rele: 0,   // Relé 1 Safe-Stop (GPIO 18)
  rele_2: 0, // Relé 2 Pisca-Alerta (GPIO 19)
  alarme_teste: 0,
  acelerometro: {
    pitch: 0.0,
    roll: 0.0,
    inclinacao: 0.0,
    pescada_detectada: false,
    online: false,
    calibrado: false,
    status: 'AGUARDANDO MODULO'
  },
  wifi: { ssid: '', rssi: 0, ip: '' },
  alerta_travado: false,
  chamada_celular_ativa: false,
  origem_alerta: null, // 'SENSOR' | 'TESTE_REMOTO' | 'SIMULACAO'
  tempo_sem_resposta_ms: 0,
  nivel_escalonamento: 0, // 1 = Celular | 2 = Crítico na Central | 3 = Possível Acidente / SOS
  tempo_recuperacao_ms: 0,
  recuperacao_limite_ms: RECUPERACAO_MS,
  escalonamento: { ...ESCALONAMENTO },
  simulacao_ativa: false,
  modo_espelho: false,
  modo_exibicao: false, // Display dos óculos mostra olhos piscando (ligado pelo app do motorista)
  contadores: {
    totalAlarmesSessao: 0,
    inicioSessao: new Date().toISOString()
  },
  motoristas: {
    matheus_garcia: {
      nome: 'Matheus Garcia',
      status: 'INATIVO', // Os sensores da ESP32 só valem depois que o app ativar o monitoramento
      veiculo: 'Scania R-450 (Placa INO-2026)',
      rota: 'Rodovia BR-116 KM 284 · Régis Bittencourt',
      atualizadoEm: new Date().toISOString()
    }
  },
  gps: {
    lat: -23.682138,
    lon: -46.875412,
    precisao: 1.8,
    velocidade: 0,
    heading: 0,
    origem: 'PADRAO_BR116',
    atualizadoEm: null
  }
};

// Histórico de eventos (máximo 100 registros)
const MAX_HISTORICO = 100;
let historicoEventos = [];

// Fila de comandos para a placa ESP32 (entregue na resposta UDP/HTTP)
let comandosESP32 = [];
// Nuvem em modo espelho: comandos recebidos pela internet que o servidor local deve executar
let comandosParaLocal = [];
// Servidor local: eventos ainda não enviados para a nuvem
let eventosParaNuvem = [];

// Controle interno do alerta
let inicioSemRespostaMs = 0;
let inicioRecuperacaoMs = 0;
let cooldownNormalizacaoAte = 0;
let ultimoEspelhoRecebidoMs = 0;
let ignorarAlarmeEspelhoAte = 0;
let limiarEsp32Ms = LIMIAR_PADRAO_MS; // Último limiar informado pela placa
const simulacao = { ativa: false, inicioMs: 0, duracaoMs: LIMIAR_SIMULACAO_MS, timer: null };

// ------------------------------------------------------------------------------
// PERSISTÊNCIA DE DADOS EM DISCO (com debounce para não brigar com o OneDrive)
// ------------------------------------------------------------------------------
const DATA_DIR = process.env.DATA_DIR || path.join(__dirname, 'data');
if (!fs.existsSync(DATA_DIR)) {
  try { fs.mkdirSync(DATA_DIR, { recursive: true }); } catch (e) { }
}
const EVENTOS_FILE = path.join(DATA_DIR, 'historico_eventos.json');
const ESTADO_FILE = path.join(DATA_DIR, 'ultimo_estado.json');

try {
  if (fs.existsSync(EVENTOS_FILE)) {
    const parsed = JSON.parse(fs.readFileSync(EVENTOS_FILE, 'utf8'));
    if (Array.isArray(parsed)) {
      historicoEventos = parsed.slice(0, MAX_HISTORICO);
      console.log(`[STORAGE] Carregados ${historicoEventos.length} eventos persistidos do disco.`);
    }
  }
} catch (e) {
  console.warn('[STORAGE] Falha ao ler historico_eventos.json:', e.message);
}

// Restaura só o que faz sentido sobreviver a um reinício (contadores e turno).
// O estado postural NUNCA é restaurado: um alarme antigo não pode "ressuscitar".
try {
  if (fs.existsSync(ESTADO_FILE)) {
    const salvo = JSON.parse(fs.readFileSync(ESTADO_FILE, 'utf8'));
    if (salvo && salvo.contadores && Number.isFinite(salvo.contadores.totalAlarmesSessao)) {
      estado.contadores.totalAlarmesSessao = salvo.contadores.totalAlarmesSessao;
    }
    const turnoSalvo = salvo && salvo.motoristas && salvo.motoristas.matheus_garcia;
    if (turnoSalvo && (turnoSalvo.status === 'EM_VIAGEM' || turnoSalvo.status === 'INATIVO')) {
      estado.motoristas.matheus_garcia.status = turnoSalvo.status;
    }
    console.log(`[STORAGE] Total acumulado de alarmes: ${estado.contadores.totalAlarmesSessao}`);
  }
} catch (e) {
  console.warn('[STORAGE] Falha ao ler ultimo_estado.json:', e.message);
}

let salvarTimer = null;
function persistirEmDisco() {
  clearTimeout(salvarTimer);
  salvarTimer = setTimeout(() => {
    fs.writeFile(EVENTOS_FILE, JSON.stringify(historicoEventos, null, 2), (err) => {
      if (err) console.error('[STORAGE ERROR] Ao salvar eventos:', err.message);
    });
    const resumo = {
      salvoEm: new Date().toISOString(),
      contadores: estado.contadores,
      motoristas: estado.motoristas,
      gps: estado.gps
    };
    fs.writeFile(ESTADO_FILE, JSON.stringify(resumo, null, 2), (err) => {
      if (err) console.error('[STORAGE ERROR] Ao salvar estado:', err.message);
    });
  }, 1500);
}

// ------------------------------------------------------------------------------
// FUNÇÕES AUXILIARES
// ------------------------------------------------------------------------------
function inserirEvento(evento) {
  historicoEventos.unshift(evento);
  if (historicoEventos.length > MAX_HISTORICO) historicoEventos.length = MAX_HISTORICO;
  persistirEmDisco();
  broadcast({ tipo: 'NOVO_EVENTO', evento });
}

function registrarEvento(tipo, mensagem, detalhes = '') {
  const novoEvento = {
    id: 'evt_' + Date.now() + '_' + Math.random().toString(36).slice(2, 6),
    timestamp: new Date().toISOString(),
    horaFormatada: new Date().toLocaleTimeString('pt-BR', { timeZone: 'America/Sao_Paulo' }),
    tipo,
    mensagem,
    detalhes
  };
  inserirEvento(novoEvento);
  if (ESPELHAR_NUVEM) {
    eventosParaNuvem.push(novoEvento);
    if (eventosParaNuvem.length > 30) eventosParaNuvem.shift();
  }
  console.log(`[EVENTO] ${tipo}: ${mensagem}`);
  return novoEvento;
}

function broadcast(dados) {
  const mensagemStr = JSON.stringify(dados);
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      try { client.send(mensagemStr); } catch (err) { console.error('[WS BROADCAST ERROR]:', err.message); }
    }
  });
}

function transmitirEstado(tipo = 'TELEMETRIA_UPDATE') {
  broadcast({ tipo, estado });
  espelharParaNuvem(tipo !== 'TELEMETRIA_UPDATE');
}

function modoEspelhoAtivo() {
  return IS_RENDER_CLOUD && (Date.now() - ultimoEspelhoRecebidoMs < TIMEOUT_ESPELHO_MS);
}

// Comandos da Central traduzidos para o vocabulário do firmware
const MAPA_COMANDOS_ESP32 = {
  DISPARAR_TESTE: 'TEST_ALARM',
  SILENCIAR_ALARME: 'SILENCIAR',
  CALIBRAR_MPU: 'CALIBRAR',
  TOGGLE_RELE1: 'TOGGLE_RELE1',
  TOGGLE_RELE2: 'TOGGLE_RELE2'
};

function enfileirarComandoESP32(cmdFirmware) {
  if (!comandosESP32.includes(cmdFirmware)) comandosESP32.push(cmdFirmware);
  if (comandosESP32.length > 10) comandosESP32.shift();
}

function retirarComandosESP32() {
  const cmds = comandosESP32;
  comandosESP32 = [];
  return cmds;
}

// Resposta para a ESP32: comandos pendentes + "mon" (1 = monitoramento ativado no app, liga os sensores)
// + "exib" (1 = modo exibição: o display mostra os olhos piscando)
function respostaESP32() {
  return {
    cmds: retirarComandosESP32(),
    mon: estado.motoristas.matheus_garcia.status === 'EM_VIAGEM' ? 1 : 0,
    exib: estado.modo_exibicao ? 1 : 0
  };
}

// ------------------------------------------------------------------------------
// ADB: ACORDA O CELULAR DO MOTORISTA PELO CABO USB (opcional, só no PC)
// ------------------------------------------------------------------------------
const ADB_PATH = path.join(frontendRoot, 'espelhamento-celular', 'adb.exe');
let chamadaCelularAbertaViaAdb = false;

// Abre obrigatoriamente a tela inteira de Chamada Café (mesmo com a tela do celular apagada/bloqueada!)
function dispararChamadaCelularADB() {
  if (IS_RENDER_CLOUD || !fs.existsSync(ADB_PATH)) return;
  chamadaCelularAbertaViaAdb = true;
  const urlChamada = 'https://inovatech-tcc.vercel.app/?chamada=1';
  // 1. Acende a tela física caso o celular esteja desligado/apagado (KEYCODE_WAKEUP = 224 + POWER)
  // 2. Aumenta brilho e volume de mídia para garantir que toque alto igual WhatsApp
  // 3. Desbloqueia a tela de bloqueio (wm dismiss-keyguard + KEYCODE_MENU = 82 + swipe)
  // 4. Abre a Chamada de Café em Tela Inteira por cima de qualquer app
  const cmdShell = `input keyevent 224 && settings put system screen_brightness 255 && media volume --show --stream 3 --set 15 ; wm dismiss-keyguard && input keyevent 82 && input swipe 500 1750 500 250 180 && am start -a android.intent.action.VIEW -d "${urlChamada}"`;
  execFile(ADB_PATH, ['shell', cmdShell], { timeout: 7000 }, () => { });
}

// Quando o motorista confirma o alerta, retorna automaticamente para o app da câmera Linklemo
function voltarParaLinklemoCelularADB() {
  if (IS_RENDER_CLOUD || !fs.existsSync(ADB_PATH) || !chamadaCelularAbertaViaAdb) return;
  chamadaCelularAbertaViaAdb = false;
  execFile(ADB_PATH, ['shell', 'input', 'keyevent', '4'], { timeout: 3000 }, () => { });
}

// ------------------------------------------------------------------------------
// MOTOR DO ALERTA TRAVADO (FASE 1: CELULAR -> FASE 2: CRÍTICO -> FASE 3: SOS)
// ------------------------------------------------------------------------------
function dispararAlerta(origem, mensagem, detalhes) {
  if (estado.alerta_travado) return;
  estado.estado = 'ALARME_ATIVADO';
  estado.alerta_travado = true;
  estado.chamada_celular_ativa = true;
  estado.origem_alerta = origem;
  estado.nivel_escalonamento = 1;
  estado.tempo_sem_resposta_ms = 0;
  estado.tempo_recuperacao_ms = 0;
  inicioSemRespostaMs = Date.now();
  inicioRecuperacaoMs = 0;
  estado.contadores.totalAlarmesSessao++;

  dispararChamadaCelularADB();
  registrarEvento('ALARME_ATIVADO', mensagem, detalhes || 'Alerta físico (Buzzer/Relés) e chamada no celular acionados. Aguardando o motorista.');
  broadcast({ tipo: 'ALERTA_FISICO_E_VIRTUAL_CELULAR', dispararChamadaCafe: true, estado });
  espelharParaNuvem(true);
  persistirEmDisco();
}

function encerrarAlerta(mensagem, detalhes = '') {
  const haviaAlerta = estado.alerta_travado || estado.estado !== 'NORMAL' || simulacao.ativa;
  pararSimulacao();

  estado.alerta_travado = false;
  estado.chamada_celular_ativa = false;
  estado.origem_alerta = null;
  estado.nivel_escalonamento = 0;
  estado.tempo_sem_resposta_ms = 0;
  estado.tempo_recuperacao_ms = 0;
  estado.estado = 'NORMAL';
  estado.tempo_cabeca_baixa_ms = 0;
  estado.limiar_seguranca_ms = limiarEsp32Ms;
  inicioSemRespostaMs = 0;
  inicioRecuperacaoMs = 0;
  cooldownNormalizacaoAte = Date.now() + COOLDOWN_NORMALIZACAO_MS;

  // Desliga também um teste remoto que esteja tocando no óculos
  enfileirarComandoESP32('SILENCIAR');
  voltarParaLinklemoCelularADB();
  if (haviaAlerta) registrarEvento('NORMAL', mensagem, detalhes);
  transmitirEstado('ALERTA_ENCERRADO');
  persistirEmDisco();
}

// Loop de 250ms: cronômetro sem resposta, níveis de escalonamento e recuperação automática
setInterval(() => {
  if (!estado.alerta_travado || !inicioSemRespostaMs || modoEspelhoAtivo()) return;
  const agora = Date.now();

  // Recuperação automática: cabeça erguida (ESP32 em NORMAL) por 3s seguidos
  if (inicioRecuperacaoMs) {
    estado.tempo_recuperacao_ms = agora - inicioRecuperacaoMs;
    if (estado.tempo_recuperacao_ms >= RECUPERACAO_MS) {
      encerrarAlerta('Recuperação automática: motorista manteve a cabeça erguida por 3s',
        'Alarmes físicos, chamada no celular e escalonamento encerrados.');
      return;
    }
  }

  const decorridoMs = agora - inicioSemRespostaMs;
  estado.tempo_sem_resposta_ms = decorridoMs;
  estado.estado = 'ALARME_ATIVADO';
  let mudouNivel = false;

  if (decorridoMs >= ESCALONAMENTO.critico_ms && estado.nivel_escalonamento < 2) {
    estado.nivel_escalonamento = 2;
    mudouNivel = true;
    registrarEvento('ALARME_ATIVADO',
      `ALERTA CRÍTICO NA CENTRAL (${ESCALONAMENTO.critico_ms / 1000}s SEM RESPOSTA): Motorista não confirmou o alerta no celular!`,
      `Protocolo de Possível Acidente em ${(ESCALONAMENTO.acidente_ms - ESCALONAMENTO.critico_ms) / 1000}s.`);
    dispararChamadaCelularADB();
  }

  if (decorridoMs >= ESCALONAMENTO.acidente_ms && estado.nivel_escalonamento < 3) {
    estado.nivel_escalonamento = 3;
    mudouNivel = true;
    registrarEvento('ALARME_ATIVADO',
      `POSSÍVEL ACIDENTE (${ESCALONAMENTO.acidente_ms / 1000}s SEM RESPOSTA)! GPS enviado ao Resgate SAMU 192 e Safe-Stop acionado!`,
      'Desaceleração veicular via Relé 1 (GPIO 18), Pisca-Alerta via Relé 2 (GPIO 19) e coordenadas GPS para Resgate/SAMU 192.');
  }

  transmitirEstado(mudouNivel ? 'ESCALONAMENTO_EMERGENCIA' : 'TELEMETRIA_UPDATE');
}, 250);

// ------------------------------------------------------------------------------
// SIMULAÇÃO CENTRALIZADA (Central e celular veem exatamente a mesma contagem)
// ------------------------------------------------------------------------------
function iniciarSimulacao(duracaoMs, origem) {
  if (estado.alerta_travado) return false;
  pararSimulacao();
  simulacao.ativa = true;
  simulacao.inicioMs = Date.now();
  simulacao.duracaoMs = Math.min(10000, Math.max(500, Number(duracaoMs) || LIMIAR_SIMULACAO_MS));
  estado.simulacao_ativa = true;
  estado.limiar_seguranca_ms = simulacao.duracaoMs;
  estado.sensor_ir = 0;
  estado.estado = 'PRE_ALERTA';
  estado.tempo_cabeca_baixa_ms = 0;
  registrarEvento('PRE_ALERTA', `Simulação de fadiga iniciada (${(simulacao.duracaoMs / 1000).toFixed(1)}s)`, `Origem: ${origem}`);

  simulacao.timer = setInterval(() => {
    const decorrido = Date.now() - simulacao.inicioMs;
    estado.tempo_cabeca_baixa_ms = Math.min(decorrido, simulacao.duracaoMs);
    if (decorrido >= simulacao.duracaoMs) {
      clearInterval(simulacao.timer);
      simulacao.timer = null;
      simulacao.ativa = false;
      estado.simulacao_ativa = false;
      dispararAlerta('SIMULACAO', `PERIGO: CABEÇA BAIXA SIMULADA (${(simulacao.duracaoMs / 1000).toFixed(1)}s)!`,
        'Simulação: chamada de descanso no celular e escalonamento da Central iniciados.');
    } else {
      transmitirEstado();
    }
  }, 100);
  return true;
}

function pararSimulacao() {
  if (simulacao.timer) clearInterval(simulacao.timer);
  simulacao.timer = null;
  simulacao.ativa = false;
  estado.simulacao_ativa = false;
}

// ------------------------------------------------------------------------------
// PROCESSAMENTO DE MENSAGENS (ESP32, App do Motorista, Central e Espelho)
// ------------------------------------------------------------------------------
function ehPacoteESP32(p) {
  return (typeof p.dispositivo === 'string' && p.dispositivo.toUpperCase().startsWith('ESP32')) ||
    (p.sensor_ir !== undefined && p.dispositivo === undefined);
}

function processarTelemetriaESP32(p, fonte) {
  const agora = Date.now();
  const estavaOnline = estado.online;
  estado.online = true;
  estado.ultimoVisto = new Date().toISOString();
  estado.fonte_conexao = fonte;
  if (p.dispositivo) estado.dispositivo = p.dispositivo;
  if (!estavaOnline) {
    registrarEvento('CONEXAO', `ESP32 conectada ao servidor (${fonte})`,
      p.wifi && p.wifi.ssid ? `Rede: ${p.wifi.ssid} · IP ${p.wifi.ip} · ${p.wifi.rssi} dBm` : 'Telemetria em tempo real ativa.');
  }

  if (p.acelerometro && typeof p.acelerometro === 'object') {
    const a = p.acelerometro;
    estado.acelerometro = {
      pitch: Number(a.pitch) || 0,
      roll: Number(a.roll) || 0,
      inclinacao: Number(a.inclinacao) || 0,
      pescada_detectada: Boolean(a.pescada_detectada),
      online: a.online !== undefined ? Boolean(a.online) : true,
      calibrado: Boolean(a.calibrado),
      status: String(a.status || 'ATIVO').slice(0, 40)
    };
  }
  if (p.wifi && typeof p.wifi === 'object') {
    estado.wifi = { ssid: String(p.wifi.ssid || ''), rssi: Number(p.wifi.rssi) || 0, ip: String(p.wifi.ip || '') };
  }

  // Atuadores sempre mostram o estado FÍSICO real dos pinos da placa
  if (p.buzzer !== undefined) estado.buzzer = Number(p.buzzer) ? 1 : 0;
  if (p.rele_1 !== undefined || p.rele !== undefined) estado.rele = Number(p.rele_1 ?? p.rele) ? 1 : 0;
  if (p.rele_2 !== undefined) estado.rele_2 = Number(p.rele_2) ? 1 : 0;
  estado.alarme_teste = Number(p.alarme_teste) ? 1 : 0;
  estado.ir_confiavel = p.ir_confiavel !== false; // Firmwares antigos não mandam o campo

  // Durante uma simulação, a postura exibida é a simulada
  const controladoPorSimulacao = simulacao.ativa || (estado.alerta_travado && estado.origem_alerta === 'SIMULACAO');
  if (controladoPorSimulacao) {
    transmitirEstado();
    return;
  }

  if (p.sensor_ir !== undefined) estado.sensor_ir = Number(p.sensor_ir) ? 1 : 0;
  estado.tempo_cabeca_baixa_ms = Math.max(0, Number(p.tempo_cabeca_baixa_ms) || 0);
  if (Number(p.limiar_ms) > 0) limiarEsp32Ms = Number(p.limiar_ms);
  estado.limiar_seguranca_ms = limiarEsp32Ms;

  // Estado decidido pela ESP32 (ou derivado, para simuladores antigos)
  let estadoEsp = p.estado;
  if (!['NORMAL', 'PRE_ALERTA', 'ALARME_ATIVADO'].includes(estadoEsp)) {
    if (estado.sensor_ir === 0) {
      estadoEsp = estado.tempo_cabeca_baixa_ms >= estado.limiar_seguranca_ms ? 'ALARME_ATIVADO' : 'PRE_ALERTA';
    } else {
      estadoEsp = 'NORMAL';
    }
  }

  if (estado.alerta_travado) {
    estado.estado = 'ALARME_ATIVADO';
    // Recuperação automática só vale para alertas reais do sensor (teste/simulação exigem confirmação)
    if (estado.origem_alerta === 'SENSOR') {
      if (estadoEsp === 'NORMAL') {
        if (!inicioRecuperacaoMs) inicioRecuperacaoMs = agora;
      } else {
        inicioRecuperacaoMs = 0;
        estado.tempo_recuperacao_ms = 0;
      }
    }
  } else if (agora < cooldownNormalizacaoAte && estadoEsp !== 'NORMAL') {
    estado.estado = 'NORMAL';
    estado.tempo_cabeca_baixa_ms = 0;
  } else {
    const anterior = estado.estado;
    if (estadoEsp === 'ALARME_ATIVADO') {
      const pescada = estado.acelerometro.pescada_detectada;
      const teste = estado.alarme_teste === 1;
      dispararAlerta(teste ? 'TESTE_REMOTO' : 'SENSOR',
        teste ? 'Teste remoto: alarme físico tocando nos óculos'
          : pescada ? 'PERIGO: PESCADA DE SONO DETECTADA PELO ACELERÔMETRO!'
            : `PERIGO: CABEÇA BAIXA POR ${(estado.limiar_seguranca_ms / 1000).toFixed(1)}s!`);
      return;
    }
    estado.estado = estadoEsp;
    if (anterior !== 'PRE_ALERTA' && estadoEsp === 'PRE_ALERTA') {
      registrarEvento('PRE_ALERTA', 'Aviso: Cabeça baixa detectada (contagem de segurança iniciada)',
        `Tolerância: ${(estado.limiar_seguranca_ms / 1000).toFixed(1)}s com 1 bip de aviso e pisca-alerta armado.`);
    } else if (anterior === 'PRE_ALERTA' && estadoEsp === 'NORMAL') {
      registrarEvento('NORMAL', 'Cabeça erguida antes do disparo do alarme', 'Pré-alerta cancelado pelo próprio motorista.');
    }
  }

  transmitirEstado();
}

function atualizarTurno(statusViagem, origem) {
  const novoStatus = statusViagem === 'INATIVO' ? 'INATIVO' : 'EM_VIAGEM';
  const motorista = estado.motoristas.matheus_garcia;
  motorista.atualizadoEm = new Date().toISOString();
  if (motorista.status === novoStatus) {
    broadcast({ tipo: 'STATUS_MOTORISTA', motorista: motorista.nome, status_viagem: novoStatus, estado });
    return;
  }
  motorista.status = novoStatus;
  if (modoEspelhoAtivo()) {
    // O servidor local registra o evento e devolve pelo espelho (evita log duplicado)
    comandosParaLocal.push({ tipo: 'STATUS_MOTORISTA', status_viagem: novoStatus, origem });
    broadcast({ tipo: 'STATUS_MOTORISTA', motorista: motorista.nome, status_viagem: novoStatus, estado });
    return;
  }
  registrarEvento('TURNO_MOTORISTA', `Condutor ${motorista.nome}: Viagem ${novoStatus === 'EM_VIAGEM' ? 'Iniciada' : 'Pausada'}`, `Origem: ${origem}`);
  broadcast({ tipo: 'STATUS_MOTORISTA', motorista: motorista.nome, status_viagem: novoStatus, estado });
  espelharParaNuvem(true);
  persistirEmDisco();
}

let ultimoBroadcastGpsMs = 0;
function atualizarGps(gps, origem) {
  const lat = Number(gps && gps.lat);
  const lon = Number(gps && gps.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon) || Math.abs(lat) > 90 || Math.abs(lon) > 180) return false;
  estado.gps = {
    lat,
    lon,
    precisao: Number(gps.precisao) || 5,
    velocidade: Number(gps.velocidade) || 0,
    heading: Number(gps.heading) || 0,
    origem: origem || 'CELULAR_MOTORISTA',
    atualizadoEm: new Date().toISOString()
  };
  if (modoEspelhoAtivo()) {
    comandosParaLocal = comandosParaLocal.filter((c) => c.tipo !== 'GPS_UPDATE');
    comandosParaLocal.push({ tipo: 'GPS_UPDATE', gps: estado.gps });
  }
  if (Date.now() - ultimoBroadcastGpsMs > 1000) {
    ultimoBroadcastGpsMs = Date.now();
    broadcast({ tipo: 'GPS_UPDATE', gps: estado.gps });
  }
  return true;
}

// Ponto único de entrada para comandos (REST, WebSocket ou repassados pela nuvem)
function executarComando(comando, origem = 'CENTRAL', extras = {}) {
  const cmd = String(comando || '').toUpperCase();
  const normalizar = ['SILENCIAR_ALARME', 'RESET', 'NORMALIZAR'].includes(cmd);
  const conhecidos = ['DISPARAR_TESTE', 'SIMULAR_ALERTA', 'CALIBRAR_MPU', 'TOGGLE_RELE1', 'TOGGLE_RELE2', 'MODO_EXIBICAO'];
  if (!normalizar && !conhecidos.includes(cmd)) return { ok: false, mensagem: `Comando desconhecido: ${comando}` };

  // Na nuvem em modo espelho, quem executa é o servidor local (a nuvem só repassa)
  if (modoEspelhoAtivo()) {
    comandosParaLocal.push({ tipo: 'COMANDO', comando: cmd, origem, extras });
    if (normalizar) {
      // Resposta otimista para quem está conectado na nuvem não ver o alerta "voltar"
      ignorarAlarmeEspelhoAte = Date.now() + 3000;
      estado.alerta_travado = false;
      estado.chamada_celular_ativa = false;
      estado.nivel_escalonamento = 0;
      estado.tempo_sem_resposta_ms = 0;
      estado.estado = 'NORMAL';
      transmitirEstado('ALERTA_ENCERRADO');
    }
    return { ok: true, mensagem: `Comando ${cmd} repassado ao servidor local.` };
  }

  if (normalizar) {
    encerrarAlerta(`Alerta desativado (${origem})`, 'Confirmação recebida: alarmes físicos e virtuais encerrados.');
    return { ok: true, mensagem: 'Alerta encerrado.' };
  }

  if (MAPA_COMANDOS_ESP32[cmd]) enfileirarComandoESP32(MAPA_COMANDOS_ESP32[cmd]);

  if (cmd === 'DISPARAR_TESTE') {
    pararSimulacao();
    dispararAlerta('TESTE_REMOTO', 'Comando remoto: Alerta físico e virtual disparado pela Central',
      'Buzzer e Relés 1/2 acionados na ESP32 até a confirmação do motorista.');
  } else if (cmd === 'SIMULAR_ALERTA') {
    if (!iniciarSimulacao(extras.duracao_ms, origem)) return { ok: false, mensagem: 'Já existe um alerta ativo. Normalize antes de simular.' };
  } else if (cmd === 'CALIBRAR_MPU') {
    registrarEvento('COMANDO_CENTRAL', 'Recalibração do acelerômetro solicitada', 'O motorista deve manter a cabeça erguida por 1 segundo.');
  } else if (cmd === 'MODO_EXIBICAO') {
    // Liga/desliga os olhos piscando no display (o app manda o valor desejado em "ativo")
    estado.modo_exibicao = extras.ativo !== undefined ? Boolean(extras.ativo) : !estado.modo_exibicao;
    registrarEvento('COMANDO_CENTRAL', `Modo exibição ${estado.modo_exibicao ? 'ligado' : 'desligado'} no display dos óculos`, `Origem: ${origem}`);
    transmitirEstado();
  } else {
    registrarEvento('COMANDO_CENTRAL', `Comando manual enviado à ESP32: ${cmd}`, `Origem: ${origem}`);
  }
  broadcast({ tipo: 'COMANDO_EXECUTADO', comando: cmd, estado });
  return { ok: true, mensagem: `Comando ${cmd} executado.` };
}

// Nuvem: aplica o estado espelhado pelo servidor local do PC
const CAMPOS_ESPELHO = ['dispositivo', 'online', 'ultimoVisto', 'fonte_conexao', 'sensor_ir', 'ir_confiavel', 'tempo_cabeca_baixa_ms',
  'limiar_seguranca_ms', 'estado', 'buzzer', 'rele', 'rele_2', 'alarme_teste', 'acelerometro', 'wifi', 'alerta_travado',
  'chamada_celular_ativa', 'origem_alerta', 'tempo_sem_resposta_ms', 'nivel_escalonamento', 'tempo_recuperacao_ms',
  'recuperacao_limite_ms', 'escalonamento', 'simulacao_ativa', 'contadores', 'motoristas', 'gps', 'modo_exibicao'];
const CAMPOS_ALERTA = ['estado', 'alerta_travado', 'chamada_celular_ativa', 'nivel_escalonamento', 'tempo_sem_resposta_ms'];

function aplicarEspelho(p) {
  ultimoEspelhoRecebidoMs = Date.now();
  const snap = p.estado_completo || {};
  const travadoAntes = estado.alerta_travado;
  const nivelAntes = estado.nivel_escalonamento;
  const ignorarAlerta = Date.now() < ignorarAlarmeEspelhoAte && snap.estado !== 'NORMAL';
  CAMPOS_ESPELHO.forEach((campo) => {
    if (snap[campo] === undefined) return;
    if (ignorarAlerta && CAMPOS_ALERTA.includes(campo)) return;
    estado[campo] = snap[campo];
  });
  estado.modo_espelho = true;
  // Comandos da Central do PC para a ESP32 que só alcança a nuvem
  if (Array.isArray(p.cmds_esp32)) p.cmds_esp32.forEach(enfileirarComandoESP32);
  if (Array.isArray(p.eventos)) {
    p.eventos.slice().reverse().forEach((ev) => {
      if (ev && ev.id && !historicoEventos.some((h) => h.id === ev.id)) inserirEvento(ev);
    });
  }
  let tipo = 'TELEMETRIA_UPDATE';
  if (!travadoAntes && estado.alerta_travado) tipo = 'ALERTA_FISICO_E_VIRTUAL_CELULAR';
  else if (travadoAntes && !estado.alerta_travado) tipo = 'ALERTA_ENCERRADO';
  else if (estado.nivel_escalonamento > nivelAntes) tipo = 'ESCALONAMENTO_EMERGENCIA';
  broadcast({ tipo, estado });
  const pendentes = comandosParaLocal;
  comandosParaLocal = [];
  return pendentes;
}

// Ponto único de entrada para pacotes vindos de qualquer canal
function processarPacote(payload, fonte) {
  const p = payload && typeof payload === 'object' ? payload : {};
  if (p._fromLocalMirror) {
    return IS_RENDER_CLOUD ? { comandos_para_local: aplicarEspelho(p) } : {};
  }
  if (p.tipo === 'STATUS_MOTORISTA' || p.status_viagem !== undefined) {
    atualizarTurno(p.status_viagem || (p.ativo === false ? 'INATIVO' : 'EM_VIAGEM'), p.origem || `App do Motorista (${fonte})`);
    return {};
  }
  if (p.gps && !ehPacoteESP32(p)) {
    atualizarGps(p.gps, p.dispositivo || 'CELULAR_MOTORISTA');
    return {};
  }
  if (ehPacoteESP32(p) && modoEspelhoAtivo()) {
    // ESP32 sem acesso ao PC (outra rede ou firewall): a nuvem repassa a telemetria ao servidor local
    comandosParaLocal = comandosParaLocal.filter((c) => c.tipo !== 'TELEMETRIA_ESP32');
    comandosParaLocal.push({ tipo: 'TELEMETRIA_ESP32', pacote: p });
    return respostaESP32();
  }
  if (ehPacoteESP32(p)) {
    processarTelemetriaESP32(p, fonte);
    return respostaESP32();
  }
  return {};
}

// Watchdog: ESP32 offline se não enviar nada por 6s
setInterval(() => {
  if (modoEspelhoAtivo() || !estado.online || !estado.ultimoVisto) return;
  if (Date.now() - new Date(estado.ultimoVisto).getTime() > TIMEOUT_ESP32_MS) {
    estado.online = false;
    inicioRecuperacaoMs = 0;
    registrarEvento('DESCONEXAO', 'Alerta: Conexão com a ESP32 perdida (Timeout)', `Último pacote recebido há mais de ${TIMEOUT_ESP32_MS / 1000} segundos.`);
    broadcast({ tipo: 'STATUS_DISPOSITIVO', online: false, estado });
    espelharParaNuvem(true);
  }
  if (IS_RENDER_CLOUD && estado.modo_espelho && !modoEspelhoAtivo()) {
    estado.modo_espelho = false;
    console.log('[NUVEM] Servidor local parou de espelhar. Nuvem assume o controle.');
  }
}, 2000);

// ------------------------------------------------------------------------------
// ESPELHAMENTO PC -> NUVEM (RENDER) com retorno de comandos da internet
// ------------------------------------------------------------------------------
let ultimoEnvioNuvemMs = 0;
let espelhoEmVoo = false;
let espelhoPendente = false;
let nuvemDisponivel = null;

function espelharParaNuvem(forcarImediato = false) {
  if (!ESPELHAR_NUVEM) return;
  const agora = Date.now();
  const intervaloMin = estado.estado === 'NORMAL' ? 1000 : 250;
  if (espelhoEmVoo) {
    if (forcarImediato) espelhoPendente = true;
    return;
  }
  if (!forcarImediato && agora - ultimoEnvioNuvemMs < intervaloMin) return;
  ultimoEnvioNuvemMs = agora;
  espelhoEmVoo = true;

  const eventosEnviados = eventosParaNuvem.slice();
  const snapshot = {};
  CAMPOS_ESPELHO.forEach((campo) => { snapshot[campo] = estado[campo]; });
  // ESP32 falando só com a nuvem: os comandos para a placa sobem junto com o espelho
  const cmdsEsp32 = estado.online && estado.fonte_conexao === 'NUVEM' ? retirarComandosESP32() : [];
  const bodyStr = JSON.stringify({ _fromLocalMirror: true, estado_completo: snapshot, eventos: eventosEnviados, cmds_esp32: cmdsEsp32 });

  const finalizar = (sucesso) => {
    espelhoEmVoo = false;
    if (sucesso) eventosParaNuvem = eventosParaNuvem.filter((ev) => !eventosEnviados.includes(ev));
    else cmdsEsp32.forEach(enfileirarComandoESP32);
    if (nuvemDisponivel !== sucesso) {
      nuvemDisponivel = sucesso;
      console.log(sucesso ? '[NUVEM] Espelhamento ativo com o Render.' : '[NUVEM] Render indisponível (seguindo 100% local).');
    }
    if (espelhoPendente) {
      espelhoPendente = false;
      setTimeout(() => espelharParaNuvem(true), 50);
    }
  };

  try {
    const urlObj = new URL(RENDER_CLOUD_URL);
    const cliente = urlObj.protocol === 'http:' ? http : https;
    const req = cliente.request({
      hostname: urlObj.hostname,
      port: urlObj.port || (urlObj.protocol === 'http:' ? 80 : 443),
      path: urlObj.pathname,
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(bodyStr) },
      timeout: 5000
    }, (res) => {
      let corpo = '';
      res.setEncoding('utf8');
      res.on('data', (c) => { if (corpo.length < 65536) corpo += c; });
      res.on('end', () => {
        finalizar(res.statusCode === 200);
        try {
          const resposta = JSON.parse(corpo);
          (resposta.comandos_para_local || []).forEach(executarMensagemDaNuvem);
        } catch (e) { }
      });
    });
    req.on('timeout', () => req.destroy(new Error('timeout')));
    req.on('error', () => finalizar(false));
    req.write(bodyStr);
    req.end();
  } catch (e) {
    finalizar(false);
  }
}

function executarMensagemDaNuvem(msg) {
  if (!msg || typeof msg !== 'object') return;
  if (msg.tipo === 'COMANDO') executarComando(msg.comando, `${msg.origem || 'INTERNET'} via nuvem`, msg.extras || {});
  else if (msg.tipo === 'STATUS_MOTORISTA') atualizarTurno(msg.status_viagem, `${msg.origem || 'App do Motorista'} via nuvem`);
  else if (msg.tipo === 'GPS_UPDATE') atualizarGps(msg.gps, 'CELULAR_MOTORISTA (via nuvem)');
  else if (msg.tipo === 'TELEMETRIA_ESP32' && msg.pacote && ehPacoteESP32(msg.pacote)) processarTelemetriaESP32(msg.pacote, 'NUVEM');
}

// Batimento: mantém a nuvem atualizada (e busca comandos) mesmo sem a ESP32
if (ESPELHAR_NUVEM) setInterval(() => espelharParaNuvem(false), 1000);

// ------------------------------------------------------------------------------
// ROTAS HTTP (REST API)
// ------------------------------------------------------------------------------
app.get(['/', '/api'], (req, res) => {
  const wantsHtml = req.accepts(['html', 'json']) === 'html';
  const indexPath = path.join(frontendRoot, 'index.html');
  if (wantsHtml && fs.existsSync(indexPath) && req.path === '/') {
    return res.sendFile(indexPath);
  }
  res.json({
    projeto: 'INOVATECH SMART DRIVER — BACKEND API',
    status: 'OPERACIONAL',
    servidor: estado.servidor,
    porta: PORT,
    esp32_online: estado.online,
    estado_atual: estado.estado,
    clientes_ws: wss.clients.size,
    documentacao_endpoints: {
      'POST /api/telemetry': 'ESP32 envia leitura em tempo real (resposta traz os comandos pendentes)',
      'GET /api/telemetry': 'Estado consolidado atual + eventos recentes',
      'POST /api/command': 'DISPARAR_TESTE, SIMULAR_ALERTA, SILENCIAR_ALARME, CALIBRAR_MPU, TOGGLE_RELE1/2',
      'POST /api/driver/shift': 'Inicia/pausa a viagem do motorista',
      'POST /api/gps': 'Posição GPS do celular do motorista',
      'GET /api/events': 'Histórico cronológico de alarmes e eventos',
      'GET /api/health': 'Status de integridade do servidor'
    }
  });
});

app.post('/api/telemetry', (req, res) => {
  const resultado = processarPacote(req.body, 'HTTP');
  if (resultado.cmds) {
    // Resposta enxuta para a ESP32 (o firmware procura os nomes dos comandos no texto)
    return res.status(200).json({ sucesso: true, srv: 'INOVATECH', cmds: resultado.cmds, mon: resultado.mon, exib: resultado.exib });
  }
  return res.status(200).json({ sucesso: true, estado_processado: estado.estado, ...resultado });
});

app.get(['/api/telemetry/current', '/api/telemetry/latest'], (req, res) => {
  res.json({ sucesso: true, estado, timestamp: new Date().toISOString() });
});

app.get('/api/telemetry', (req, res) => {
  res.json({ sucesso: true, estado, eventosRecentes: historicoEventos.slice(0, 10) });
});

app.post(['/api/driver/shift', '/api/driver/status'], (req, res) => {
  const { status_viagem = 'EM_VIAGEM', origem } = req.body || {};
  atualizarTurno(status_viagem, origem || 'App do Motorista (REST)');
  return res.json({ sucesso: true, motoristas: estado.motoristas });
});

app.post('/api/command', (req, res) => {
  const { comando, origem = 'CENTRAL_WEB', ...extras } = req.body || {};
  if (!comando) return res.status(400).json({ erro: 'Comando não especificado' });
  const r = executarComando(comando, origem, extras);
  return res.status(r.ok ? 200 : 400).json({ sucesso: r.ok, mensagem: r.mensagem, estado });
});

app.post('/api/gps', (req, res) => {
  const body = req.body || {};
  if (atualizarGps(body.gps || body, body.origem)) return res.json({ sucesso: true, gps: estado.gps });
  return res.status(400).json({ erro: 'Coordenadas inválidas' });
});

// Compatibilidade: ESP32 antiga consumindo a fila de comandos
app.get('/api/commands', (req, res) => {
  const enviados = retirarComandosESP32();
  res.json({ sucesso: true, total: enviados.length, comandos: enviados });
});

app.get('/api/events', (req, res) => {
  res.json({ sucesso: true, total: historicoEventos.length, eventos: historicoEventos });
});

app.post('/api/events/clear', (req, res) => {
  historicoEventos = [];
  registrarEvento('NORMAL', 'Histórico de eventos resetado pela Central.');
  res.json({ sucesso: true, mensagem: 'Histórico limpo' });
});

app.get(['/health', '/api/health'], (req, res) => {
  res.status(200).json({
    status: 'ONLINE',
    servidor: estado.servidor,
    modo_espelho: modoEspelhoAtivo(),
    uptime_segundos: Math.floor(process.uptime()),
    clientes_conectados: wss.clients.size,
    esp32_conectado: estado.online,
    esp32_fonte: estado.fonte_conexao,
    timestamp: new Date().toISOString()
  });
});

// ------------------------------------------------------------------------------
// GERENCIAMENTO DE WEBSOCKETS
// ------------------------------------------------------------------------------
wss.on('connection', (ws, req) => {
  ws.vivo = true;
  ws.on('pong', () => { ws.vivo = true; });
  console.log(`[WS] Novo cliente conectado: ${req.socket.remoteAddress} (Total ativos: ${wss.clients.size})`);

  ws.send(JSON.stringify({ tipo: 'ESTADO_INICIAL', estado, eventos: historicoEventos.slice(0, 20) }));

  ws.on('message', (message) => {
    let parsed;
    try { parsed = JSON.parse(message); } catch (err) { return; }
    if (!parsed || typeof parsed !== 'object') return;

    if (parsed.tipo === 'TELEMETRIA') {
      processarPacote(parsed.dados || parsed, 'WS');
    } else if (parsed.tipo === 'COMANDO') {
      const r = executarComando(parsed.comando, parsed.origem || 'CENTRAL (WS)', parsed.extras || {});
      if (!r.ok) ws.send(JSON.stringify({ tipo: 'ERRO_COMANDO', mensagem: r.mensagem }));
    } else if (parsed.tipo === 'GPS_UPDATE' && parsed.gps) {
      atualizarGps(parsed.gps, 'CELULAR_MOTORISTA');
    } else if (parsed.tipo === 'STATUS_MOTORISTA') {
      atualizarTurno(parsed.status_viagem || (parsed.ativo === false ? 'INATIVO' : 'EM_VIAGEM'), parsed.origem || 'App do Motorista (WS)');
    } else if (parsed.tipo === 'PING') {
      ws.send(JSON.stringify({ tipo: 'PONG', t: Date.now() }));
    }
  });

  ws.on('close', () => console.log(`[WS] Cliente desconectado (Ativos restantes: ${wss.clients.size})`));
  ws.on('error', (err) => console.error('[WS ERROR]:', err.message));
});

// Derruba conexões "zumbis" e mantém o túnel do Render aberto
setInterval(() => {
  wss.clients.forEach((ws) => {
    if (ws.vivo === false) return ws.terminate();
    ws.vivo = false;
    try { ws.ping(); } catch (e) { }
  });
}, 25000);

// ------------------------------------------------------------------------------
// RECEPTOR UDP (ZERO CONFIGURAÇÃO DE IP) — responde com os comandos pendentes
// ------------------------------------------------------------------------------
const udpServer = dgram.createSocket({ type: 'udp4', reuseAddr: true });

udpServer.on('message', (msg, rinfo) => {
  let dados;
  try {
    const raw = msg.toString('utf8').trim();
    if (!raw.startsWith('{')) return;
    dados = JSON.parse(raw);
  } catch (e) { return; }

  const resultado = processarPacote(dados, 'UDP');
  // A resposta ensina o IP do servidor para a ESP32 e entrega os comandos da Central
  const resposta = Buffer.from(JSON.stringify({ srv: 'INOVATECH', cmds: resultado.cmds || [], mon: resultado.mon, exib: resultado.exib }));
  udpServer.send(resposta, rinfo.port, rinfo.address, () => { });
});

udpServer.on('error', (err) => {
  console.log(`[UDP ERROR]: ${err.message} (a porta ${UDP_PORT} pode estar em uso por outro server.js aberto)`);
});

if (!IS_RENDER_CLOUD) {
  udpServer.bind(UDP_PORT, '0.0.0.0', () => {
    try { udpServer.setBroadcast(true); } catch (e) { }
    console.log(`  ✔ Receptor UDP da ESP32 ativo na porta: ${UDP_PORT}`);
  });
}

// ------------------------------------------------------------------------------
// PONTE USB SERIAL OPCIONAL (WINDOWS) — ative com PONTE_SERIAL=COM5 (ou "auto") no .env
// Fica desligada por padrão para a porta COM ficar livre para o PlatformIO gravar.
// ------------------------------------------------------------------------------
let serialBridgeProc = null;

function iniciarPonteSerialUSB(portaPreferida) {
  if (process.platform !== 'win32' || IS_RENDER_CLOUD) return;
  const porta = /^COM\d+$/i.test(portaPreferida) ? portaPreferida.toUpperCase() : '';

  const psScript = `
    $ErrorActionPreference = 'SilentlyContinue'
    $preferida = '${porta}'
    while ($true) {
      $ports = [System.IO.Ports.SerialPort]::getportnames()
      $target = if ($preferida -and ($ports -contains $preferida)) { $preferida } elseif (-not $preferida) { $ports | Select-Object -First 1 } else { $null }
      if ($target) {
        $sp = New-Object System.IO.Ports.SerialPort $target, 115200, 'None', 8, 'One'
        $sp.ReadTimeout = 1000
        try {
          $sp.Open()
          Write-Output "SERIAL_CONNECTED:$target"
          while ($sp.IsOpen) {
            try {
              $line = $sp.ReadLine()
              if ($line) { Write-Output $line.Trim() }
            } catch [System.TimeoutException] {
            } catch {
              break
            }
          }
        } catch {
        } finally {
          if ($sp -and $sp.IsOpen) { $sp.Close() }
        }
      }
      Start-Sleep -Seconds 2
    }
  `;

  serialBridgeProc = spawn('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', psScript], {
    stdio: ['ignore', 'pipe', 'ignore']
  });

  let buffer = '';
  serialBridgeProc.stdout.on('data', (chunk) => {
    buffer += chunk.toString('utf8');
    const lines = buffer.split(/\r?\n/);
    buffer = lines.pop() || '';
    for (const rawLine of lines) {
      const line = rawLine.trim();
      if (line.startsWith('SERIAL_CONNECTED:')) {
        const portName = line.split(':')[1];
        console.log(`[USB SERIAL] Conectado à ESP32 na porta ${portName} (115200 bps)`);
      } else if (line.startsWith('TELEMETRY:')) {
        try {
          processarPacote(JSON.parse(line.substring('TELEMETRY:'.length)), 'SERIAL');
        } catch (e) { }
      } else if (line) {
        console.log(`[ESP32] ${line}`);
      }
    }
  });

  serialBridgeProc.on('exit', () => {
    serialBridgeProc = null;
    setTimeout(() => iniciarPonteSerialUSB(portaPreferida), 3000);
  });
}

function listarIpsDaRede() {
  const ips = [];
  Object.entries(require('os').networkInterfaces()).forEach(([nome, interfaces]) => {
    (interfaces || []).forEach((i) => {
      if (i.family === 'IPv4' && !i.internal && !/vEthernet|VirtualBox|VMware|WSL/i.test(nome)) ips.push(`${i.address} (${nome})`);
    });
  });
  return ips;
}

server.listen(PORT, '0.0.0.0', () => {
  if (!IS_RENDER_CLOUD) {
    console.log('\n  Endereços deste PC na rede (use no celular: http://ENDEREÇO:' + PORT + '/motorista.html):');
    listarIpsDaRede().forEach((ip) => console.log('    • ' + ip));
  }
  console.log(`
  ===================================================================
    INOVA TECH SMART DRIVER — BACKEND ATIVO (${estado.servidor})
  ===================================================================
  ✔ Servidor HTTP & WebSocket na porta: ${PORT}
  ✔ Central Tech:        http://localhost:${PORT}/central.html
  ✔ App do Motorista:    http://<IP-DO-PC>:${PORT}/motorista.html
  ✔ WebSocket:           ws://localhost:${PORT}/ws
  ✔ Health Check:        http://localhost:${PORT}/health
  ✔ Espelho na nuvem:    ${ESPELHAR_NUVEM ? RENDER_CLOUD_URL : 'desativado'}
  ===================================================================
  `);
  if (process.env.PONTE_SERIAL) iniciarPonteSerialUSB(process.env.PONTE_SERIAL);
});

server.on('error', (err) => {
  if (err.code === 'EADDRINUSE') {
    console.error(`\n[ERRO] A porta ${PORT} já está em uso. Feche a outra janela do servidor (ou o processo node.exe) e tente de novo.\n`);
    process.exit(1);
  }
  throw err;
});

// Encerramento gracioso para orquestradores de nuvem (Render, Docker, Kubernetes)
function encerrarGraciosamente(sinal) {
  console.log(`[SERVER] Sinal ${sinal} recebido. Encerrando conexões graciosamente...`);
  if (serialBridgeProc) {
    serialBridgeProc.removeAllListeners('exit');
    try { serialBridgeProc.kill(); } catch (e) { }
  }
  try { udpServer.close(); } catch (e) { }
  wss.clients.forEach((client) => {
    try { client.close(1001, 'Servidor reiniciando'); } catch (e) { }
  });
  server.close(() => {
    console.log('[SERVER] Servidor HTTP e WebSocket finalizados com sucesso.');
    process.exit(0);
  });
  setTimeout(() => process.exit(0), 2000).unref();
}

process.on('SIGTERM', () => encerrarGraciosamente('SIGTERM'));
process.on('SIGINT', () => encerrarGraciosamente('SIGINT'));
