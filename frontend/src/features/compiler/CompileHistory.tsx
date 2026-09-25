import { useCallback, useEffect, useMemo, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { compileHistory } from '../../infrastructure/nativeClient'
import { onNativeEvent } from '../../infrastructure/nativeEvents'
import { STAGE_ORDER, type HistoryEntry, type StageKey } from '../../domain/ipc'
import { ClockIcon } from '../../components/icons'

const RESULT_STYLES: Record<HistoryEntry['result'], string> = {
  succeeded: 'bg-emerald-500/15 text-emerald-300 border-emerald-700/60',
  failed: 'bg-red-500/15 text-red-300 border-red-700/60',
  canceled: 'bg-amber-500/15 text-amber-300 border-amber-700/60',
}

function formatDuration(ms: number): string {
  if (ms < 1000) {
    return `${ms} ms`
  }
  const seconds = ms / 1000
  if (seconds < 60) {
    return `${seconds.toFixed(1)} s`
  }
  const minutes = Math.floor(seconds / 60)
  return `${minutes}m ${Math.round(seconds - minutes * 60)}s`
}

function formatTimestamp(timestampMs: number): string {
  const date = new Date(timestampMs)
  return date.toLocaleString(undefined, {
    day: '2-digit',
    month: 'short',
    hour: '2-digit',
    minute: '2-digit',
  })
}

function StageTrail({ entry }: { entry: HistoryEntry }) {
  const { t } = useTranslation()
  if (entry.stagesReached.length === 0) {
    return null
  }
  const ordered = STAGE_ORDER.filter(key => entry.stagesReached.includes(key))
  const extras = entry.stagesReached.filter(key => !STAGE_ORDER.includes(key))
  return (
    <span className="inline-flex flex-wrap items-center gap-1 text-[10px] text-ink-400">
      {ordered.map(key => (
        <span key={key} className="inline-flex items-center gap-1">
          <span
            className={`px-1.5 py-0.5 rounded border ${
              entry.result === 'failed'
                ? 'border-ink-500 bg-ink-800/60 text-ink-300'
                : 'border-ink-600 bg-ink-800/60 text-ink-400'
            }`}
          >
            {t(`stages.${key}`)}
            {typeof entry.stageDurationsMs[key] === 'number' &&
              ` · ${formatDuration(entry.stageDurationsMs[key] as number)}`}
          </span>
        </span>
      ))}
      {extras.map(key => (
        <span key={key} className="px-1.5 py-0.5 rounded border border-ink-600 bg-ink-800/60 text-ink-400">
          {key}
        </span>
        ))}
    </span>
  )
}

export function CompileHistory({ projectPath }: { projectPath: string }) {
  const { t } = useTranslation()
  const [entries, setEntries] = useState<HistoryEntry[] | null>(null)
  const [error, setError] = useState<string | null>(null)

  const refresh = useCallback(async () => {
    try {
      const result = await compileHistory(projectPath)
      setEntries(result.entries)
      setError(null)
    } catch (e) {
      setError(e instanceof Error ? e.message : t('history.loadError'))
    }
  }, [projectPath])

  useEffect(() => {
    void refresh()
    // The list refreshes itself when any compile finishes, even while the
    // user is looking at another tab of the same explorer.
    const unsubscribe = onNativeEvent((topic, payload) => {
      if (topic === 'compile_success' || topic === 'compile_error' || topic === 'compile_canceled') {
        try {
          const data = JSON.parse(payload) as { project?: string }
          if (data.project === undefined || data.project === projectPath) {
            void refresh()
          }
        } catch {
          void refresh()
        }
      }
    })
    return unsubscribe
  }, [refresh, projectPath])

  const failurePattern = useMemo(() => {
    const withStages = entries?.filter(entry => entry.result === 'failed' && entry.stagesReached.length > 0) ?? []
    if (withStages.length === 0) {
      return null
    }
    const counts = new Map<StageKey, number>()
    for (const entry of withStages) {
      const last = entry.stagesReached[entry.stagesReached.length - 1]
      counts.set(last, (counts.get(last) ?? 0) + 1)
    }
    const top = [...counts.entries()].sort((a, b) => b[1] - a[1])[0]
    return { stage: top[0], count: top[1], total: withStages.length }
  }, [entries])

  if (error) {
    return <p className="text-sm text-red-300">{error}</p>
  }
  if (entries === null) {
    return <p className="text-sm text-ink-400">{t('history.loading')}</p>
  }
  if (entries.length === 0) {
    return <p className="text-sm text-ink-400">{t('history.empty')}</p>
  }

  return (
    <div>
      {failurePattern && (
        <p className="mb-3 text-xs text-amber-300/90 border border-amber-700/50 bg-amber-500/10 rounded px-3 py-2">
          {failurePattern.count === failurePattern.total
            ? t('history.allFailuresAtStage', { total: failurePattern.total, stage: t(`stages.${failurePattern.stage}`) })
            : t('history.topFailureStage', { stage: t(`stages.${failurePattern.stage}`), count: failurePattern.count, total: failurePattern.total })}
        </p>
        )}
      <ul className="divide-y divide-ink-600/60">
        {entries.map((entry, index) => (
          <li key={`${entry.timestampMs}-${index}`} className="py-2.5 flex flex-wrap items-center gap-3">
            <span className={`px-2 py-0.5 rounded-full border text-xs font-medium ${RESULT_STYLES[entry.result] ?? ''}`}>
              {t(`history.result${entry.result.charAt(0).toUpperCase()}${entry.result.slice(1)}`)}
            </span>
            <span className="text-sm text-parchment/70 font-mono truncate max-w-48" title={entry.mainFile}>
              {entry.mainFile}
            </span>
            <span className="text-sm text-ink-300 inline-flex items-center gap-1">
              <ClockIcon />
              {formatDuration(entry.durationMs)}
            </span>
            <span className="text-xs text-ink-400 ml-auto">{formatTimestamp(entry.timestampMs)}</span>
            <span className="w-full flex flex-wrap items-center gap-1.5">
              <StageTrail entry={entry} />
              {entry.result === 'succeeded' && entry.outputPath ? (
                <span className="text-xs text-ink-400 font-mono truncate max-w-56" title={entry.outputPath}>
                  {entry.outputPath}
                </span>
              ) : entry.errorMessage ? (
                <span className="text-xs text-red-400/90 truncate max-w-56" title={entry.errorMessage}>
                  {entry.errorMessage}
                </span>
              ) : null}
            </span>
          </li>
        ))}
      </ul>
    </div>
  )
}
