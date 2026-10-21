/**
 * ==============================================================================
 * INOVATECH SMART DRIVER — BACKEND API & WEBSOCKET ENGINE (TCC)
 * ==============================================================================
 * Servidor central de telemetria em tempo real para comunicação com o ESP32,
 * processamento do filtro temporal de segurança (2500 ms) e transmissão
 * instantânea para o Painel de Controle na Central (Vercel ou Local).
 * 
 * Port: 3001 (ou process.env.PORT)
 * Protocolos: HTTP REST + WebSockets (/ws)
 * ==============================================================================
 */

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
require('dotenv').config();

const app = express();
const PORT = process.env.PORT || 3001;

// Configuração para suporte a proxies reversos do Render / Nginx
app.set('trust proxy', 1);

// Configuração de Middlewares
app.use(cors({
  origin: '*', // Permite Vercel, localhost e qualquer origem autorizada
  methods: ['GET', 'POST', 'OPTIONS'],
  allowedHeaders: ['Content-Type', 'Authorization']
}));
app.use(express.json());

// Servir frontend estático para apresentação local e contingência
const frontendRoot = path.join(__dirname, '..');
if (fs.existsSync(path.join(frontendRoot, 'index.html'))) {
  app.use(express.static(frontendRoot));
}

// Criação do servidor HTTP integrado
const server = http.createServer(app);

// Servidor WebSocket no endpoint /ws
const wss = new WebSocket.Server({ server, path: '/ws' });

