@echo off
chcp 65001 > nul
title INOVATECH SMART DRIVER - CENTRAL DE OPERAÇÕES
cls

echo =====================================================================
echo       INOVA TECH — SMART DRIVER (INICIALIZADOR DA CENTRAL TCC)
echo =====================================================================
echo.
echo [1/4] Identificando endereço de IP local na rede...
set IP_LOCAL=
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "127.0.0.1"') do (
    set IP_LOCAL=%%a
    goto :ip_encontrado
)
:ip_encontrado
set IP_LOCAL=%IP_LOCAL: =%
echo       Central Tech (TV):     http://localhost:3001/central.html
echo       App do Motorista:      http://%IP_LOCAL%:3001/motorista.html
echo       (abra o endereço acima no celular, conectado na MESMA rede Wi-Fi)
echo.

echo [2/4] Liberando o firewall do Windows (portas 3001 TCP e 3002 UDP)...
netsh advfirewall firewall show rule name="InovaTech Smart Driver UDP" > nul 2>&1
if errorlevel 1 (
    netsh advfirewall firewall add rule name="InovaTech Smart Driver UDP" dir=in action=allow protocol=UDP localport=3002 > nul 2>&1
    netsh advfirewall firewall add rule name="InovaTech Smart Driver TCP" dir=in action=allow protocol=TCP localport=3001 > nul 2>&1
    if errorlevel 1 (
        echo       AVISO: sem permissão de administrador. Se a ESP32 não aparecer online,
        echo       clique com o botão direito neste arquivo e "Executar como administrador",
        echo       ou clique em "Permitir acesso" quando o Windows perguntar sobre o Node.js.
    ) else (
        echo       Regras de firewall criadas.
    )
) else (
    echo       Regras de firewall já existem.
)
echo.

echo [3/4] Verificando dependências do servidor...
cd /d "%~dp0backend"
if not exist node_modules (
    echo       Instalando dependências dentro da pasta backend (só na primeira vez)...
    call npm install --no-audit --no-fund
)
echo.

echo [4/4] Iniciando Servidor Backend Node.js + WebSockets + UDP...
echo       (Pressione Ctrl+C para encerrar)
echo.
start "" http://localhost:3001/central.html
node server.js

pause
