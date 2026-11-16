# Pinagem completa — Smart Driver (ESP32 DevKit 30 pinos) — 25/10/2026, atualizada em 11/11/2026

Conferida com o firmware (`Smart Driver/src/main.cpp`). ESP32 vista de cima, com o **USB para baixo**.

## Por componente

| Componente | Pino do componente | Pino da ESP32 | Observação |
|---|---|---|---|
| **Acelerômetro GY-521** | VCC | **3V3** | `MPU_VCC_NO_3V3 = true` |
| | GND | **RX2** (GPIO 16) | GND virtual |
| | SDA | **D21** | I²C 1 |
| | SCL | **D22** | I²C 1 |
| ~~Display OLED 0,96"~~ | — | **removido em 11/11/2026** | D23, D15, D5 e TX2 ficaram livres, mas o firmware ainda deixa o D23 em HIGH e o D15 em LOW: não ligar o LED neles |
| **LED RGB** (cátodo comum) — remontado em 16/11 (pernas de frente: R, comum, G, B; fio no LED → fio até a ESP) | R — roxo → **roxo** | **D26** | sem resistor desde 11/11 |
| | comum (perna mais longa) — preto → **azul** | **D14** | GND virtual |
| | G — cinza → **cinza** | **D4** | sem resistor desde 11/11 |
| | B — branco → **branco** | **D27** | sem resistor desde 11/11 |
| **Buzzer** | + | **D25** | |
| | − | **D33** | GND virtual (desde 25/10) |
| **Relé duplo HL-52S** | VCC | **VIN** (5 V) | jumper JD-VCC encaixado |
| | GND | **GND físico** | as bobinas puxam ~70 mA cada: no D13 as "molas" paravam |
| | IN1 (Safe-Stop) | **D18** | |
| | IN2 (Pisca-alerta) | **D19** | |
| Sensor IR | — | **removido** | se voltar: OUT no D32, VCC/GND em pinos virtuais |

## Pelos dois lados da placa

| Lado esquerdo (de cima para baixo) | Uso | Lado direito (de cima para baixo) | Uso |
|---|---|---|---|
| EN | botão de reset | D23 | livre (era OLED VDD; firmware ainda em HIGH) |
| VP | livre (só entrada) | D22 | acelerômetro SCL |
| VN | livre (só entrada) | TX0 | USB (não usar) |
| D34 | livre (só entrada) | RX0 | USB (não usar) |
| D35 | livre (só entrada) | D21 | acelerômetro SDA |
| D32 | livre (era o IR) | D19 | relé IN2 |
| D33 | buzzer − (GND virtual) | D18 | relé IN1 |
| D25 | buzzer + | D5 | livre (era OLED SCK) |
| D26 | LED R (fio roxo) | TX2 | livre (era OLED SDA) |
| D27 | LED B (fio branco) | RX2 | acelerômetro GND |
| D14 | LED comum (fio azul) | D4 | LED G (fio cinza) |
| D12 | ⛔ não usar (placa não liga) | D2 | LED azul da placa |
| D13 | livre (era o GND do relé) | D15 | livre (era OLED GND; firmware ainda em LOW) |
| GND | relé GND | GND | ocupado |
| VIN | relé VCC (5 V) | 3V3 | acelerômetro VCC |

## Desenhos (11/11/2026)

- LED RGB: https://claude.ai/artifact/Kvy3tgDUSrpNG2rtc1uZ2Q
- Acelerômetro GY-521: https://claude.ai/artifact/6pnWL8kL9QPPNSudxKtsXT
