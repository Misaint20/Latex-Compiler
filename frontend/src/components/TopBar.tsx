import { useEffect } from 'react'
import { useTranslation } from 'react-i18next'
import { FolderOpenIcon } from './icons'
import { LanguagePicker } from './LanguagePicker'
import { useChrome } from '../application/chrome'

const SECTIONS = [
  { key: 'welcome', label: 'Inicio' },
  { key: 'chapters', label: 'Capítulos' },
  { key: 'diagrams', label: 'Diagramas' },
  { key: 'styles', label: 'Estilos' },
  { key: 'assets', label: 'Assets' },
  { key: 'history', label: 'Historial' },
] as const

export type TopBarSection = (typeof SECTIONS)[number]['key']

export function TopBar({
  active,
  onNavigate,
  onBrowse,
}: {
  active: TopBarSection
  onNavigate: (section: TopBarSection) => void
  onBrowse: () => void
}) {
  const { t } = useTranslation()
  const { isProjectOpen } = useChrome()
  const labels: Record<TopBarSection, string> = {
    welcome: t('topbar.home'),
    chapters: t('topbar.chapters'),
    diagrams: t('topbar.diagrams'),
    styles: t('topbar.styles'),
    assets: t('topbar.assets'),
    history: t('topbar.history'),
  }
  // The in-app menu bar (Windows/Linux) routes its open command here too.
  useEffect(() => {
    const onBrowseEvent = () => onBrowse()
    window.addEventListener('app:browse', onBrowseEvent)
    return () => window.removeEventListener('app:browse', onBrowseEvent)
  }, [onBrowse])

  return (
    <header className="app-topbar sticky top-0 z-30 mb-6 flex items-center gap-3 border-b border-ink-700/80 bg-ink-900/90 px-5 py-2.5 backdrop-blur">
      <button
        onClick={() => onNavigate('welcome')}
        className="flex items-center gap-2"
        title="Inicio"
      >
        <span className="grid h-7 w-7 place-items-center rounded-md bg-gradient-to-b from-brass-400 to-brass-600 text-xs font-bold text-ink-950 shadow">
          LX
        </span>
        <span className="font-display text-sm font-semibold tracking-wide text-parchment">
          {t('title')}
        </span>
      </button>

      <nav className="ml-4 flex items-center gap-1">
        {SECTIONS.map(section =>
          section.key !== 'welcome' && !isProjectOpen ? null : (
            <button
              key={section.key}
              onClick={() => onNavigate(section.key)}
              className={`tab ${active === section.key ? 'tab-active' : ''}`}
            >
              {labels[section.key]}
            </button>
          ),
        )}
      </nav>

      <div className="flex-1" />

      <LanguagePicker />

      <button
        onClick={onBrowse}
        className="btn-ghost"
        title={t('topbar.browseTooltip')}
      >
        <FolderOpenIcon />
        {t('topbar.browse')}
      </button>
    </header>
  )
}
