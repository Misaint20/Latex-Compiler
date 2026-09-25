import { useEffect, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { FileList } from '../../components/FileList'
import { rememberLastTab, rememberMainFile } from '../../infrastructure/nativeClient'
import { CompilerPanel } from '../compiler/CompilerPanel'
import { CompileHistory } from '../compiler/CompileHistory'
import { DiagramsList } from './DiagramsList'
import { EditorPicker } from './EditorPicker'
import { ArrowLeftIcon, FolderOpenIcon, RefreshIcon } from '../../components/icons'
import type { ProjectScanResult } from '../../domain/ipc'

type SectionKey = 'chapters' | 'diagrams' | 'styles' | 'assets' | 'history'

const TAB_KEYS: SectionKey[] = ['chapters', 'diagrams', 'styles', 'assets', 'history']

const SECTION_KEYS: Record<SectionKey, string> = {
  chapters: 'explorer.chaptersTab',
  diagrams: 'explorer.diagramsTab',
  styles: 'explorer.stylesTab',
  assets: 'explorer.assetsTab',
  history: 'explorer.historyTab',
}

export function ProjectExplorer({
  scan,
  isBusy,
  error,
  onBrowse,
  onRescan,
  onBack,
  onSectionChange,
}: {
  scan: ProjectScanResult
  isBusy: boolean
  error: string | null
  onBrowse: () => void
  onRescan: (projectPath: string) => void
  onBack: () => void
  onSectionChange?: (section: string) => void
}) {
  const { t } = useTranslation()
  // Restore the tab remembered for this project; unknown or empty values
  // fall back to the default chapters tab.
  const [section, setSection] = useState<SectionKey>(
    (TAB_KEYS as string[]).includes(scan.savedLastTab)
      ? (scan.savedLastTab as SectionKey)
      : 'chapters',
  )

  // Sync the top bar with the restored tab on first render.
  useEffect(() => {
    onSectionChange?.(section)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const chooseSection = (key: SectionKey) => {
    setSection(key)
    onSectionChange?.(key)
    void rememberLastTab(scan.projectPath, key).catch(() => {
      // The choice still applies this session when persisting fails.
    })
  }
  const [mainFile, setMainFile] = useState(
    scan.savedMainFile && scan.texFiles.includes(scan.savedMainFile)
      ? scan.savedMainFile
      : scan.mainFileCandidate,
  )

  const chooseMainFile = (file: string) => {
    setMainFile(file)
    void rememberMainFile(scan.projectPath, file).catch(() => {
      // The choice still applies this session when persisting fails.
    })
  }

  const filesBySection: Record<Exclude<SectionKey, 'history'>, typeof scan.chapters> = {
    chapters: scan.chapters,
    diagrams: scan.diagrams,
    styles: scan.styles,
    assets: scan.assets,
  }
  const counts: Record<SectionKey, number | null> = {
    chapters: scan.chapters.length,
    diagrams: scan.diagrams.length,
    styles: scan.styles.length,
    assets: scan.assets.length,
    history: null,
  }

  return (
    <div className="w-full">
      <div className="flex flex-wrap items-center gap-3 mb-6">
        <button
          onClick={onBack}
          className="px-3 py-1.5 btn-ghost !rounded transition-colors inline-flex items-center gap-1.5"
        >
          <ArrowLeftIcon />
          {t('explorer.changeProject')}
        </button>
        <p className="text-sm text-ink-300 font-mono flex-1 min-w-0 truncate">
          {scan.projectPath}
        </p>
        <button
          onClick={() => onRescan(scan.projectPath)}
          disabled={isBusy}
          className="px-3 py-1.5 bg-ink-800 hover:bg-ink-700 disabled:opacity-50 text-parchment/70 rounded text-sm transition-colors inline-flex items-center gap-1.5"
        >
          <RefreshIcon />
          {isBusy ? t('explorer.rescanning') : t('explorer.rescan')}
        </button>
        <button
          onClick={onBrowse}
          disabled={isBusy}
          className="px-3 py-1.5 bg-ink-800 hover:bg-ink-700 disabled:opacity-50 text-parchment/70 rounded text-sm transition-colors inline-flex items-center gap-1.5"
        >
          <FolderOpenIcon />
          Examinar…
        </button>
      </div>

      {error && (
        <div className="mb-4 rounded border border-vermilion/40 bg-vermilion/10 px-4 py-2 text-sm text-vermilion">
          {error}
        </div>
      )}

      <div className="flex gap-2 mb-4 border-b border-ink-600 pb-2">
        {(Object.keys(SECTION_KEYS) as SectionKey[]).map(key => (
          <button
            key={key}
            onClick={() => chooseSection(key)}
            className={`px-4 py-1.5 rounded-t text-sm font-medium transition-colors ${
              section === key
                ? 'tab-active !text-brass-300'
                : 'tab'
            }`}
          >
            {t(SECTION_KEYS[key])}{counts[key] !== null ? ` (${counts[key]})` : ''}
          </button>
        ))}
      </div>

      <section className="card p-4">
        <h2 className="text-lg font-semibold text-parchment mb-3">{t(SECTION_KEYS[section])}</h2>
        {section === 'diagrams' ? (
          <DiagramsList scan={scan} />
        ) : section === 'history' ? (
          <CompileHistory projectPath={scan.projectPath} />
        ) : (
          <FileList
            files={filesBySection[section]}
            accent="text-brass-400"
            projectPath={scan.projectPath}
          />
        )}
      </section>

      {section === 'chapters' && scan.chapters.length > 0 && (
        <>
          <EditorPicker />
          <div className="mt-6 flex flex-wrap items-center gap-3 card p-4">
            <label className="text-sm text-ink-300">{t('explorer.mainFile')}</label>
            <select
              value={mainFile}
              onChange={e => chooseMainFile(e.target.value)}
              className="select-input font-mono"
            >
              {scan.texFiles.map(file => (
                <option key={file} value={file}>{file}</option>
              ))}
            </select>
            <span className="text-xs text-ink-400">
              {scan.savedMainFile && mainFile === scan.savedMainFile
                ? t('explorer.remembered')
                : mainFile === scan.mainFileCandidate
                  ? t('explorer.autoDetected')
                  : t('explorer.manualChoice')}
            </span>
          </div>
          <CompilerPanel projectPath={scan.projectPath} mainFile={mainFile} />
        </>
      )}
    </div>
  )
}
