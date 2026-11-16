# Histórico das sessões com o Claude Code

## Sessão 1 — 23/10/2026, 00:55 → 01:38

**Pedido:** continuar o projeto iniciado no Google Antigravity, corrigir erros e melhorar; prompt de especificação em [prompt-especificacao-2026-10-23.md](prompt-especificacao-2026-10-23.md).

**Feito** (commit `637ee7e` — "fix: estabiliza comunicação entre ESP32, servidor, central e app do motorista"):
- ESP32 virou a fonte única do estado postural (`NORMAL` / `PRE_ALERTA` / `ALARME_ATIVADO`, 3,5 s com 3 bips). O `server.js` deixou de ter cronômetro próprio de 1,5 s.
- Servidor cuida só do alerta travado: Crítico em 9 s, Possível Acidente / SOS em 15 s, recuperação automática com cabeça erguida por 3 s.
- Protocolo UDP 3002 com resposta `{"srv":"INOVATECH","cmds":[...]}` — a ESP32 aprende o IP do PC sozinha e os comandos da Central finalmente chegam à placa. Fallback para o Render após 5 s sem servidor local.
- `conexao-backend.js` criado e compartilhado por `central.html` e `motorista.html` (WebSocket + REST + reconexão + simulação local).
- Tempos lidos do servidor; nenhum número fixo no HTML. Simulação rápida = 1,5 s.
- GY-521 calibrado ao ligar e pelo botão "Recalibrar"; detecção de "pescada".
- Firmware compila (Arduino-ESP32 3.3.12, Flash ~92%).

**Pendente:**
- O prompt chegou cortado no Nível 3 do SOS — o restante (incluindo a descrição do `motorista.html`) nunca foi recebido.
- Itens do prompt ainda não conferidos: regra da logo do `index.html` (Home sem logo no header) e modal seletor `#portalAccessModal`.
- Testar com a ESP32 física os comandos da Central (Testar Alarme, Silenciar, Recalibrar, Relés).
- Decisão do Matheus: os áudios se chamam `alerta-critico-10s.mp3` / `alerta-acidente-20s.mp3`, mas o servidor usa 9 s / 15 s — se as vozes falam "10" e "20 segundos", ajustar a constante `ESCALONAMENTO` no `server.js`.
- Decisão do Matheus: tirar `backend/data/*.json` do git (mudam a cada uso).

## Sessão 2 — 23/10/2026, 01:46

- Recuperado o prompt da sessão 1 a partir do histórico local do Claude Code (`C:\Users\NATAL\.claude\projects\...\*.jsonl`).
- Criada esta pasta `memoria-claude/` e a memória automática do Claude passou a apontar para ela.
- Conversas exportadas em `conversas/` (texto das mensagens, sem senhas) para leitura também pelo celular.
- Nenhuma alteração de código no começo da sessão (depois veio a madrugada do CPIC, abaixo).
- Configurado o **Remote Control** (usar o Claude pelo app do celular, aba Code → `inovatech_tcc`), modo `same-dir`. Para ligar de novo, num terminal do Antigravity:
  ```powershell
  cd "C:\Users\NATAL\OneDrive - Instituição Adventista de Ensino\Projetos 2026\INOVATECH\INOVATECH TCC"
  & "c:\Users\NATAL\.antigravity-ide\extensions\anthropic.claude-code-2.1.306-win32-x64\resources\native-binary\claude.exe" rc
  ```
  (o número da versão no caminho muda quando a extensão atualiza; o terminal precisa ficar aberto; Ctrl+C desliga.)

## Sessão 2 (continuação) — madrugada de 23/10/2026 até a apresentação do CPIC ✅

**Resultado:** apresentação feita, tudo funcionou.

**Firmware (ESP32):**
- Sem cabo/power bank: Wi-Fi sem economia de energia, LED azul de status (fixo = PC, pisca devagar = nuvem, rápido = sem rede).
- Sensor IR virou reserva do acelerômetro (`ir_confiavel`); estava sem alimentação e segurava o alarme ligado.
- Calibração guiada por bipes (1 bip → olhar para frente → 2 bips), histerese 32°/25°, pitch/roll relativos à postura calibrada.
- Tolerância 1,5 s: bip rápido em 1 s + "tec" do Relé 2; contínuo em 1,5 s + "tec" do Relé 1; "tec" ao normalizar.
- Giroscópio: pegar objeto (cabeça se mexendo) não dispara; sono = cabeça baixa e parada.
- [TESTE] Sensores só com monitoramento ativado no app (`"mon":1`), com música ao ativar e despertador ao desativar; à prova de falha (15 s sem servidor = sensores ligam).
- Wi-Fi somente Redmi (`SOMENTE_WIFI_REDMI`), reconexão com 15 s, motivo das quedas no log, comandos seriais `SCAN` e `DIAG:1/0`. Buzzer com corrente máxima.

