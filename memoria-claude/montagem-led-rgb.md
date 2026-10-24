# Montagem do LED RGB nos óculos — cores dos fios

LED RGB de 4 pernas (segurando com as pernas para baixo, a mais comprida é a 2ª). Anotado conforme o Matheus monta.

| Perna do LED | Cor do fio | Vai no pino da ESP32 | Resistor |
|---|---|---|---|
| 1 — vermelho (R) | **laranja** | D26 | 220 Ω |
| 2 — **comum** (a mais comprida) | **cinza** | D14 (GND virtual, se for cátodo comum) | — |
| 3 — verde (G) | **verde** | **D4** (o D33 está ocupado pelo sensor IR) | 100 Ω |
| 4 — azul (B) | **roxo** | D27 | 100 Ω |

**Tipo do LED:** _(a confirmar com a bateria de moeda: cátodo comum ou ânodo comum)_
