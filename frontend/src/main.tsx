import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { RouterProvider } from '@tanstack/react-router'
import './index.css'
import './i18n'
import { router } from './router'
import { installNativeEventBridge } from './infrastructure/nativeEvents'
import { setLanguage } from './i18n'
import { getAppLanguage } from './infrastructure/nativeClient'

installNativeEventBridge()

// Restore the stored language before the first paint; fall back to the system
// locale when nothing is persisted or the bridge is not ready yet.
void getAppLanguage().then(stored => {
  if (stored) {
    void setLanguage(stored)
  }
})

// macOS draws traffic-light buttons over the page: the chrome shifts right.
if (navigator.userAgent.includes('Mac')) {
  document.documentElement.classList.add('is-macos')
}

window.onerror = function(message, source, lineno, colno, error) {
  document.body.innerHTML += `<div style="color:red; background:white; padding:20px; position:absolute; top:0; left:0; z-index:9999;">
    <h3>Global Error</h3>
    <p>${message}</p>
    <p>${source}:${lineno}:${colno}</p>
    <pre>${error?.stack}</pre>
  </div>`;
};

window.addEventListener('unhandledrejection', function(event) {
  document.body.innerHTML += `<div style="color:red; background:white; padding:20px; position:absolute; top:0; left:0; z-index:9999;">
    <h3>Unhandled Promise Rejection</h3>
    <p>${event.reason}</p>
  </div>`;
});

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <RouterProvider router={router} />
  </StrictMode>,
)
