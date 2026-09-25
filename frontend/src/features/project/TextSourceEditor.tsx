import { useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import type { ProjectFile } from '../../domain/ipc'
import { readProjectFile, writeProjectTextFile } from '../../infrastructure/nativeClient'

export function TextSourceEditor({
  scan,
  file,
  onSaved,
  showRecommendation = true,
}: {
  scan: { projectPath: string }
  file: ProjectFile
  onSaved?: () => void
  showRecommendation?: boolean
}) {
  const [content, setContent] = useState<string | null>(null)
  const [loadError, setLoadError] = useState<string | null>(null)
  const [feedback, setFeedback] = useState<string | null>(null)
  const [saving, setSaving] = useState(false)
  const { t } = useTranslation()
  const dirtyRef = useRef(false)
  const relativePath = file.folder === '' ? file.name : `${file.folder}/${file.name}`

  useEffect(() => {
    let canceled = false
    void readProjectFile(scan.projectPath, relativePath).then(
      result => {
        if (!canceled) {
          setContent(result.content)
        }
      },
      () => {
        if (!canceled) {
          setLoadError(t('textEditor.readError'))
        }
      },
    )
    return () => {
      canceled = true
    }
  }, [scan.projectPath, relativePath, t])

  const save = async () => {
    if (content === null || saving) {
      return
    }
    setSaving(true)
    setFeedback(null)
    try {
      await writeProjectTextFile(scan.projectPath, relativePath, content)
      dirtyRef.current = false
      setFeedback(t('textEditor.saved'))
      onSaved?.()
    } catch (e) {
      setFeedback(e instanceof Error ? e.message : t('textEditor.saveError'))
    } finally {
      setSaving(false)
    }
  }

  if (loadError) {
    return <p className="text-xs text-red-400 px-3 py-2">{loadError}</p>
  }
  if (content === null) {
    return (
      <div className="flex items-center gap-2 px-2 py-2 text-xs text-ink-400">
        <span className="inline-block w-2.5 h-2.5 border-2 border-brass-500 border-t-transparent rounded-full animate-spin" />
        {t('textEditor.loadingSource')}
      </div>
    )
  }

  return (
    <div className="px-3 py-2 space-y-2">
      {showRecommendation && (
        <p className="rounded border border-amber-700/50 bg-amber-500/10 px-2.5 py-1.5 text-[11px] leading-snug text-amber-300">
          {t('textEditor.recommendation')}
        </p>
      )}
      <textarea
        value={content}
        onChange={e => {
          dirtyRef.current = true
          setContent(e.target.value)
          setFeedback(null)
        }}
        onKeyDown={e => {
          if ((e.metaKey || e.ctrlKey) && e.key === 's') {
            e.preventDefault()
            void save()
          }
        }}
        spellCheck={false}
        rows={12}
        className="w-full bg-ink-950/80 border border-ink-600 rounded p-3 text-xs text-parchment/90 font-mono leading-relaxed focus:outline-none focus:border-brass-500 resize-y"
      />
      <div className="flex items-center gap-3">
        <button
          onClick={() => void save()}
          disabled={saving}
          className="px-3 py-1.5 bg-brass-500 hover:bg-brass-400 disabled:opacity-50 text-ink-950 rounded text-xs font-medium transition-colors"
        >
          {saving ? t('textEditor.saving') : t('textEditor.save')}
        </button>
        {feedback && (
          <span className={`text-xs ${feedback === t('textEditor.saved') ? 'text-emerald-400' : 'text-red-400'}`}>
            {feedback}
          </span>
        )}
      </div>
    </div>
  )
}
