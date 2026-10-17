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
  limiar_seguranca_ms: 2500,
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

// Rota raiz informativa
app.get('/', (req, res) => {
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

// Endpoint de telemetria recebida do ESP32 ou simulador
app.post('/api/telemetry', (req, res) => {
  const payload = req.body || {};
  const estadoAnterior = estado.estado;

  // Atualiza flags de presença
  estado.online = true;
  estado.ultimoVisto = new Date().toISOString();

  if (payload.dispositivo) estado.dispositivo = payload.dispositivo;

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

  // Lógica de cálculo ou confirmação do estado de risco
  if (payload.estado) {
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
  if (estadoAnterior !== estado.estado) {
    if (estado.estado === 'PRE_ALERTA') {
      registrarEvento('PRE_ALERTA', 'Aviso: Cabeça Baixa Detectada (Iniciando contagem de segurança)', `Tempo decorrido: ${estado.tempo_cabeca_baixa_ms}ms`);
    } else if (estado.estado === 'ALARME_ATIVADO') {
      estado.contadores.totalAlarmesSessao++;
      registrarEvento('ALARME_ATIVADO', 'PERIGO: MOTORISTA ADORMECEU COM A CABEÇA BAIXA!', `Limite de 2500ms ultrapassado. Buzzer e Relé acionados.`);
    } else if (estado.estado === 'NORMAL') {
      registrarEvento('NORMAL', 'Postura Normalizada: Cabeça Erguida', 'Alarme cessado imediatamente.');
    }
  }

  // Transmite atualização aos navegadores em tempo real via WebSocket
  broadcast({
    tipo: 'TELEMETRIA_UPDATE',
    estado,
    comandosPendentesParaESP32: comandosPendentes.length
  });

  return res.status(200).json({
    sucesso: true,
    estado_processado: estado.estado,
    comandos_pendentes: comandosPendentes
  });
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
    registrarEvento('COMANDO_CENTRAL', 'Comando Remoto: Alerta de Teste Disparado pela Central');
  } else if (comando === 'SILENCIAR_ALARME' || comando === 'RESET') {
    estado.estado = 'NORMAL';
    estado.buzzer = 0;
    estado.rele = 0;
    estado.tempo_cabeca_baixa_ms = 0;
    registrarEvento('COMANDO_CENTRAL', 'Comando Remoto: Alarme Silenciado/Resetado pela Central');
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
          registrarEvento('COMANDO_CENTRAL', 'Comando Remoto (WS): Alerta de Teste Disparado');
        } else if (parsed.comando === 'SILENCIAR_ALARME' || parsed.comando === 'RESET') {
          estado.estado = 'NORMAL';
          estado.buzzer = 0;
          estado.rele = 0;
          estado.tempo_cabeca_baixa_ms = 0;
          registrarEvento('COMANDO_CENTRAL', 'Comando Remoto (WS): Alarme Silenciado/Resetado');
        }
        broadcast({
          tipo: 'COMANDO_EXECUTADO',
          comando: parsed.comando,
          estado
        });
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

// Inicia escuta na porta e host configurados (0.0.0.0 é obrigatório para Render e Docker)
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
});

// Encerramento gracioso para orquestradores de nuvem (Render, Docker, Kubernetes)
function encerrarGraciosamente(sinal) {
  console.log(`[SERVER] Sinal ${sinal} recebido. Encerrando conexões graciosamente...`);
  wss.clients.forEach(client => {
    try { client.close(1001, 'Servidor reiniciando'); } catch (e) {}
  });
  server.close(() => {
    console.log('[SERVER] Servidor HTTP e WebSocket finalizados com sucesso.');
    process.exit(0);
  });
}

process.on('SIGTERM', () => encerrarGraciosamente('SIGTERM'));
process.on('SIGINT', () => encerrarGraciosamente('SIGINT'));
