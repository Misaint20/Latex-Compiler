import { useEffect, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { ping, recentProjects, removeRecentProject, removeMissingRecents } from '../infrastructure/nativeClient'
import { useProjectExplorer } from '../application/useProjectExplorer'
import { useChrome } from '../application/chrome'
import { ProjectExplorer } from '../features/project/ProjectExplorer'
import { ClockIcon, FolderOpenIcon, SearchIcon } from '../components/icons'
import type { RecentProject } from '../domain/ipc'
import type { TFunction } from 'i18next'

function formatRecency(timestampMs: number, t: TFunction): string {
  const diff = Date.now() - timestampMs
  const minutes = Math.floor(diff / 60_000)
  if (minutes < 1) {
    return t('home.momentAgo')
  }
  if (minutes < 60) {
    return t('home.minutesAgo', { count: minutes })
  }
  const hours = Math.floor(minutes / 60)
  if (hours < 24) {
    return t('home.hoursAgo', { count: hours })
  }
  const days = Math.floor(hours / 24)
  return t('home.daysAgo', { count: days })
}

export function HomePage() {
  const { browseRequest, setSection } = useChrome()
  const { t } = useTranslation()
  const [connection, setConnection] = useState<string | null>(null)
  const [recents, setRecents] = useState<RecentProject[] | null>(null)
  const [cleanFeedback, setCleanFeedback] = useState<string | null>(null)
  const { phase, scan, isBusy, error, openPath, browse, rescan, backToWelcome } = useProjectExplorer()

  useEffect(() => {
    void ping().then(
      () => setConnection(t('home.backendOk')),
      (err: unknown) =>
        setConnection(err instanceof Error ? err.message : t('home.backendError')),
    )
  }, [])

  // The top bar asks the home page to open the folder picker.
  useEffect(() => {
    if (browseRequest > 0) {
      void browse()
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [browseRequest])

  // Reflect the explorer phase back into the top bar highlight.
  useEffect(() => {
    if (phase !== 'exploring') {
      setSection('welcome')
    }
  }, [phase, setSection])

  useEffect(() => {
    if (phase !== 'welcome') {
      return
    }
    let cancelled = false
    void recentProjects().then(
      (result) => {
        if (!cancelled) {
          setRecents(result.projects)
        }
      },
      () => {
        if (!cancelled) {
          setRecents([])
        }
      },
    )
    return () => {
      cancelled = true
    }
  }, [phase])

  // The ProjectExplorer reports the visible tab so the top bar follows.
  const handleSectionChange = (section: string) => {
    setSection(section as Parameters<typeof setSection>[0])
  }

  const forgetProject = async (path: string) => {
    try {
      await removeRecentProject(path)
      setRecents(previous => (previous ?? []).filter(entry => entry.path !== path))
    } catch {
      // The list stays as is when the removal cannot be persisted.
    }
  }

  const missingCount = recents?.filter(entry => !entry.exists).length ?? 0

  const cleanMissing = async () => {
    try {
      const { removed } = await removeMissingRecents()
      if (removed > 0) {
        setRecents(previous => (previous ?? []).filter(entry => entry.exists))
        setCleanFeedback(t(removed === 1 ? 'home.removedOne' : 'home.removedMany', { count: removed }))
      }
    } catch {
      // Keep the list untouched when the purge cannot be persisted.
    }
  }

  return (
    <main className="min-h-screen p-8 pt-2 text-parchment">
      <div className="max-w-4xl mx-auto">
        {phase === 'welcome' && (
          <section className="card p-10 text-center">
            <div className="flex justify-center mb-4 text-brass-400">
              <FolderOpenIcon className="h-12 w-12" />
            </div>
            <h2 className="text-xl font-semibold text-parchment mb-2">{t('home.openProjectTitle')}</h2>
            <p className="text-xs text-ink-400 mb-4">{connection ?? t('home.connectBackend')}</p>
            <p className="text-sm text-ink-300 mb-6 max-w-md mx-auto">
              {t('home.openProjectHelp')}
            </p>
            <button
              onClick={browse}
              disabled={isBusy}
              className="btn-brass mx-auto"
            >
              <SearchIcon />
              {isBusy ? t('home.opening') : t('home.browse')}
            </button>
            {error && (
              <p className="mt-4 text-sm text-vermilion">{error}</p>
            )}
          </section>
        )}

        {phase === 'welcome' && recents !== null && recents.length > 0 && (
          <section className="card mt-6 p-5">
            <div className="flex items-center justify-between mb-3">
              <h3 className="font-display text-sm font-semibold text-parchment-dim">{t('home.recentsTitle')}</h3>
              {cleanFeedback ? (
                <span className="text-xs text-sage">{cleanFeedback}</span>
              ) : missingCount > 0 ? (
                <button
                  onClick={() => void cleanMissing()}
                  disabled={isBusy}
                  className="btn-ghost !border-vermilion/40 !text-vermilion hover:!bg-vermilion/10"
                >
                  {t(missingCount === 1 ? 'home.cleanMissing_one' : 'home.cleanMissing_other', { count: missingCount })}
                </button>
              ) : null}
            </div>
            <ul className="divide-y divide-ink-700">
              {recents.map((project, index) => (
                <li key={project.path} className={`py-2 flex items-center gap-3 ${project.exists ? '' : 'opacity-60'}`}>
                  <button
                    onClick={() => void openPath(project.path)}
                    disabled={isBusy}
                    className="flex-1 min-w-0 text-left group"
                  >
                    <span className={`block text-sm font-mono truncate transition-colors ${project.exists ? 'text-ink-100 group-hover:text-brass-300' : 'text-ink-400 line-through'}`}>
                      {project.path}
                    </span>
                    <span className="block text-xs text-ink-400">
                      {project.exists ? (
                        <>
                          {index === 0 ? t('home.lastProject') : ''}
                          {formatRecency(project.lastOpenedMs, t)}
                        </>
                      ) : (
                        <span className="text-vermilion/90">{t('home.missingFolder')}{formatRecency(project.lastOpenedMs, t)}</span>
                      )}
                    </span>
                  </button>
                  {index === 0 && project.exists && (
                    <button
                      onClick={() => void openPath(project.path)}
                      disabled={isBusy}
                      className="btn-brass !px-3 !py-1.5 !text-xs"
                    >
                      <ClockIcon />
                      {t('home.continueButton')}
                    </button>
                  )}
                  <button
                    onClick={() => void forgetProject(project.path)}
                    disabled={isBusy}
                    title={t('home.forgetTooltip')}
                    aria-label={t('home.forgetTooltip')}
                    className="px-2 py-1 text-ink-400 hover:text-vermilion transition-colors text-lg leading-none"
                  >
                    ×
                  </button>
                </li>
              ))}
            </ul>
          </section>
        )}

        {phase === 'exploring' && scan && (
          <ProjectExplorer
            scan={scan}
            isBusy={isBusy}
            error={error}
            onBrowse={browse}
            onRescan={rescan}
            onBack={backToWelcome}
            onSectionChange={handleSectionChange}
          />
        )}
      </div>
    </main>
  )
}
