import { useEffect, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { useEditorPreference } from '../../application/useEditorPreference'
import { PencilIcon } from '../../components/icons'

type PickerScope = 'editor' | 'diagram'

const COPY: Record<PickerScope, { labelKey: string; defaultKey: string; feedbackKey: string }> = {
  editor: {
    labelKey: 'editorPicker.editorLabel',
    defaultKey: 'editorPicker.systemDefault',
    feedbackKey: 'editorPicker.systemDefaultFeedback',
  },
  diagram: {
    labelKey: 'editorPicker.diagramLabel',
    defaultKey: 'editorPicker.internalPreview',
    feedbackKey: 'editorPicker.internalPreviewFeedback',
  },
}

export function EditorPicker({ scope = 'editor' }: { scope?: PickerScope }) {
  const texts = COPY[scope]
  const { t } = useTranslation()
  const { presets, availability, preferred, customTemplate, saving, error, save } =
    useEditorPreference(scope)
  const [draft, setDraft] = useState(customTemplate)
  const [feedback, setFeedback] = useState<string | null>(null)

  useEffect(() => {
    setDraft(customTemplate)
  }, [customTemplate])

  const onChoose = async (value: string) => {
    setFeedback(null)
    if (value === 'custom') {
      await save('custom', draft)
      setFeedback(t('editorPicker.saved'))
      return
    }
    await save(value, customTemplate)
    setFeedback(value === '' ? t(texts.feedbackKey) : t('editorPicker.saved'))
  }

  const onSaveCustom = async () => {
    setFeedback(null)
    if (!draft.includes('{file}')) {
      setFeedback(t('editorPicker.templateNeedsFile'))
      return
    }
    await save('custom', draft)
    setFeedback(t('editorPicker.customSaved'))
  }

  // A stored preset that is no longer installed cannot stay selected; the
  // service already falls back when launching.
  const storedPreset = presets.find(p => p.id === preferred)
  const storedChoiceAvailable =
    preferred === '' ||
    preferred === 'custom' ||
    (storedPreset !== undefined && (!availability || storedPreset.installed))
  const selectedValue = storedChoiceAvailable ? preferred : ''

  return (
    <div className="mt-6 bg-ink-800/60 border border-ink-600 rounded-lg p-4">
      <div className="flex flex-wrap items-center gap-3">
        <label className="text-sm text-ink-300 inline-flex items-center gap-1.5">
          <PencilIcon />
          {t(texts.labelKey)}
        </label>
        <select
          value={selectedValue}
          onChange={e => void onChoose(e.target.value)}
          disabled={saving}
          className="bg-ink-800 border border-ink-600 rounded px-3 py-1.5 text-sm text-parchment/90 focus:outline-none focus:border-brass-500 disabled:opacity-50"
        >
          <option value="">{t(texts.defaultKey)}</option>
          {presets.map(preset => {
            const missing = availability && !preset.installed
            return (
              <option
                key={preset.id}
                value={preset.id}
                disabled={missing}
                className={missing ? 'text-ink-400' : undefined}
              >
                {missing ? t('editorPicker.notInstalled', { label: preset.label }) : preset.label}
              </option>
            )
          })}
          <option value="custom">{t('editorPicker.customOption')}</option>
        </select>

        {preferred === 'custom' && (
          <input
            type="text"
            value={draft}
            onChange={e => setDraft(e.target.value)}
            placeholder="code {file}"
            spellCheck={false}
            className="flex-1 min-w-56 bg-ink-800 border border-ink-600 rounded px-3 py-1.5 text-sm text-parchment/90 font-mono focus:outline-none focus:border-brass-500"
          />
        )}

        {preferred === 'custom' && (
          <button
            onClick={() => void onSaveCustom()}
            disabled={saving}
            className="px-3 py-1.5 bg-brass-500 hover:bg-brass-400 disabled:opacity-50 text-ink-950 rounded text-sm font-medium transition-colors"
          >
            {t('diagrams.save')}
          </button>
        )}

        {feedback && <span className="text-xs text-emerald-400">{feedback}</span>}
        {error && <span className="text-xs text-red-400">{error}</span>}
      </div>
    </div>
  )
}
