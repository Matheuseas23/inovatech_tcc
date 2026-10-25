# Pinagem completa — Smart Driver (ESP32 DevKit 30 pinos) — 25/10/2026

Conferida com o firmware (`Smart Driver/src/main.cpp`). ESP32 vista de cima, com o **USB para baixo**.

## Por componente

| Componente | Pino do componente | Pino da ESP32 | Observação |
|---|---|---|---|
| **Acelerômetro GY-521** | VCC | **3V3** | `MPU_VCC_NO_3V3 = true` |
| | GND | **RX2** (GPIO 16) | GND virtual |
| | SDA | **D21** | I²C 1 |
| | SCL | **D22** | I²C 1 |
| **Display OLED 0,96"** | VDD | **D23** | VCC virtual |
| | GND | **D15** | GND virtual |
| | SCK | **D5** | I²C 2 (barramento próprio) |
| | SDA | **TX2** (GPIO 17) | I²C 2 |
| **LED RGB** (cátodo comum) | R — fio laranja | **D26** | resistor 220 Ω |
| | G — fio verde | **D4** | resistor 100 Ω |
| | B — fio roxo | **D27** | resistor 100 Ω |
| | comum (perna mais longa) — fio cinza | **D14** | GND virtual |
| **Buzzer** | + | **D25** | |
| | − | **GND** | |
| **Relé duplo HL-52S** | VCC | **VIN** (5 V) | jumper JD-VCC encaixado |
| | GND | **D13** | GND virtual |
| | IN1 (Safe-Stop) | **D18** | |
| | IN2 (Pisca-alerta) | **D19** | |
| Sensor IR | — | **removido** | se voltar: OUT no D32, VCC/GND em pinos virtuais |

## Pelos dois lados da placa

| Lado esquerdo (de cima para baixo) | Uso | Lado direito (de cima para baixo) | Uso |
|---|---|---|---|
| EN | botão de reset | D23 | OLED VDD |
| VP | livre (só entrada) | D22 | acelerômetro SCL |
| VN | livre (só entrada) | TX0 | USB (não usar) |
| D34 | livre (só entrada) | RX0 | USB (não usar) |
| D35 | livre (só entrada) | D21 | acelerômetro SDA |
| D32 | livre (era o IR) | D19 | relé IN2 |
| D33 | livre | D18 | relé IN1 |
| D25 | buzzer + | D5 | OLED SCK |
| D26 | LED R (laranja) | TX2 | OLED SDA |
| D27 | LED B (roxo) | RX2 | acelerômetro GND |
| D14 | LED comum (cinza) | D4 | LED G (verde) |
| D12 | ⛔ não usar (placa não liga) | D2 | LED azul da placa |
| D13 | relé GND | D15 | OLED GND |
| GND | (buzzer −) | GND | ocupado |
| VIN | relé VCC (5 V) | 3V3 | acelerômetro VCC |
