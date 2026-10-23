@echo off
chcp 65001 > nul
title INOVATECH SMART DRIVER - CENTRAL DE OPERAÇÕES

rem Reabre como administrador (necessário para liberar o firewall). Se recusar, segue sem.
net session > nul 2>&1
if errorlevel 1 if not "%~1"=="--sem-admin" (
    powershell -NoProfile -Command "try { Start-Process -FilePath '%~f0' -Verb RunAs -ErrorAction Stop; exit 0 } catch { exit 1 }"
    if not errorlevel 1 exit /b
    call "%~f0" --sem-admin
    exit /b
)
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
net session > nul 2>&1
if errorlevel 1 (
    echo       AVISO: sem permissão de administrador. A ESP32 só chega pela nuvem (mais lento^).
    echo       Feche e abra este arquivo de novo, clicando em "Sim" no aviso do Windows.
) else (
    rem Regras "Bloquear" do Node.js (de quando clicaram em Cancelar no aviso do Windows) vencem qualquer liberação
    netsh advfirewall firewall set rule name="Node.js JavaScript Runtime" dir=in new action=allow > nul 2>&1
    netsh advfirewall firewall delete rule name="InovaTech Smart Driver UDP" > nul 2>&1
    netsh advfirewall firewall delete rule name="InovaTech Smart Driver TCP" > nul 2>&1
    netsh advfirewall firewall add rule name="InovaTech Smart Driver UDP" dir=in action=allow protocol=UDP localport=3002 profile=any > nul 2>&1
    netsh advfirewall firewall add rule name="InovaTech Smart Driver TCP" dir=in action=allow protocol=TCP localport=3001 profile=any > nul 2>&1
    echo       Firewall liberado (inclusive em rede pública, como o hotspot do celular^).
)
echo.

echo [3/4] Verificando dependências do servidor...
cd /d "%~dp0backend"
if not exist node_modules (
    echo       Instalando dependências dentro da pasta backend (só na primeira vez^)...
    call npm install --no-audit --no-fund
)
echo.

echo [4/4] Iniciando Servidor Backend Node.js + WebSockets + UDP...
echo       (Pressione Ctrl+C para encerrar)
echo.
start "" http://localhost:3001/central.html
node server.js

pause
