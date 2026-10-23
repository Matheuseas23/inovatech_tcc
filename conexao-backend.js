/**
 * ==============================================================================
 * INOVA TECH — SMART DRIVER · LINK COM O SERVIDOR (Central Tech + App do Motorista)
 * ==============================================================================
 * Descobre sozinho qual servidor usar, nesta ordem de prioridade:
 *   1. ?backend=URL na barra de endereço, ou a URL salva no campo da Central
 *   2. O próprio endereço da página (ex.: http://192.168.0.10:3001/motorista.html)
 *   3. http://localhost:3001 (PC da apresentação)
 *   4. Nuvem Render (https://inovatech-tcc.onrender.com)
 * Conecta por WebSocket, cai para REST (polling) se o WebSocket for bloqueado
 * pelo firewall e reconecta sozinho quando a rede volta.
 *
 * Também traz um simulador local para quando não existe servidor nenhum
 * (as abas do mesmo navegador ficam sincronizadas via BroadcastChannel).
 * ==============================================================================
 */
(function (global) {
	var NUVEM_URL = 'https://inovatech-tcc.onrender.com';
	var CHAVE_URL_SALVA = 'inovatech_backend_url';
	var ESCALONAMENTO_PADRAO = { critico_ms: 9000, acidente_ms: 15000 };

	function normalizarUrl(url) {
		return String(url || '').trim().replace(/\/+$/, '');
	}

	function listarCandidatos() {
		var lista = [];
		function adicionar(url) {
			url = normalizarUrl(url);
			if (!/^https?:\/\//i.test(url)) return;
			// Página HTTPS (Vercel) não pode abrir conexão HTTP (bloqueio de conteúdo misto)
			if (global.location.protocol === 'https:' && /^http:/i.test(url)) return;
			if (lista.indexOf(url) === -1) lista.push(url);
		}
		try { adicionar(new URLSearchParams(global.location.search).get('backend')); } catch (e) { }
		try { adicionar(global.localStorage.getItem(CHAVE_URL_SALVA)); } catch (e) { }
		if (/^https?:$/.test(global.location.protocol)) adicionar(global.location.origin);
		adicionar('http://localhost:3001');
		adicionar(NUVEM_URL);
		return lista;
	}

	function testarServidor(base, timeoutMs) {
		return new Promise(function (resolve) {
			var controle = ('AbortController' in global) ? new AbortController() : null;
			var timer = setTimeout(function () {
				if (controle) controle.abort();
				resolve(false);
			}, timeoutMs);
			fetch(base + '/api/health', { cache: 'no-store', signal: controle ? controle.signal : undefined })
				.then(function (r) { return r.json(); })
				.then(function (j) { clearTimeout(timer); resolve(!!(j && j.status === 'ONLINE')); })
				.catch(function () { clearTimeout(timer); resolve(false); });
		});
	}

	// Testa todos em paralelo, mas respeita a ordem de prioridade da lista
	function descobrirServidor() {
		var candidatos = listarCandidatos();
		var testes = candidatos.map(function (url) {
			return testarServidor(url, url === NUVEM_URL ? 15000 : 2500);
		});
		var i = 0;
		function proximo() {
			if (i >= candidatos.length) return Promise.resolve(null);
			var indice = i++;
			return testes[indice].then(function (ok) { return ok ? candidatos[indice] : proximo(); });
		}
		return proximo();
	}

	/**
	 * opcoes.origem         -> nome de quem envia comandos ('CENTRAL_TECH', 'APP_MOTORISTA')
	 * opcoes.aoReceber(msg) -> toda mensagem do servidor ({ tipo, estado, evento, ... })
	 * opcoes.aoMudarStatus(status, base) -> 'PROCURANDO' | 'WS' | 'REST' | 'OFFLINE'
	 */
	function criarLink(opcoes) {
		var base = null;
		var ws = null;
		var wsAberto = false;
		var pollTimer = null;
		var reconTimer = null;
		var ultimoContatoMs = 0;
		var statusAtual = '';

		function avisarStatus(s) {
			if (s === statusAtual) return;
			statusAtual = s;
			if (opcoes.aoMudarStatus) opcoes.aoMudarStatus(s, base);
		}

		function entregar(msg) {
			ultimoContatoMs = Date.now();
			if (opcoes.aoReceber) {
				try { opcoes.aoReceber(msg); } catch (e) { console.error('[LINK] Erro ao processar mensagem:', e); }
			}
		}

		function iniciar() {
			clearTimeout(reconTimer);
			avisarStatus('PROCURANDO');
			descobrirServidor().then(function (encontrado) {
				if (!encontrado) {
					base = null;
					avisarStatus('OFFLINE');
					agendarReconexao(5000);
					return;
				}
				base = encontrado;
				abrirWebSocket();
			});
		}

		function abrirWebSocket() {
			var wsUrl = base.replace(/^http/i, 'ws') + '/ws';
			var abriu = false;
			try {
				ws = new WebSocket(wsUrl);
			} catch (e) {
				iniciarPolling();
				agendarReconexao(5000);
				return;
			}
			ws.onopen = function () {
				abriu = true;
				wsAberto = true;
				ultimoContatoMs = Date.now();
				pararPolling();
				avisarStatus('WS');
			};
			ws.onmessage = function (ev) {
				var msg;
				try { msg = JSON.parse(ev.data); } catch (e) { return; }
				entregar(msg);
			};
			ws.onclose = function () {
				wsAberto = false;
				ws = null;
				iniciarPolling();
				agendarReconexao(abriu ? 1500 : 5000);
			};
			ws.onerror = function () { /* o onclose cuida da reconexão */ };
		}

		function iniciarPolling() {
			if (pollTimer || !base) return;
			pollTimer = setInterval(function () {
				fetch(base + '/api/telemetry', { cache: 'no-store' })
					.then(function (r) { return r.json(); })
					.then(function (d) {
						if (d && d.estado) {
							entregar({ tipo: 'TELEMETRIA_UPDATE', estado: d.estado, via: 'REST' });
							if (!wsAberto) avisarStatus('REST');
						}
					})
					.catch(function () {
						if (Date.now() - ultimoContatoMs > 6000) avisarStatus('OFFLINE');
					});
			}, 700);
		}

		function pararPolling() {
			if (pollTimer) clearInterval(pollTimer);
			pollTimer = null;
		}

		function agendarReconexao(ms) {
			clearTimeout(reconTimer);
			reconTimer = setTimeout(function () {
				if (wsAberto) return;
				// Se nem o REST responde, procura outro servidor (ex.: PC desligou -> nuvem)
				if (!base || Date.now() - ultimoContatoMs > 8000) {
					pararPolling();
					iniciar();
				} else {
					abrirWebSocket();
				}
			}, ms);
		}

		function postar(caminho, corpo) {
			if (!base) return Promise.resolve(false);
			return fetch(base + caminho, {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify(corpo)
			}).then(function (r) { return r.ok; }).catch(function () { return false; });
		}

		// Envia por WebSocket quando aberto; senão usa a rota REST equivalente (nunca os dois)
		function enviar(msg) {
			if (ws && ws.readyState === 1) {
				try {
					ws.send(JSON.stringify(msg));
					return Promise.resolve(true);
				} catch (e) { }
			}
			if (msg.tipo === 'COMANDO') {
				var corpo = { comando: msg.comando, origem: msg.origem };
				var extras = msg.extras || {};
				for (var k in extras) corpo[k] = extras[k];
				return postar('/api/command', corpo);
			}
			if (msg.tipo === 'STATUS_MOTORISTA') return postar('/api/driver/shift', msg);
			if (msg.tipo === 'GPS_UPDATE') return postar('/api/gps', { gps: msg.gps, origem: 'CELULAR_MOTORISTA' });
			return postar('/api/telemetry', msg);
		}

		function comando(nome, extras) {
			return enviar({ tipo: 'COMANDO', comando: nome, origem: opcoes.origem || 'WEB', extras: extras || {} });
		}

		function trocarServidor(url) {
			try {
				if (normalizarUrl(url)) global.localStorage.setItem(CHAVE_URL_SALVA, normalizarUrl(url));
				else global.localStorage.removeItem(CHAVE_URL_SALVA);
			} catch (e) { }
			if (ws) { try { ws.onclose = null; ws.close(); } catch (e) { } }
			ws = null;
			wsAberto = false;
			pararPolling();
			iniciar();
		}

		return {
			iniciar: iniciar,
			enviar: enviar,
			comando: comando,
			trocarServidor: trocarServidor,
			base: function () { return base; },
			conectado: function () { return !!base && (wsAberto || Date.now() - ultimoContatoMs < 6000); }
		};
	}

	/**
	 * Simulador local (sem servidor): mesma linha do tempo do servidor real.
	 * Pré-alerta até o limiar -> alarme travado -> crítico aos 9s -> SOS aos 15s.
	 */
	function criarSimuladorLocal(aoEstado) {
		var canal = null;
		var timer = null;
		var inicioMs = 0;
		var duracaoMs = 1500;

		function estadoBase() {
			return {
				online: false,
				servidor: 'SIMULADOR_LOCAL',
				sensor_ir: 1,
				tempo_cabeca_baixa_ms: 0,
				limiar_seguranca_ms: duracaoMs,
				estado: 'NORMAL',
				buzzer: 0,
				rele: 0,
				rele_2: 0,
				alerta_travado: false,
				chamada_celular_ativa: false,
				origem_alerta: null,
				tempo_sem_resposta_ms: 0,
				nivel_escalonamento: 0,
				tempo_recuperacao_ms: 0,
				recuperacao_limite_ms: 3000,
				escalonamento: ESCALONAMENTO_PADRAO,
				simulacao_ativa: false,
				acelerometro: { pitch: 2, roll: 0, inclinacao: 2, pescada_detectada: false, online: false, status: 'SIMULAÇÃO LOCAL' }
			};
		}

		function emitir(est) {
			aoEstado(est);
			if (canal) canal.postMessage({ tipo: 'estado', estado: est });
		}

		function tick() {
			var t = Date.now() - inicioMs;
			var est = estadoBase();
			est.sensor_ir = 0;
			est.origem_alerta = 'SIMULACAO';
			est.acelerometro = { pitch: -36, roll: -2, inclinacao: 36, pescada_detectada: false, online: false, status: 'SIMULAÇÃO LOCAL' };
			if (t < duracaoMs) {
				est.estado = 'PRE_ALERTA';
				est.simulacao_ativa = true;
				est.tempo_cabeca_baixa_ms = t;
			} else {
				var semResposta = t - duracaoMs;
				est.estado = 'ALARME_ATIVADO';
				est.tempo_cabeca_baixa_ms = duracaoMs;
				est.alerta_travado = true;
				est.chamada_celular_ativa = true;
				est.tempo_sem_resposta_ms = semResposta;
				est.nivel_escalonamento = semResposta >= ESCALONAMENTO_PADRAO.acidente_ms ? 3 : (semResposta >= ESCALONAMENTO_PADRAO.critico_ms ? 2 : 1);
				est.buzzer = 1;
				est.rele = 1;
				est.rele_2 = 1;
			}
			emitir(est);
		}

		function pararTimer() {
			if (timer) clearInterval(timer);
			timer = null;
		}

		try {
			canal = new BroadcastChannel('inovatech_sim_local');
			canal.onmessage = function (ev) {
				if (!ev.data) return;
				if (ev.data.tipo === 'parar') pararTimer();
				if (ev.data.tipo === 'iniciar') pararTimer();
				if (ev.data.tipo === 'estado') aoEstado(ev.data.estado);
			};
		} catch (e) { }

		return {
			iniciar: function (ms) {
				pararTimer();
				duracaoMs = ms || 1500;
				inicioMs = Date.now();
				if (canal) canal.postMessage({ tipo: 'iniciar' });
				timer = setInterval(tick, 100);
				tick();
			},
			parar: function () {
				pararTimer();
				if (canal) canal.postMessage({ tipo: 'parar' });
				emitir(estadoBase());
			}
		};
	}

	global.SmartDriverLink = {
		NUVEM_URL: NUVEM_URL,
		criarLink: criarLink,
		criarSimuladorLocal: criarSimuladorLocal
	};
})(window);
