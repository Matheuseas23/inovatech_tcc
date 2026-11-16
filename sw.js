// Service worker mínimo do App do Motorista: só existe para o Android aceitar notificações
// (fim da pausa). Não guarda nada em cache: o app continua sempre atualizado.
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (evento) => evento.waitUntil(self.clients.claim()));

// Tocar na notificação traz o app do motorista para a frente
self.addEventListener('notificationclick', (evento) => {
  evento.notification.close();
  evento.waitUntil(self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((janelas) => {
    for (const janela of janelas) {
      if (janela.url.includes('motorista') && 'focus' in janela) return janela.focus();
    }
    return self.clients.openWindow('motorista.html');
  }));
});
