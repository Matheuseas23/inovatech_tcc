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