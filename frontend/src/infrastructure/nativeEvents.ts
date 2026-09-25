export type NativeEventHandler = (topic: string, payload: string) => void

const handlers = new Set<NativeEventHandler>()

declare global {
  interface Window {
    __emitNativeEvent?: (topic: string, payload: string) => void
    __nativeEventsReady?: boolean
  }
}

export function onNativeEvent(handler: NativeEventHandler): () => void {
  handlers.add(handler)
  return () => {
    handlers.delete(handler)
  }
}

// Installed by the platform window before page scripts run.
export function installNativeEventBridge(): void {
  window.__emitNativeEvent = (topic: string, payload: string) => {
    for (const handler of handlers) {
      try {
        handler(topic, payload)
      } catch (error) {
        console.error('native event handler failed', error)
      }
    }
  }
  window.__nativeEventsReady = true
}
