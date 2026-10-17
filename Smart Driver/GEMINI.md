# INOVA TECH SMART DRIVER — CONTEXTO & REGRAS DO PROJETO

Este arquivo é lido automaticamente pelo assistente de IA em qualquer computador ou dispositivo que abra esta pasta sincronizada pelo OneDrive.

## 1. Identificação do Projeto
- **Projeto:** Inova Tech Smart Driver (TCC)
- **Autor:** Matheus Garcia (UNASP / IAE)
- **Dispositivo:** ESP32 Dev Module (`esp32dev`, framework `arduino`, chip USB-UART CP2102)
- **IDE / Toolchain:** PlatformIO / Arduino IDE
- **Último Registro de Sessão:** 15 de Outubro de 2026 às 11:50

## 2. Mapa de Hardware & Ligações (Teste de Bancada)
- **Sensor de Obstáculo Infravermelho (Deteção de Cabeça Baixa / Queda de Sono):**
  - Fixado na armação dos óculos e apontado para baixo (em direção ao peito/tronco).
  - `VCC` -> `3V3` da ESP32 (3,3V obrigatório para proteger os GPIOs)
  - `GND` -> `GND`
  - `OUT` -> `D32` (GPIO 32, digital input com pull-up, nível `LOW` ao detetar cabeça baixa / aproximação do peito)
- **Buzzer Ativo (Alarme Sonoro Local):**
  - Positivo (`+`) -> `D25` (GPIO 25, digital output)
  - Negativo (`-`) -> `GND`
- **Módulo de Relé de 2 Canais (Alarme Externo / Camionagem):**
  - `VCC` -> `VIN` ou `5V` da ESP32
  - `GND` -> `GND`
  - `IN1` (Canal 1) -> `D18` (GPIO 18)
  - `IN2` (Canal 2) -> Reserva

## 3. Diretrizes de Código & Operação
- **Temporização Não-Bloqueante:** Todo o código utiliza `millis()` (proibido o uso de `delay()`).
- **Filtro de Cabeça Baixa:** Disparo de alarme apenas após 2500 ms (2,5 segundos) contínuos de cabeça baixa, prevenindo falsos alarmes ao consultar rapidamente o painel ou espelhos.
- **Corte Imediato:** O retorno da cabeça para a posição normal (erguida) desliga imediatamente o alarme sonoro e zera a contagem.
- **Porta Serial:** 115200 baud, mensagens por mudança de estado (`Aviso: Cabeca Baixa Detectada`, `ALARME ATIVADO`, `Postura Normalizada`).
- **Documentação Detalhada & Checkpoint:** Consulte o arquivo `MEMORIA_DO_PROJETO.md` nesta mesma pasta.

## 4. Próxima Etapa no Novo Dispositivo
1. Conectar a placa ESP32 via USB (garantir driver CP2102 se necessário, guardado em `INOVATECH/drivers/`).
2. Fazer o Upload do firmware no PlatformIO.
3. Abrir o Monitor Serial (115200 baud) e testar a lógica dos 2,5 segundos com o sensor e buzzer na bancada.