**Servidor / páginas:**
- Nuvem repassa telemetria e comandos da ESP para o PC quando a placa só alcança o Render.
- Central: sirene durante o alerta (som liberado no 1º clique), card do motorista só abre com viagem ativa no app.
- App: cards mais transparentes, ícone único no botão de turno, vozes neurais (Francisca), fontes e ícones locais (funciona sem internet).
- `iniciar-painel.bat` pede administrador e corrige o firewall (CRLF + parênteses).

**Lições:** o ponto de acesso do Redmi segue a banda do Wi-Fi do celular (escola em 5 GHz derrubava a ESP); solução no dia: Redmi conectado no iPhone de um amigo com "Maximizar Compatibilidade". A conta do notebook não é administradora → firewall bloqueado → caminho ESP → Render → PC, com quedas ocasionais.

**Pendências (de 23/10):** liberar o firewall com senha de administrador; buzzer em 5 V via transistor (S8050/2N2222) para ficar bem mais alto; ~~validar com dados a regra de "pegar objeto"~~ (feito em 24/10: parado até 23 °/s, mexendo 44–77 °/s → limite ajustado para 35 °/s); decidir se as funções de teste ficam ("tira a função dos sensores desligados", "tirar wifi redmi").

## Sessão 3 — 24/10/2026 a 26/10/2026 (mesma conversa da sessão 2, continuada)

Conversa completa: [conversas/sessao-3-2026-10-23-a-26.md](conversas/sessao-3-2026-10-23-a-26.md).

**Hardware / firmware:**
- LED RGB na lente: autoteste ao ligar, azul aguardando o servidor, verde ao ativar e apaga devagar, amarelo no aviso, vermelho no alarme, roxo com o acelerômetro desconectado, laranja do Claude (`CLAUDE:OLA` / `CLAUDE:TCHAU`), branco↔azul no modo exibição.
- Acelerômetro: VCC no 3V3, I²C em 100 kHz, religamento automático, comando `I2C`; giroscópio validado com dados (limite de 35 °/s).
- Relés com GND físico (bobina precisa de ~70 mA); buzzer com GND virtual no D33.
- Display OLED 128×64 no meio dos óculos (2º barramento I²C, tarefa própria no Core 0): carinha "OLÁ", caminhão/ônibus ao ativar, 4 animações ao desativar, fagulha do Claude ("Entrou na ESP"), olhos do modo exibição, "TCHAU!" com efeito de TV. Reconfigurado a cada 5 s contra mau contato.
- Botão **Desligar/Ligar** no app: repouso com Wi-Fi ligado, sons inspirados na lavadora Samsung.
- Envio imediato à nuvem a cada mudança de estado (barra do app com 0,2–0,4 s de atraso).

**Páginas:** modo exibição no app, card da Central cinza com monitoramento desligado, mapas maiores, site com os papéis reais da equipe, ecossistema e manual corrigidos.

**26/10 (escola):** o ponto de acesso do Redmi foi para 5 GHz quando o celular entrou no Wi-Fi da escola e a ESP sumiu (só enxerga 2,4 GHz). `SOMENTE_WIFI_REDMI = true` de novo, a pedido ("coloca as redes de volta" desfaz). Modo exibição ligado para mostrar ao professor de robótica. Sem rede, os sensores ligam sozinhos em 15 s.

**Pendências:**
- Tabela "Investimento no protótipo" no site (óculos R$ 160; o resto veio num kit, exceto a ESP32 — faltam os preços).
- **Câmera YPAY X7 (GOKE GK7202)** para ler o rosto no painel: resetar (8 s + desligar/ligar), instalar o app HOMEEYE, depois testar as portas (RTSP/ONVIF). Plano B: OpenIPC; alternativa: webcam do notebook.
- Preparação da banca (≈ 11/11): roteiro, perguntas prováveis, solda dos fios, caixinha de proteção.
## Sessão 4 — 05/11/2026

