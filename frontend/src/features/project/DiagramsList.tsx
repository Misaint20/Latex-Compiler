import { useState } from 'react'
import { useTranslation } from 'react-i18next'
import type { ProjectFile, ProjectScanResult } from '../../domain/ipc'
import { useEditorPreference } from '../../application/useEditorPreference'
import { DiagramPreview } from './DiagramPreview'
import { DiagramSourceEditor } from './DiagramSourceEditor'
import { EditorPicker } from './EditorPicker'

function relative(file: ProjectFile): string {
  return file.folder === '' ? file.name : `${file.folder}/${file.name}`
}

type ViewMode = 'preview' | 'source'

export function DiagramsList({ scan }: { scan: ProjectScanResult }) {
  const { t } = useTranslation()
  const { presets, availability, preferred, customTemplate } = useEditorPreference('diagram')
  const [menuFor, setMenuFor] = useState<string | null>(null)
  const [openFiles, setOpenFiles] = useState<Set<string>>(new Set())
  const [viewModes, setViewModes] = useState<Record<string, ViewMode>>({})
  const [previewVersions, setPreviewVersions] = useState<Record<string, number>>({})
  const [openError, setOpenError] = useState<string | null>(null)

  const toggle = (key: string) => {
    setOpenError(null)
    setOpenFiles(previous => {
      const next = new Set(previous)
      if (next.has(key)) {
        next.delete(key)
      } else {
        next.add(key)
      }
      return next
    })
  }

  const setMode = (key: string, mode: ViewMode) => {
    setViewModes(previous => ({ ...previous, [key]: mode }))
  }

  const markSaved = (key: string) => {
    setPreviewVersions(previous => ({ ...previous, [key]: (previous[key] ?? 0) + 1 }))
  }

  const openExternal = async (file: ProjectFile) => {
    setOpenError(null)
    const { openProjectFile } = await import('../../infrastructure/nativeClient')
    try {
      await openProjectFile(scan.projectPath, relative(file))
    } catch (e) {
      setOpenError(e instanceof Error ? e.message : t('diagrams.openError'))
    }
  }

  // One-off launch with an explicit preset; never touches the stored choice.
  const openWith = async (file: ProjectFile, preset: string) => {
    setMenuFor(null)
    setOpenError(null)
    const { openProjectFileWith } = await import('../../infrastructure/nativeClient')
    try {
      await openProjectFileWith(scan.projectPath, relative(file), preset)
    } catch (e) {
      setOpenError(e instanceof Error ? e.message : t('diagrams.openError'))
    }
  }

  const preferredLabel =
    preferred === 'custom'
      ? t('diagrams.customWithTemplate', { template: customTemplate })
      : (presets.find(p => p.id === preferred)?.label ?? t('diagrams.systemDefault'))

  const installedPresets = presets.filter(
    p => p.id !== 'default' && p.id !== 'custom' && (!availability || p.installed),
  )
  const hasCustomTemplate = customTemplate.trim() !== ''

  if (scan.diagrams.length === 0) {
    return <p className="text-sm text-ink-400 italic">{t('diagrams.empty')}</p>
  }

  return (
    <div className="space-y-3">
      {openError && <p className="text-sm text-red-400">{openError}</p>}
      <ul className="space-y-2">
        {scan.diagrams.map(file => {
          const key = relative(file)
          const isOpen = openFiles.has(key)
          const mode = viewModes[key] ?? 'preview'
          return (
            <li key={key} className="bg-ink-800/60 rounded">
              <div className="relative flex items-center justify-between px-3 py-2 hover:bg-ink-700/50 rounded transition-colors">
                <button
                  onClick={() => toggle(key)}
                  className="flex-1 text-left text-sm text-parchment/90 font-mono"
                >
                  <span
                    className={`inline-block mr-2 transition-transform ${isOpen ? 'rotate-90' : ''}`}
                  >
                    ›
                  </span>
                  {key}
                </button>
                {isOpen && (
                  <>
                    <button
                      onClick={() => setMode(key, mode === 'preview' ? 'source' : 'preview')}
                      className={`ml-3 text-xs transition-colors ${
                        mode === 'source' ? 'text-amber-300' : 'text-ink-300 hover:text-amber-300'
                      }`}
                    >
                      {mode === 'source' ? t('diagrams.editSource') : t('diagrams.viewSource')}
                    </button>
                    <button
                      onClick={() => setMenuFor(menuFor === key ? null : key)}
                      className="ml-3 text-xs text-ink-300 hover:text-brass-400 transition-colors"
                    >
                      {t('diagrams.openWith')}
                    </button>
                    {menuFor === key && (
                      <>
                        <div className="fixed inset-0 z-10" onClick={() => setMenuFor(null)} />
                        <div className="absolute right-2 top-9 z-20 min-w-52 bg-ink-900 border border-ink-600 rounded shadow-xl py-1">
                          <p className="px-3 py-1 text-[11px] uppercase tracking-wide text-ink-400">
                            {t('diagrams.openWithTitle')}
                          </p>
                          <button
                            onClick={() => void openExternal(file)}
                            className="block w-full text-left px-3 py-1.5 text-xs font-medium text-brass-300 hover:bg-ink-800 transition-colors"
                          >
                            {t('diagrams.preferred', { label: preferredLabel })}
                          </button>
                          <div className="my-1 border-t border-ink-600" />
                          <button
                            onClick={() => void openWith(file, '')}
                            className="block w-full text-left px-3 py-1.5 text-xs text-parchment/70 hover:bg-ink-800 transition-colors"
                          >
                            {t('diagrams.systemDefault')}
                          </button>
                          {installedPresets.map(preset => (
                            <button
                              key={preset.id}
                              onClick={() => void openWith(file, preset.id)}
                              className="block w-full text-left px-3 py-1.5 text-xs text-parchment/70 hover:bg-ink-800 transition-colors"
                            >
                              {preset.label}
                            </button>
                          ))}
                          {hasCustomTemplate && (
                            <button
                              onClick={() => void openWith(file, 'custom')}
                              className="block w-full text-left px-3 py-1.5 text-xs text-parchment/70 hover:bg-ink-800 transition-colors"
                            >
      ? 'Personalizado (' + customTemplate + ')'
                            </button>
                          )}
                        </div>
                      </>
                    )}
                  </>
                )}
                <button
                  onClick={() => toggle(key)}
                  className="ml-3 text-xs text-brass-400"
                >
                  {isOpen ? t('diagrams.hide') : t('diagrams.show')}
                </button>
              </div>
              {isOpen &&
                (mode === 'source' ? (
                  <DiagramSourceEditor
                    scan={scan}
                    file={file}
                    onSaved={() => markSaved(key)}
                    showRecommendation
                  />
                ) : (
                  <DiagramPreview
                    key={`${key}-v${previewVersions[key] ?? 0}`}
                    scan={scan}
                    file={file}
                  />
                ))}
            </li>
          )
        })}
      </ul>
      <EditorPicker scope="diagram" />
    </div>
  )
}
