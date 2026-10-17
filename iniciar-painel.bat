@echo off
chcp 65001 > nul
title INOVATECH SMART DRIVER - CENTRAL DE OPERAÇÕES
cls

echo =====================================================================
echo       INOVA TECH — SMART DRIVER (INICIALIZADOR DA CENTRAL TCC)
echo =====================================================================
echo.
echo [1/3] Identificando endereço de IP local na rede...
for /f "tokens=2 delims=:" %%a in ('ipconfig ^| findstr /i "IPv4" ^| findstr /v "127.0.0.1"') do (
    set IP_LOCAL=%%a
    goto :ip_encontrado
)
:ip_encontrado
set IP_LOCAL=%IP_LOCAL: =%
echo       IP do Computador da Sala: http://%IP_LOCAL%:3001
echo.
echo [2/3] Iniciando Servidor Backend Node.js + WebSockets...
echo       (Pressione Ctrl+C para encerrar)
echo.

cd /d "%~dp0backend"
start http://localhost:3001
node server.js

pause
