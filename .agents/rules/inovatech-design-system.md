---
trigger: always_on
description: Diretrizes de design e padrões do projeto InovaTech Smart Driver
---

# Regras de Design e Comportamento do Usuário — InovaTech Smart Driver

## 1. Padrão Estético e Design System
- Estilo: Dark-Tech / Cockpit Industrial de Alta Precisão (Swiss typographic precision + telemetria veicular).
- Tipografia: 'Barlow Condensed' (títulos em caixa alta com letter-spacing) e 'Space Mono' (textos técnicos, métricas, botões).
- Cores: Ciano elétrico neon (`#08deeb`), fundo abissal (`#020b12`, `#031522`), cards com transparência e glassmorphism sutil (`backdrop-filter: blur(8px)`).
- Modo Claro: Contraste calibrado (`--bg: #eef5f6`, `--card: #ffffff`, `--cyan: #087d88`).

## 2. Regras Essenciais da Home
- **Vídeo `oculos.mp4`**: Preenche todo o viewport (`position: fixed; inset: 0; object-fit: cover`).
- **Reprodução Limitada**: Toca exatamente 2 vezes e para definitivamente no final da 2ª exibição (sem tocar a 3ª vez).
- **Sem Caixas/Bordas Azuis no Hero**: `.hero` e `#home` livres de bordas azuis e sombras pesadas (`border: none !important; box-shadow: none !important`).
- **Margem do Topo**: `margin-top: 28px;` deve ser **SEMPRE mantido**. NUNCA zerar ou excluir o margin-top. Apenas o fundo dessa margem (e do topo/header) deve ser `background: transparent !important`.

## 3. Equipe e Links Oficiais
- Apenas botões estilizados do Instagram para os membros:
  - Matheus Souza: `https://www.instagram.com/m.souzavl/?utm_source=ig_web_button_share_sheet`
  - Matheus Novaes: `https://www.instagram.com/matheusoliveira__17/?utm_source=ig_web_button_share_sheet`
  - Otávio Lima: `https://www.instagram.com/_otxxvio/?utm_source=ig_web_button_share_sheet`
  - Thiago Tivium: `https://www.instagram.com/t.tivium/?utm_source=ig_web_button_share_sheet`
  - Matheus Garcia: `https://www.instagram.com/_mth.garcia/`
  - Nathan Arkhanjo: Instagram oficial

## 4. Práticas de Código
- Site 100% estático para execução perfeita no Go Live (Live Server).
- Sincronização obrigatória entre `index.html` e `style.css`.
- Não executar comandos no terminal sem necessidade.
- Respostas e comunicação sempre em Português.