// ------------------------------------------------------------------------------
// ESTADO GLOBAL DO SISTEMA EM MEMÓRIA
// ------------------------------------------------------------------------------
const estado = {
  dispositivo: 'ESP32_SMART_DRIVER_01',
  online: false,
  ultimoVisto: null,
  sensor_ir: 1, // 1 = HIGH (Cabeça erguida), 0 = LOW (Cabeça baixa)
  tempo_cabeca_baixa_ms: 0,
  limiar_seguranca_ms: 1500,
  limiar_seguranca_ms: 1500,
  estado: 'NORMAL', // 'NORMAL', 'PRE_ALERTA', 'ALARME_ATIVADO'
  buzzer: 0, // 0 = Desligado, 1 = Ligado
  rele: 0, // 0 = Desligado, 1 = Acionado
  acelerometro: {
    pitch: 0.0,
    roll: 0.0,
    yaw: 0.0,
    pescada_detectada: false,
    status: 'Aguardando módulo / Simulação'
  },
  contadores: {
    totalAlarmesSessao: 0,
    inicioSessao: new Date().toISOString()
  },
  motoristas: {
    matheus_garcia: {
      nome: 'Matheus Garcia',
      status: 'EM_VIAGEM',
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
let historicoEventos = [
  {
    id: 'evt_init',
    timestamp: new Date().toISOString(),
    horaFormatada: new Date().toLocaleTimeString('pt-BR'),
    tipo: 'NORMAL',
    mensagem: 'Central de Operações Inicializada. Aguardando conexão do ESP32...',
    detalhes: 'Sistema em prontidão operacional.'
  }
];

// Fila de comandos pendentes para a placa ESP32
let comandosPendentes = [];

// ------------------------------------------------------------------------------
// PERSISTÊNCIA DE DADOS EM DISCO (STORAGE LOCAL & NUVEM)
// ------------------------------------------------------------------------------
const DATA_DIR = path.join(__dirname, 'data');
if (!fs.existsSync(DATA_DIR)) {
  try { fs.mkdirSync(DATA_DIR, { recursive: true }); } catch (e) { }
}
const EVENTOS_FILE = path.join(DATA_DIR, 'historico_eventos.json');
const ESTADO_FILE = path.join(DATA_DIR, 'ultimo_estado.json');

// Carregar histórico prévio do disco
try {
  if (fs.existsSync(EVENTOS_FILE)) {
    const raw = fs.readFileSync(EVENTOS_FILE, 'utf8');
    const parsed = JSON.parse(raw);
    if (Array.isArray(parsed) && parsed.length > 0) {
      historicoEventos = parsed;
      console.log(`[STORAGE] Carregados ${historicoEventos.length} eventos persistidos do disco.`);
    }
  }
} catch (e) {
  console.warn('[STORAGE] Falha ao ler historico_eventos.json:', e.message);
}

// Carregar contadores e estado prévio do disco
try {
  if (fs.existsSync(ESTADO_FILE)) {
    const rawEst = fs.readFileSync(ESTADO_FILE, 'utf8');
    const parsedEst = JSON.parse(rawEst);
    if (parsedEst && typeof parsedEst === 'object') {
      if (parsedEst.contadores && parsedEst.contadores.totalAlarmesSessao !== undefined) {
        estado.contadores.totalAlarmesSessao = parsedEst.contadores.totalAlarmesSessao;
      }
      if (parsedEst.estado) estado.estado = parsedEst.estado;
      console.log(`[STORAGE] Estado persistido carregado. Total acumulado de alarmes: ${estado.contadores.totalAlarmesSessao}`);
    }
  }
} catch (e) {
  console.warn('[STORAGE] Falha ao ler ultimo_estado.json:', e.message);
}

// Função para salvar alterações no disco com debounce
let salvarTimer = null;
function persistirEmDisco() {
  clearTimeout(salvarTimer);
  salvarTimer = setTimeout(() => {
    try {
      fs.writeFile(EVENTOS_FILE, JSON.stringify(historicoEventos, null, 2), (err) => {
        if (err) console.error('[STORAGE ERROR] Ao salvar eventos:', err.message);
      });
      fs.writeFile(ESTADO_FILE, JSON.stringify(estado, null, 2), (err) => {
        if (err) console.error('[STORAGE ERROR] Ao salvar estado:', err.message);
      });
    } catch (e) {
      console.error('[STORAGE] Erro crítico de persistência:', e.message);
    }
  }, 100);
}

// ------------------------------------------------------------------------------
// FUNÇÕES AUXILIARES DE TELEMETRIA
// ------------------------------------------------------------------------------
function registrarEvento(tipo, mensagem, detalhes = '') {
  const novoEvento = {
    id: 'evt_' + Date.now() + '_' + Math.random().toString(36).substr(2, 4),
    timestamp: new Date().toISOString(),
    horaFormatada: new Date().toLocaleTimeString('pt-BR'),
    tipo,
    mensagem,
    detalhes
  };

  historicoEventos.unshift(novoEvento);
  if (historicoEventos.length > MAX_HISTORICO) {
    historicoEventos.pop();
  }

  // Persiste no arquivo JSON em disco
  persistirEmDisco();

  // Notifica todos os clientes conectados via WebSocket
  broadcast({
    tipo: 'NOVO_EVENTO',
    evento: novoEvento
  });

  return novoEvento;
}

function broadcast(dados) {
  const mensagemStr = JSON.stringify(dados);
  wss.clients.forEach((client) => {
    if (client.readyState === WebSocket.OPEN) {
      try {
        client.send(mensagemStr);
      } catch (err) {
        console.error('[WS BROADCAST ERROR]:', err.message);
      }
    }
  });
}

// Watchdog para detectar se o ESP32 ficou offline (se não enviar ping por 6s)
setInterval(() => {
  if (estado.ultimoVisto) {
    const diffMs = Date.now() - new Date(estado.ultimoVisto).getTime();
    if (diffMs > 6000 && estado.online) {
      estado.online = false;
      registrarEvento('DESCONEXAO', 'Alerta: Conexão com o ESP32 perdida (Timeout)', 'Último pacote recebido há mais de 6 segundos.');
      broadcast({
        tipo: 'STATUS_DISPOSITIVO',
        online: false,
        estado
      });
    }
  }
}, 2000);

// ------------------------------------------------------------------------------
// ROTAS HTTP (REST API)
// ------------------------------------------------------------------------------

// Rota raiz inteligente: serve o frontend para navegadores ou JSON descritivo para APIs
app.get(['/', '/api'], (req, res) => {
  const wantsHtml = req.accepts(['html', 'json']) === 'html';
  const indexPath = path.join(frontendRoot, 'index.html');
  if (wantsHtml && fs.existsSync(indexPath) && req.path === '/') {
    return res.sendFile(indexPath);
  }
  res.json({
    projeto: 'INOVATECH SMART DRIVER — BACKEND API',
    status: 'OPERACIONAL',
    porta: PORT,
    esp32_online: estado.online,
    estado_atual: estado.estado,
    clientes_ws: wss.clients.size,
    documentacao_endpoints: {
      'POST /api/telemetry': 'ESP32 envia dados de leitura em tempo real',
      'GET /api/telemetry': 'Central obtém estado consolidado atual',
      'POST /api/command': 'Central envia comando remoto (DISPARAR_TESTE, SILENCIAR, RESET)',
      'GET /api/commands': 'ESP32 obtém comandos pendentes para execução',
      'GET /api/events': 'Lista histórico cronológico de alarmes e eventos',
      'GET /api/health': 'Status de integridade do servidor'
    }
  });
});

// Espelhamento automático para a Nuvem (Render) quando rodando localmente no PC
const https = require('https');
const { execFile } = require('child_process');
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

// Quando o motorista clica em "Vou parar para descansar", retorna automaticamente para o app da câmera Linklemo
function voltarParaLinklemoCelularADB() {
  if (IS_RENDER_CLOUD || !fs.existsSync(ADB_PATH) || !chamadaCelularAbertaViaAdb) return;
  chamadaCelularAbertaViaAdb = false;
  execFile(ADB_PATH, ['shell', 'input', 'keyevent', '4'], { timeout: 3000 }, () => { });
}

// ------------------------------------------------------------------------------
// MOTOR DE ESCALONAMENTO DE EMERGÊNCIA (FASE 1: CELULAR -> FASE 2: +10s CRÍTICO -> FASE 3: +10s POSSÍVEL ACIDENTE)
// ------------------------------------------------------------------------------
let inicioSemRespostaTimestamp = 0;
let cooldownNormalizacaoUntil = 0;

function iniciarProtocoloAlertaTravado() {
  if (!estado.chamada_celular_ativa) {
    estado.chamada_celular_ativa = true;
    estado.alerta_travado = true;
    inicioSemRespostaTimestamp = Date.now();
    estado.tempo_sem_resposta_ms = 0;
    estado.nivel_escalonamento = 1; // 1 = Alerta Físico + Celular | 2 = Alerta Crítico Central (>10s) | 3 = Possível Acidente + SOS (>20s)
    dispararChamadaCelularADB();
  }
}

function encerrarProtocoloAlertaTravado() {
  estado.chamada_celular_ativa = false;
  estado.alerta_travado = false;
  inicioSemRespostaTimestamp = 0;
  estado.tempo_sem_resposta_ms = 0;
  estado.nivel_escalonamento = 0;
  estado.estado = 'NORMAL';
  estado.buzzer = 0;
  estado.rele = 0;
  estado.tempo_cabeca_baixa_ms = 0;
  cooldownNormalizacaoUntil = Date.now() + 2500;
  voltarParaLinklemoCelularADB();
  espelharParaNuvem({
    dispositivo: estado.dispositivo,
    sensor_ir: 1,
    tempo_cabeca_baixa_ms: 0,
    buzzer: 0,
    rele: 0,
    estado: 'NORMAL',
    chamada_celular_ativa: false,
    alerta_travado: false,
    tempo_sem_resposta_ms: 0,
    nivel_escalonamento: 0,
    _forceReset: true,
    acelerometro: estado.acelerometro
  }, true);
  persistirEmDisco();
}

// Loop de alta precisão (250ms) que monitora o tempo sem resposta do motorista no celular
setInterval(() => {
  if (!estado.chamada_celular_ativa || !inicioSemRespostaTimestamp) return;

  const decorridoMs = Date.now() - inicioSemRespostaTimestamp;
  estado.tempo_sem_resposta_ms = decorridoMs;
  estado.estado = 'ALARME_ATIVADO';
  estado.buzzer = 1;
  estado.rele = 1;

  let mudouNivel = false;

  // Passou de 10 segundos sem o motorista desativar o alerta no celular -> ALERTA CRÍTICO NA CENTRAL
  if (decorridoMs >= 10000 && decorridoMs < 20000 && estado.nivel_escalonamento < 2) {
    estado.nivel_escalonamento = 2;
    mudouNivel = true;
    registrarEvento(
      'ALARME_ATIVADO',
      '🚨 ALERTA CRÍTICO NA CENTRAL (10s SEM RESPOSTA): Motorista não desativou o alerta no celular!',
      'Iniciando contagem crítica de +10s para protocolo de Possível Acidente.'
    );
    // Reforça despertar da tela do celular via ADB
    dispararChamadaCelularADB();
  }

  // Passou de +10 segundos após o Alerta Crítico (20s no total) -> POSSÍVEL ACIDENTE + PROTOCOLO SAFE-STOP & SOS
  if (decorridoMs >= 20000 && estado.nivel_escalonamento < 3) {
    estado.nivel_escalonamento = 3;
    mudouNivel = true;
    registrarEvento(
      'ALARME_ATIVADO',
      '💥 POSSÍVEL ACIDENTE DETECTADO (20s SEM RESPOSTA)! Protocolo Autônomo Safe-Stop & Resgate SOS Acionado na Central!',
      'Desaceleração veicular via Relé (GPIO 18), Pisca-Alerta e envio de GPS para Resgate/SAMU 192.'
    );
  }

  broadcast({
    tipo: mudouNivel ? 'ESCALONAMENTO_EMERGENCIA' : 'TELEMETRIA_UPDATE',
    estado,
    comandosPendentesParaESP32: comandosPendentes.length
  });

  espelharParaNuvem({
    dispositivo: estado.dispositivo,
    sensor_ir: estado.sensor_ir,
    tempo_cabeca_baixa_ms: estado.tempo_cabeca_baixa_ms,
    buzzer: estado.buzzer,
    rele: estado.rele,
    estado: estado.estado,
    chamada_celular_ativa: true,
    alerta_travado: true,
    tempo_sem_resposta_ms: estado.tempo_sem_resposta_ms,
    nivel_escalonamento: estado.nivel_escalonamento,
    acelerometro: estado.acelerometro
  }, mudouNivel);
}, 250);

const RENDER_CLOUD_URL = process.env.RENDER_CLOUD_URL || 'https://inovatech-tcc.onrender.com/api/telemetry';
const IS_RENDER_CLOUD = Boolean(process.env.RENDER || process.env.RENDER_EXTERNAL_URL);
let ultimoEnvioNuvemMs = 0;
let ultimoEstadoEnviadoNuvem = '';

function espelharParaNuvem(payloadEspelho, forcarImediato = false) {
  if (IS_RENDER_CLOUD || payloadEspelho._fromLocalMirror) return;
  const agora = Date.now();
  const mudouEstado = payloadEspelho.estado !== ultimoEstadoEnviadoNuvem;
  const intervaloMin = (payloadEspelho.estado === 'PRE_ALERTA' || payloadEspelho.estado === 'ALARME_ATIVADO') ? 120 : 600;
  if (!forcarImediato && !mudouEstado && (agora - ultimoEnvioNuvemMs < intervaloMin)) {
    return;
  }
  ultimoEnvioNuvemMs = agora;
  ultimoEstadoEnviadoNuvem = payloadEspelho.estado;

  try {
    const bodyStr = JSON.stringify({ ...payloadEspelho, _fromLocalMirror: true });
    const urlObj = new URL(RENDER_CLOUD_URL);
    const req = https.request({
      hostname: urlObj.hostname,
      port: 443,
      path: urlObj.pathname,
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(bodyStr)
      },
      timeout: 4000
    }, (res) => {
      res.resume();
    });
    req.on('error', () => { });
    req.write(bodyStr);
    req.end();
  } catch (e) { }
}

// Função centralizada de processamento de telemetria (HTTP, WebSocket e USB Serial)
function processarTelemetria(payload = {}) {
  const estadoAnterior = estado.estado;

  // Atualiza flags de presença
  estado.online = true;
  estado.ultimoVisto = new Date().toISOString();

  if (payload.dispositivo) estado.dispositivo = payload.dispositivo;

  // Sincronização de Turno / Viagem do Motorista
  if (payload.tipo === 'STATUS_MOTORISTA' || payload.status_viagem !== undefined) {
    if (!estado.motoristas) estado.motoristas = {};
    if (!estado.motoristas.matheus_garcia) {
      estado.motoristas.matheus_garcia = {
        nome: 'Matheus Garcia',
        status: 'EM_VIAGEM',
        veiculo: 'Scania R-450 (Placa INO-2026)',
        rota: 'Rodovia BR-116 KM 284 · Régis Bittencourt'
      };
    }
    const novoStatus = payload.status_viagem || (payload.ativo ? 'EM_VIAGEM' : 'INATIVO');
    estado.motoristas.matheus_garcia.status = novoStatus;
    estado.motoristas.matheus_garcia.atualizadoEm = new Date().toISOString();
    registrarEvento('TURNO_MOTORISTA', `Condutor Matheus Garcia: Viagem ${novoStatus === 'EM_VIAGEM' ? 'Ativada' : 'Pausada'}`, `Origem: ${payload.origem || 'App do Motorista'}`);
    broadcast({
      tipo: 'STATUS_MOTORISTA',
      motorista: 'Matheus Garcia',
      status_viagem: novoStatus,
      estado
    });
    persistirEmDisco();
  }

  // Sensor IR (GPIO 32)
  if (payload.sensor_ir !== undefined) {
    estado.sensor_ir = Number(payload.sensor_ir);
  }

  // Tempo de cabeça baixa contínuo em ms
  if (payload.tempo_cabeca_baixa_ms !== undefined) {
    estado.tempo_cabeca_baixa_ms = Math.max(0, Number(payload.tempo_cabeca_baixa_ms));
  }

  // Atuadores
  if (payload.buzzer !== undefined) estado.buzzer = Number(payload.buzzer);
  if (payload.rele !== undefined) estado.rele = Number(payload.rele);

  // Acelerômetro (I2C)
  if (payload.acelerometro && typeof payload.acelerometro === 'object') {
    estado.acelerometro = {
      pitch: Number(payload.acelerometro.pitch || 0),
      roll: Number(payload.acelerometro.roll || 0),
      yaw: Number(payload.acelerometro.yaw || 0),
      pescada_detectada: Boolean(payload.acelerometro.pescada_detectada),
      status: payload.acelerometro.status || 'Ativo'
    };
  }

  // Se vier espelhado do backend local para a nuvem, preserva campos de escalonamento
  if (payload.tempo_sem_resposta_ms !== undefined) {
    estado.tempo_sem_resposta_ms = Number(payload.tempo_sem_resposta_ms);
  }
  if (payload.nivel_escalonamento !== undefined) {
    estado.nivel_escalonamento = Number(payload.nivel_escalonamento);
  }
  if (payload.alerta_travado !== undefined) {
    estado.alerta_travado = Boolean(payload.alerta_travado);
  }

  // Enquanto o motorista NÃO apertar para desativar o alerta no celular, mantém o alerta travado ativo!
  if (estado.alerta_travado && !payload._forceReset) {
    estado.estado = 'ALARME_ATIVADO';
    estado.buzzer = 1;
    estado.rele = 1;
    estado.chamada_celular_ativa = true;
    estado.tempo_cabeca_baixa_ms = Math.max(3000, estado.tempo_cabeca_baixa_ms || 3000);
  } else if (Date.now() < cooldownNormalizacaoUntil && payload.estado !== 'ALARME_ATIVADO') {
    estado.estado = 'NORMAL';
    estado.tempo_cabeca_baixa_ms = 0;
    estado.buzzer = 0;
    estado.rele = 0;
  } else if (payload.estado) {
    estado.estado = payload.estado;
  } else {
    if (estado.sensor_ir === 0) {
      if (estado.tempo_cabeca_baixa_ms >= estado.limiar_seguranca_ms) {
        estado.estado = 'ALARME_ATIVADO';
        estado.buzzer = 1;
        estado.rele = 1;
      } else {
        estado.estado = 'PRE_ALERTA';
      }
    } else {
      estado.estado = 'NORMAL';
      estado.tempo_cabeca_baixa_ms = 0;
      estado.buzzer = 0;
      estado.rele = 0;
    }
  }

  // Detecção de transição de estado para gerar log e métricas
  const houveTransicao = (estadoAnterior !== estado.estado);
  if (houveTransicao) {
    if (estado.estado === 'PRE_ALERTA') {
      registrarEvento('PRE_ALERTA', 'Aviso: Cabeça Baixa Detectada (Iniciando contagem de segurança)', `Tempo decorrido: ${estado.tempo_cabeca_baixa_ms}ms`);
    } else if (estado.estado === 'ALARME_ATIVADO') {
      estado.contadores.totalAlarmesSessao++;
      iniciarProtocoloAlertaTravado();
      registrarEvento('ALARME_ATIVADO', 'PERIGO: CABEÇA BAIXA DETECTADA (1,5s)!', `Alerta Físico (Buzzer/Relé) e Virtual no Celular acionados. Aguardando motorista desativar!`);
      broadcast({
        tipo: 'ALERTA_FISICO_E_VIRTUAL_CELULAR',
        dispararChamadaCafe: true,
        estado
      });
    } else if (estado.estado === 'NORMAL') {
      registrarEvento('NORMAL', 'Postura Normalizada: Motorista confirmou alerta', 'Alarme físico e virtual encerrados.');
    }
  }

  // Transmite atualização aos navegadores em tempo real via WebSocket
  broadcast({
    tipo: 'TELEMETRIA_UPDATE',
    estado,
    comandosPendentesParaESP32: comandosPendentes.length
  });

  // Espelha imediatamente para a nuvem Render para que TODOS os dispositivos recebam o alerta
  if (payload.chamada_celular_ativa !== undefined) {
    estado.chamada_celular_ativa = Boolean(payload.chamada_celular_ativa);
  }
  if (!payload._fromLocalMirror) {
    espelharParaNuvem({
      dispositivo: estado.dispositivo,
      sensor_ir: estado.sensor_ir,
      tempo_cabeca_baixa_ms: estado.tempo_cabeca_baixa_ms,
      buzzer: estado.buzzer,
      rele: estado.rele,
      estado: estado.estado,
      chamada_celular_ativa: Boolean(estado.chamada_celular_ativa),
      alerta_travado: Boolean(estado.alerta_travado),
      tempo_sem_resposta_ms: estado.tempo_sem_resposta_ms || 0,
      nivel_escalonamento: estado.nivel_escalonamento || 0,
      acelerometro: estado.acelerometro
    }, houveTransicao);
  }

  // Salva no disco apenas quando houver transição de estado (evita conflitos de sincronização contínua no OneDrive/VS Code)
  if (houveTransicao) {
    persistirEmDisco();
  }
}

// Endpoint de telemetria recebida do ESP32 ou simulador

// ------------------------------------------------------------------------------
// MOTOR DE ALTA FREQUÊNCIA PARA CONTAGEM SUAVE DO CRONÔMETRO (20 FPS / 50ms)
// ------------------------------------------------------------------------------
let inicioCabecaBaixaServerMs = 0;

setInterval(() => {
  if (estado.sensor_ir === 0 && estado.estado !== 'ALARME_ATIVADO' && !estado.alerta_travado) {
    if (!inicioCabecaBaixaServerMs) {
      inicioCabecaBaixaServerMs = Date.now() - (estado.tempo_cabeca_baixa_ms || 0);
    }
    const decorrido = Date.now() - inicioCabecaBaixaServerMs;
    estado.tempo_cabeca_baixa_ms = decorrido;

    if (decorrido >= 1500) {
      estado.estado = 'ALARME_ATIVADO';
      estado.buzzer = 1;
      estado.rele = 1;
      estado.contadores.totalAlarmesSessao++;
      iniciarProtocoloAlertaTravado();
      registrarEvento('ALARME_ATIVADO', 'PERIGO: CABEÇA BAIXA DETECTADA (1,5s)!', 'Alerta Físico e Virtual acionados.');
      broadcast({
        tipo: 'ALERTA_FISICO_E_VIRTUAL_CELULAR',
        dispararChamadaCafe: true,
        estado
      });
    } else {
      estado.estado = 'PRE_ALERTA';
    }

    // Transmite a cada 50ms para Central e Celular acompanharem com fluidez total
    broadcast({
      tipo: 'TELEMETRIA_UPDATE',
      estado,
      comandosPendentesParaESP32: comandosPendentes.length
    });
  } else if (estado.sensor_ir === 1) {
    inicioCabecaBaixaServerMs = 0;
  }
}, 50);

app.post('/api/telemetry', (req, res) => {
  processarTelemetria(req.body || {});
  return res.status(200).json({
    sucesso: true,
    estado_processado: estado.estado,
    comandos_pendentes: comandosPendentes
  });
});

// Endpoint dedicado para ligar/desligar turno do motorista via REST
app.post(['/api/driver/shift', '/api/driver/status'], (req, res) => {
  const { status_viagem = 'EM_VIAGEM', motorista = 'Matheus Garcia' } = req.body || {};
  if (!estado.motoristas) estado.motoristas = {};
  if (!estado.motoristas.matheus_garcia) {
    estado.motoristas.matheus_garcia = {
      nome: 'Matheus Garcia',
      status: 'EM_VIAGEM',
      veiculo: 'Scania R-450 (Placa INO-2026)',
      rota: 'Rodovia BR-116 KM 284 · Régis Bittencourt'
    };
  }
  const novoStatus = (status_viagem === 'INATIVO') ? 'INATIVO' : 'EM_VIAGEM';
  estado.motoristas.matheus_garcia.status = novoStatus;
  estado.motoristas.matheus_garcia.atualizadoEm = new Date().toISOString();
  registrarEvento('TURNO_MOTORISTA', `Condutor ${motorista}: Turno ${novoStatus === 'EM_VIAGEM' ? 'Iniciado' : 'Pausado'} (REST)`);
  broadcast({
    tipo: 'STATUS_MOTORISTA',
    motorista,
    status_viagem: novoStatus,
    estado
  });
  persistirEmDisco();
  return res.json({ sucesso: true, motoristas: estado.motoristas });
});

// Endpoint para o Painel obter o estado atual
app.get('/api/telemetry', (req, res) => {
  res.json({
    sucesso: true,
    estado,
    eventosRecentes: historicoEventos.slice(0, 10)
  });
});

// Endpoint para a Central enviar comando remoto
app.post('/api/command', (req, res) => {
  const { comando, origem = 'CENTRAL_WEB' } = req.body || {};

  if (!comando) {
    return res.status(400).json({ erro: 'Comando não especificado' });
  }

  const comandoItem = {
    id: 'cmd_' + Date.now(),
    comando,
    origem,
    timestamp: new Date().toISOString()
  };

  comandosPendentes.push(comandoItem);

  if (comando === 'DISPARAR_TESTE') {
    estado.estado = 'ALARME_ATIVADO';
    estado.buzzer = 1;
    estado.rele = 1;
    estado.tempo_cabeca_baixa_ms = 2500;
    estado.contadores.totalAlarmesSessao++;
    iniciarProtocoloAlertaTravado();
    registrarEvento('COMANDO_CENTRAL', 'Comando Remoto: Alerta Físico e Virtual no Celular Disparado');
    broadcast({
      tipo: 'ALERTA_FISICO_E_VIRTUAL_CELULAR',
      dispararChamadaCafe: true,
      estado
    });
  } else if (comando === 'SILENCIAR_ALARME' || comando === 'RESET') {
    encerrarProtocoloAlertaTravado();
    registrarEvento('COMANDO_CENTRAL', 'Confirmação Recebida: Alerta Desativado pelo Motorista / Central');
  }

  // Notifica todos via WebSocket imediatamente
  broadcast({
    tipo: 'COMANDO_EXECUTADO',
    comando: comandoItem,
    estado
  });

  return res.status(200).json({
    sucesso: true,
    mensagem: `Comando ${comando} registrado com sucesso.`,
    comandoItem
  });
});

// Endpoint para receber atualização de GPS real do celular do motorista
app.post('/api/gps', (req, res) => {
  const { lat, lon, precisao, velocidade, heading, origem } = req.body || {};
  if (lat !== undefined && lon !== undefined) {
    estado.gps = {
      lat: Number(lat),
      lon: Number(lon),
      precisao: Number(precisao || 5),
      velocidade: Number(velocidade || 0),
      heading: Number(heading || 0),
      origem: origem || 'CELULAR_MOTORISTA',
      atualizadoEm: new Date().toISOString()
    };
    broadcast({
      tipo: 'GPS_UPDATE',
      gps: estado.gps
    });
    return res.json({ sucesso: true, gps: estado.gps });
  }
  return res.status(400).json({ erro: 'Coordenadas inválidas' });
});

// Endpoint para o ESP32 consumir e limpar comandos pendentes
app.get('/api/commands', (req, res) => {
  const enviados = [...comandosPendentes];
  comandosPendentes = []; // Limpa após consumo
  res.json({
    sucesso: true,
    total: enviados.length,
    comandos: enviados
  });
});

// Endpoint de histórico de eventos
app.get('/api/events', (req, res) => {
  res.json({
    sucesso: true,
    total: historicoEventos.length,
    eventos: historicoEventos
  });
});

// Endpoint para limpar o histórico
app.post('/api/events/clear', (req, res) => {
  historicoEventos = [];
  registrarEvento('NORMAL', 'Histórico de eventos resetado pela Central.');
  res.json({ sucesso: true, mensagem: 'Histórico limpo' });
});

// Endpoints de saúde do servidor (compatíveis com Render e Monitoramento)
app.get(['/health', '/api/health'], (req, res) => {
  res.status(200).json({
    status: 'ONLINE',
    uptime_segundos: Math.floor(process.uptime()),
    clientes_conectados: wss.clients.size,
    esp32_conectado: estado.online,
    timestamp: new Date().toISOString()
  });
});

// ------------------------------------------------------------------------------
// GERENCIAMENTO DE WEBSOCKETS
// ------------------------------------------------------------------------------
wss.on('connection', (ws, req) => {
  const ip = req.socket.remoteAddress;
  console.log(`[WS] Novo cliente conectado: ${ip} (Total ativos: ${wss.clients.size})`);

  // Envia estado inicial imediatamente ao conectar
  ws.send(JSON.stringify({
    tipo: 'ESTADO_INICIAL',
    estado,
    eventos: historicoEventos.slice(0, 20)
  }));

  ws.on('message', (message) => {
    try {
      const parsed = JSON.parse(message);

      // Se for telemetria enviada via WebSocket direto do ESP32/cliente
      if (parsed.tipo === 'TELEMETRIA') {
        // Redireciona para o mesmo fluxo
        app.handle({
          method: 'POST',
          url: '/api/telemetry',
          body: parsed.dados || parsed
        });
      } else if (parsed.tipo === 'COMANDO') {
        // Processa comando recebido via WS
        if (parsed.comando === 'DISPARAR_TESTE') {
          estado.estado = 'ALARME_ATIVADO';
          estado.buzzer = 1;
          estado.rele = 1;
          estado.tempo_cabeca_baixa_ms = 2500;
          estado.contadores.totalAlarmesSessao++;
          iniciarProtocoloAlertaTravado();
          registrarEvento('COMANDO_CENTRAL', 'Comando Remoto (WS): Alerta Físico e Virtual Disparado');
          broadcast({
            tipo: 'ALERTA_FISICO_E_VIRTUAL_CELULAR',
            dispararChamadaCafe: true,
            estado
          });
        } else if (parsed.comando === 'SILENCIAR_ALARME' || parsed.comando === 'RESET') {
          encerrarProtocoloAlertaTravado();
          registrarEvento('COMANDO_CENTRAL', 'Confirmação Recebida (WS): Alerta Desativado pelo Motorista / Central');
        }
        broadcast({
          tipo: 'COMANDO_EXECUTADO',
          comando: parsed.comando,
          estado
        });
      } else if (parsed.tipo === 'GPS_UPDATE' && parsed.gps) {
        estado.gps = {
          lat: Number(parsed.gps.lat),
          lon: Number(parsed.gps.lon),
          precisao: Number(parsed.gps.precisao || 5),
          velocidade: Number(parsed.gps.velocidade || 0),
          heading: Number(parsed.gps.heading || 0),
          origem: 'CELULAR_MOTORISTA',
          atualizadoEm: new Date().toISOString()
        };
        broadcast({
          tipo: 'GPS_UPDATE',
          gps: estado.gps
        });
      } else if (parsed.tipo === 'STATUS_MOTORISTA') {
        const statusViagem = parsed.status_viagem || (parsed.ativo ? 'EM_VIAGEM' : 'INATIVO');
        if (!estado.motoristas) estado.motoristas = {};
        if (!estado.motoristas.matheus_garcia) {
          estado.motoristas.matheus_garcia = {
            nome: 'Matheus Garcia',
            status: 'EM_VIAGEM',
            veiculo: 'Scania R-450 (Placa INO-2026)',
            rota: 'Rodovia BR-116 KM 284 · Régis Bittencourt'
          };
        }
        estado.motoristas.matheus_garcia.status = statusViagem;
        estado.motoristas.matheus_garcia.atualizadoEm = new Date().toISOString();
        registrarEvento('TURNO_MOTORISTA', `Condutor Matheus Garcia: Turno ${statusViagem === 'EM_VIAGEM' ? 'Iniciado' : 'Pausado'} (WS)`);
        broadcast({
          tipo: 'STATUS_MOTORISTA',
          motorista: 'Matheus Garcia',
          status_viagem: statusViagem,
          estado
        });
        persistirEmDisco();
      }
    } catch (err) {
      console.error('[WS PARSE ERROR]:', err.message);
    }
  });

  ws.on('close', () => {
    console.log(`[WS] Cliente desconectado (Ativos restantes: ${wss.clients.size})`);
  });

  ws.on('error', (err) => {
    console.error('[WS ERROR]:', err.message);
  });
});

// ------------------------------------------------------------------------------
// PONTE AUTOMÁTICA USB SERIAL (WINDOWS COM PORT -> PAINEL EM TEMPO REAL + NUVEM)
// ------------------------------------------------------------------------------
const { spawn } = require('child_process');
let serialBridgeProc = null;

function iniciarPonteSerialUSB() {
  if (process.platform !== 'win32' || IS_RENDER_CLOUD) return;

  const psScript = `
    $ErrorActionPreference = 'SilentlyContinue'
    while ($true) {
      $ports = [System.IO.Ports.SerialPort]::getportnames()
      $target = if ($ports -contains 'COM5') { 'COM5' } else { $ports | Select-Object -First 1 }
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
      if (!line) continue;
      if (line.startsWith('SERIAL_CONNECTED:')) {
        const portName = line.split(':')[1];
        console.log(`[USB SERIAL] Conectado automaticamente à ESP32 na porta ${portName} (115200 bps)`);
        registrarEvento('NORMAL', `ESP32 conectada via USB Serial (${portName})`, 'Leitura em tempo real ativa.');
      } else if (line.startsWith('TELEMETRY:')) {
        try {
          const jsonStr = line.substring('TELEMETRY:'.length).trim();
          const dados = JSON.parse(jsonStr);
          processarTelemetria(dados);
        } catch (e) { }
      }
    }
  });

  serialBridgeProc.on('exit', () => {
    setTimeout(iniciarPonteSerialUSB, 3000);
  });
}

// Inicia escuta na porta e host configurados (0.0.0.0 é obrigatório para Render e Docker)

// ------------------------------------------------------------------------------
// RECEPTOR UDP BROADCAST LOCAL (ZERO CONFIGURAÇÃO DE IP / LATÊNCIA < 1MS)
// ------------------------------------------------------------------------------
const dgram = require('dgram');
const udpServer = dgram.createSocket('udp4');
const UDP_PORT = 3002;

udpServer.on('message', (msg, rinfo) => {
  try {
    const raw = msg.toString('utf8').trim();
    if (raw.startsWith('{')) {
      const dados = JSON.parse(raw);
      processarTelemetria(dados);
    }
  } catch (e) {}
});

udpServer.on('error', (err) => {
  console.log('[UDP ERROR]:', err.message);
});

try {
  udpServer.bind(UDP_PORT, '0.0.0.0', () => {
    console.log(`  ✔ Receptor UDP Broadcast ativo na porta: ${UDP_PORT} (Zero Latência)`);
  });
} catch (e) {
  console.log('[UDP BIND ERROR]:', e.message);
}

server.listen(PORT, '0.0.0.0', () => {
  console.log(`
  ===================================================================
    INOVA TECH SMART DRIVER — BACKEND ATIVO
  ===================================================================
  ✔ Servidor HTTP & WebSocket iniciado na porta: ${PORT}
  ✔ Endpoint Local / Cloud: http://0.0.0.0:${PORT}
  ✔ WebSocket URL:         ws://0.0.0.0:${PORT}/ws
  ✔ REST Telemetria (POST): http://0.0.0.0:${PORT}/api/telemetry
  ✔ REST Comandos (POST):   http://0.0.0.0:${PORT}/api/command
  ✔ Health Check:          http://0.0.0.0:${PORT}/health
  ===================================================================
  `);
  // Ponte serial desativada para a porta COM5 ficar 100% livre para o PlatformIO gravar
  // iniciarPonteSerialUSB();
});

// Encerramento gracioso para orquestradores de nuvem (Render, Docker, Kubernetes)
function encerrarGraciosamente(sinal) {
  console.log(`[SERVER] Sinal ${sinal} recebido. Encerrando conexões graciosamente...`);
  if (serialBridgeProc) {
    try { serialBridgeProc.kill(); } catch (e) { }
  }
  wss.clients.forEach(client => {
    try { client.close(1001, 'Servidor reiniciando'); } catch (e) { }
  });
  server.close(() => {
    console.log('[SERVER] Servidor HTTP e WebSocket finalizados com sucesso.');
    process.exit(0);
  });
}

process.on('SIGTERM', () => encerrarGraciosamente('SIGTERM'));
process.on('SIGINT', () => encerrarGraciosamente('SIGINT'));
