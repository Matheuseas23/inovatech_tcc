# Montagem do LED RGB nos óculos — cores dos fios

LED RGB de 4 pernas (segurando com as pernas para baixo, a mais comprida é a 2ª). Anotado conforme o Matheus monta.

| Perna do LED | Cor do fio | Vai no pino da ESP32 | Resistor |
|---|---|---|---|
| 1 — vermelho (R) | **laranja** | D26 | 220 Ω |
| 2 — **comum** (a mais comprida) | **cinza** | D14 (GND virtual, se for cátodo comum) | — |
| 3 — verde (G) | **verde** | **D4** (o D33 está ocupado pelo sensor IR) | 100 Ω |
| 4 — azul (B) | **roxo** | D27 | 100 Ω |

**Tipo do LED:** cátodo comum (firmware com `LED_RGB_ANODO_COMUM = false`; o autoteste vermelho→verde→azul funcionou).

## Mudanças de 24/10/2026 (depois do CPIC)

| Componente | Ligação atual |
|---|---|
| Acelerômetro GY-521 — VCC | **pino 3V3** (antes D23) → `MPU_VCC_NO_3V3 = true` no firmware |
| Acelerômetro GY-521 — GND | RX2 (GPIO 16) |
| Acelerômetro GY-521 — SDA / SCL | D21 / D22 |
| Buzzer (+) | **D25** (logo ao lado do fio laranja do LED) |
| Buzzer (−) | GND |
| Sensor IR | **removido** (se voltar: VCC no D23, OUT no D32) |

**Lição:** o acelerômetro "parava" porque o fio **GND estava com mau contato** — respondia dormindo, mas travava ao acordar (consumo maior). Diagnóstico pela serial: comando `I2C`.
