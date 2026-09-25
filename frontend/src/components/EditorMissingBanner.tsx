import { useState } from 'react'
import { useTranslation } from 'react-i18next'
import { useEditorPreference } from '../application/useEditorPreference'
import { WarningIcon } from './icons'

// True when the stored preference points at a preset that the background
// refresh found to be no longer installed.
function ghostPreset(
  preferred: string,
  availability: boolean,
  presets: { id: string; label: string; installed: boolean }[],
): { id: string; label: string } | null {
  if (preferred === '' || preferred === 'custom' || presets.length === 0) {
    return null
  }
  const preset = presets.find(p => p.id === preferred)
  const uninstalled = preset !== undefined && availability && !preset.installed
  if (!uninstalled) {
    return null
  }
  return { id: preset.id, label: preset.label }
}

// At most one banner at a time: editor scope wins over diagram scope.
export function EditorMissingBanner() {
  const { t } = useTranslation()
  const editorState = useEditorPreference('editor')
  const diagramState = useEditorPreference('diagram')

  const editorGhost = ghostPreset(
    editorState.preferred,
    editorState.availability,
    editorState.presets,
  )
  const diagramGhost = ghostPreset(
    diagramState.preferred,
    diagramState.availability,
    diagramState.presets,
  )
  const ghost = editorGhost
    ? { scope: 'editor' as const, ...editorGhost }
    : diagramGhost
      ? { scope: 'diagram' as const, ...diagramGhost }
      : null

  // Dismissing is per occurrence; picking the same preset again re-shows it.
  const [dismissedKey, setDismissedKey] = useState<string | null>(null)
  const key = ghost ? `${ghost.scope}:${ghost.id}` : null
  const visible = ghost !== null && dismissedKey !== key

  if (!visible || !ghost) {
    return null
  }
  const isEditor = ghost.scope === 'editor'

  return (
    <div className="max-w-4xl mx-auto mb-6">
      <div className="flex items-start gap-2.5 rounded-lg border border-amber-700/50 bg-amber-950/40 px-4 py-3">
        <span className="text-amber-400 mt-0.5 shrink-0">
          <WarningIcon />
        </span>
        <div className="text-sm flex-1 min-w-0">
          <p className="text-amber-300 font-medium leading-snug">
            {t(isEditor ? 'banner.editorTitle' : 'banner.diagramTitle', { name: ghost.label })}
          </p>
          <p className="text-amber-200/70 leading-snug">
            {t(isEditor ? 'banner.editorDetail' : 'banner.diagramDetail')}
          </p>
        </div>
        <button
          onClick={() => setDismissedKey(key)}
          aria-label={t('banner.dismiss')}
          className="px-1.5 text-amber-300/70 hover:text-amber-200 transition-colors text-lg leading-none"
        >
          ×
        </button>
      </div>
    </div>
  )
}
