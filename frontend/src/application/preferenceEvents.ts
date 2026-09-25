type PreferenceListener = (scope: 'editor' | 'diagram') => void

const listeners = new Set<PreferenceListener>()

export function onEditorPreferenceChange(listener: PreferenceListener): () => void {
  listeners.add(listener)
  return () => {
    listeners.delete(listener)
  }
}

export function emitEditorPreferenceChange(scope: 'editor' | 'diagram'): void {
  for (const listener of listeners) {
    try {
      listener(scope)
    } catch (error) {
      console.error('editor preference listener failed', error)
    }
  }
}