- `index.html`: a caixinha de acesso (`#portalAccessModal`) **não abre mais sozinha** ao entrar no site. Ela abre pelo botão **"Central / App do Motorista"** (cadeado) nos botões da Home. Ficaram só 2 opções (Central e App, com a senha); a opção "Apresentação do Projeto" e a flag `MOSTRAR_PORTAL_SELETOR` saíram. Fecha pelo X, Esc ou clique fora. `GEMINI.md` atualizado.
- **Logo:** o C e o H de "TECH" estavam grudados no próprio PNG (a haste do H cobria as pontas do C). Corrigido em `logo_inovatech.png` e `image.png` (são o mesmo arquivo, usado no site, na Central, no app e no manifest): as pontas do C foram reconstruídas, o H foi afastado 56 px, a linha neon foi estendida e o brilho foi recalculado. A imagem passou de 1024×237 para 1080×237. Versão antiga no git (antes do commit desta correção).

## Sessão 5 — 14/11/2026 a 16/11/2026 (véspera e dia da banca)

**App do motorista:**
- Modo pausa (café 15 min, refeição 60 min, descanso com tempo escolhido de 30 min a 11 h), vozes da Francisca na pausa e na retomada, fim da pausa com notificação (café/refeição) ou despertador (descanso), botão "zerar tempo de descanso" para a demonstração.
- Fases 2 e 3 no celular com voz e vibração; quadro de emergência com SAMU 192 / Bombeiros 193 / PRF 191. **Sem ligação automática** (decisão final dele).
- Pergunta "Você está bem?" depois que o alerta acaba pela postura: o microfone abre e ela pergunta 3 s depois. "Sim" = "OK. Pare para descansar, se for necessário."; "não" ou **10 s sem resposta** = o alerta volta de onde parou.
- Abertura animada (caminhão na estrada, batimento e ícone do óculos) terminando em "TOQUE PARA COMEÇAR", que libera som, vibração e notificação. Ícone do atalho voltou a ser o óculos com olho e batimento.
- Voz cortada no Android ("Aten… rista"): as vozes passaram a tocar pelo Web Audio, no mesmo mixer do toque de chamada; os sons do alerta tocam junto com a voz (pedido dele) e nenhuma frase é cortada no meio.
- Vibração: com o app em segundo plano, quem vibra é a notificação do alerta (sw.js). O Chrome não vibra no modo silencioso.

**Central:** textos das fases mais curtos, sem vozes (só a sirene), pausa no card da frota, "Baixar Relatório (.JSON)" com o registro de eventos.

**ESP32:**
- Relés com "tec" duplo em todas as funções, cores novas dos fios do LED (pinagem-completa.md), LED ciano piscando devagar no modo exibição.
- **Registro offline** (sugestão do professor): sem conexão, a placa grava até 30 eventos (alarme, pescada, fim do alarme, acelerômetro, modo offline) e, ao voltar online, eles aparecem na Central com a hora real e o prefixo [OFFLINE]. Testado com a ESP de verdade em 16/11.
- Em 16/11 o acelerômetro chegou a cair e voltar a cada 2 a 5 s ("energia fraca"): conferir os fios (D23, GND, D21, D22) se repetir.

**Testes automáticos** em `I:\ferramentas-claude` (servidor, app no Chrome, vozes, registro offline): todos passando em 16/11. O professor aprovou o projeto.

## Sessão 6 — 16/11/2026 (depois da revisão final, esperando a banca)

- **Site, Ecossistema de segurança:** a lista de componentes aparecia como texto solto porque as classes `tech-item-card`, `ecosystem-grid-*` etc. não tinham CSS nenhum. Ganhou cards de vidro no padrão Dark-Tech (4 colunas no físico, 5 no software, 1 coluna no celular, modo claro em vidro branco) e o roxo dos Softwares virou ciano. A foto do final virou `oculos_prototipo.jpg` (foto nova enviada pelo Matheus, 75 KB) num card grande com o texto ao lado. Conferido no Edge (desktop 1440, celular 390 e modo claro), sem erros de JS. `GEMINI.md` atualizado.
- O card "Display OLED 0,96"" saiu (o OLED não faz mais parte do projeto, confirmado pelo Matheus) e os outros foram renumerados (7 componentes; o último ocupa 2 espaços na grade). O texto do Manual de operações também deixou de citar o display OLED. "Fios Jumper & Resistores" ficou como estava.
