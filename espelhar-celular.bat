@echo off
chcp 65001 >nul
title INOVATECH SMART DRIVER — ESPELHAMENTO DO CELULAR (LINKLEMO)
echo ============================================================================
echo   INOVATECH SMART DRIVER — ESPELHAMENTO DO CELULAR DO MOTORISTA (LINKLEMO)
echo ============================================================================
echo.
echo   1. Conecte o celular no cabo USB (com Depuracao USB ativada)
echo      OU use o espelhamento sem fio do Windows ("Vincular ao Celular").
echo   2. Abra o aplicativo LINKLEMO no celular mostrando a camera ao vivo.
echo   3. No Painel de Controle do site, clique em:
echo      [ Capturar Tela do Celular / Linklemo ] e selecione a janela do celular!
echo.
echo   Iniciando espelhamento de alta velocidade (60 FPS)...
echo ============================================================================
cd /d "%~dp0espelhamento-celular"
scrcpy.exe --window-title "Celular do Motorista - Linklemo (InovaTech)" --stay-awake --always-on-top
pause
