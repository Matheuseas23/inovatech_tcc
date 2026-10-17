# HISTÓRICO COMPLETO DA CONVERSA & REGISTRO DE DECISÕES TÉCNICAS

**Data:** 16 e 17 de Outubro de 2026  
**Projeto:** INOVA TECH — Smart Driver (TCC)  
**Ambiente:** VS Code + Antigravity IDE (OneDrive Sincronizado)  

---

## 1. Mensagens, Áudios e Pontos Tratados no Chat

### A. Solicitação Inicial do Usuário
- **Evolução do Escopo:** Desenvolvimento do Backend completo para integração com a placa ESP32 e painel em tempo real.
- **Regra Obrigatória do OneDrive:** Toda e qualquer dependência (`node_modules`), pacote, biblioteca ou arquivo de configuração deve ser instalado e salvo estritamente dentro da pasta do projeto no OneDrive (`OneDrive - Instituição Adventista de Ensino\Projetos 2026\INOVATECH\INOVATECH TCC\backend`), sem instalações globais no Windows/AppData.
- **Estrutura dos 6 Cards do Painel:**
  1. Status Geral do Motorista & Alerta Crítico (Normal / Pré-alerta 0-2.5s / Alarme Crítico Vermelho Neon).
  2. Câmera Wi-Fi Linklemo da Cabine 24/7 (captura da janela no PC + destaque automático ao disparar o alarme de cansaço).
  3. Telemetria do Sensor IR (GPIO 32 - HIGH / LOW, contagem de ms vs 2500ms).
  4. Telemetria do Acelerômetro I2C (GPIO 21/22 - Pitch, Roll, detecção de solavancos).
  5. Atuadores de Segurança (Buzzer GPIO 25, Relé GPIO 18) e comandos da Central (Teste e Reset).
  6. Conectividade, contadores e histórico de eventos em tempo real.

---

### B. Transcrição e Análise dos Áudios do Usuário

#### Áudio 1:
> *"Olha, eu vou te explicar aqui... Bom, a gente vai na Semana do TI, né, que é a semana que vem já... hoje é dia 16/10, é a semana que vem. Só que o nosso dia de apresentar o nosso trabalho... é por salas. Segunda é o 31A TI, terça é o 31B TI, e quarta é a gente, o 31C TI. Mas assim, a gente vai ter... cada grupo vai ter uma sala que eles vão sortear. Aí eles vão... eu não sei se é confirmado, mas acho, eu acho, tá, que o professor falou pra gente que vai fornecer, pedir pro TI da escola fornecer um Wi-Fi pra cada sala, entendeu? Aí a gente... aí na sala de aula lá que a gente vai ser sorteado tem o computador e tem uma TV lá, que mostra a tela do computador que a gente vai apresentar lá pro povo, entendeu? Quando for o dia das salas, todo mundo vai apresentar, entendeu? Isso é só pra você saber. Entendeu?"*

#### Áudio 2:
> *"Não, mas você não tá sabendo. A gente vai chegar na escola às 9 horas, aí o pessoal vai... os alunos dos outros, dos segundos e primeiros anos do TI, eles vão ir pras salas pra ver os projetos é às 1 hora da tar- às 1 hora da tarde, entendeu? Então a gente vai ter um tempo pra preparar a sala, arrumar tudo, entendeu?"*

---

### C. Discussões de Arquitetura e Decisões Tomadas

#### 1. Dúvida: "Migraremos da Vercel para o Render, né?"
- **Explicação:** A Vercel é ideal para Frontends estáticos e rápidos. O Render é voltado para servidores Node.js com WebSockets persistentes (conexão contínua com ESP32).
- **Abordagem Definida:** O site pode continuar na Vercel (`inovatech-tcc.vercel.app`), com o backend no Render (`inovatech-backend.onrender.com`), OU executado localmente.

#### 2. Dúvida: "É necessário o Render mesmo?"
- **Análise Técnica:** O Render **não é estritamente obrigatório** para o dia da apresentação na sala, porque:
  - O grupo terá 4 horas (das 9h às 13h) para preparar a sala.
  - O PC estará conectado diretamente à TV da sala.
  - Rodando localmente (`npm start` no PC), o sistema ganha **latência zero (< 5ms)** e fica **imune a quedas de internet** da escola.
- **Por que o Professor Recomendou o Render:**
  - A Vercel corta conexões WebSocket contínuas.
  - O Render é o substituto moderno do Heroku ensinado nas instituições técnicas.
  - Ter o backend na nuvem comprova para a banca que o projeto é uma solução IoT comercial escalável, permitindo que os professores abram a telemetria pelo próprio celular.
- **Estratégia Final Aprovada:** Subir para o Render para cumprir os requisitos da banca, mas manter o modo local pronto no computador da sala como plano infalível de contingência.

---

### D. Análise do Print do Render ("New Web Service")

O usuário enviou o print da tela de criação do serviço no Render. As orientações fornecidas foram:
1. **Name:** `inovatech_tcc` ou `inovatech-backend`.
2. **Language:** Mudar de `Docker` para **`Node`**.
3. **Branch:** Manter em **`main`**.
4. **Region:** Manter em **`Oregon (US West)`**.
5. **Root Directory:** Preencher com **`backend`**.
6. **Build Command:** `npm install`.
7. **Start Command:** `npm start`.
8. **Instance Type (Preço):** Rolar para baixo e marcar **`Free` ($0/mês)** em vez do plano pago de $7/mês.

---

### E. Dúvida: "No controle de código fonte não apareceu nenhuma alteração feita"
- **Causa Identificada:** O VS Code estava com o arquivo `Smart Driver/src/main.cpp` aberto, focando na pasta do ESP32 ou sofrendo com travamento temporário do OneDrive nos arquivos ocultos `.git`.
- **Solução Fornecida:**
  - Abrir a pasta `INOVATECH TCC` no menu `Arquivo > Abrir Pasta`.
  - Como alternativa prática sem terminal: acessar [github.com/Matheuseas23/inovatech_tcc](https://github.com/Matheuseas23/inovatech_tcc) e fazer upload direto dos arquivos atualizados (`index.html`, `style.css` e a pasta `backend` com `server.js` e `package.json`).

---

## 2. Status dos Arquivos no Projeto

| Arquivo | Localização | Status |
| :--- | :--- | :--- |
| `index.html` | Raiz do projeto | Atualizado com os 6 cards e senha `Inovatech266` |
| `style.css` | Raiz do projeto | Sincronizado com estilos Dark-Tech e modal de senha |
| `backend/server.js` | `backend/` | Servidor Node.js (REST + WebSocket na porta 3001) |
| `backend/package.json` | `backend/` | Dependências instaladas localmente no OneDrive |
| `backend/test-esp32-sim.js`| `backend/` | Script de teste automatizado de postura e alarmes |
| `GEMINI.md` | Raiz do projeto | Memória persistente oficial atualizada |
| `MEMORIA_DO_PROJETO.md` | Raiz do projeto | Resumo técnico e operacional completo |
| `HISTORICO_CHAT_E_DECISOES.md` | Raiz do projeto | Este arquivo contendo o histórico detalhado do chat |
