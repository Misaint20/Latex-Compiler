import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import type { ProjectFile } from '../domain/ipc'
import { FileIcon, FolderIcon } from './icons'
import { TextSourceEditor } from '../features/project/TextSourceEditor'
import { AssetPreview, isPreviewable } from '../features/project/AssetPreview'

function formatSize(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
}

function groupByFolder(files: ProjectFile[], rootLabel: string): [string, ProjectFile[]][] {
  const groups = new Map<string, ProjectFile[]>()
  for (const file of files) {
    const key = file.folder === '' ? rootLabel : file.folder
    const bucket = groups.get(key)
    if (bucket) {
      bucket.push(file)
    } else {
      groups.set(key, [file])
    }
  }
  return [...groups.entries()].sort(([a], [b]) => a.localeCompare(b))
}

export function FileList({
  files,
  accent,
  projectPath,
}: {
  files: ProjectFile[]
  accent: string
  projectPath: string
}) {
  const [openError, setOpenError] = useState<string | null>(null)
  const [sourceFor, setSourceFor] = useState<string | null>(null)
  const [previewFor, setPreviewFor] = useState<string | null>(null)
  const { t } = useTranslation()

  const relativeOf = (file: ProjectFile): string =>
    file.folder === '' ? file.name : `${file.folder}/${file.name}`

  const openFile = async (file: ProjectFile) => {
    setOpenError(null)
    const { openProjectFile } = await import('../infrastructure/nativeClient')
    try {
      await openProjectFile(projectPath, relativeOf(file))
    } catch (e) {
      setOpenError(e instanceof Error ? e.message : t('fileList.openError'))
    }
  }

  if (files.length === 0) {
    return <p className="text-sm text-ink-400 italic">{t('fileList.empty')}</p>
  }

  return (
    <div className="space-y-3">
      {openError && (
        <p className="text-sm text-red-400">{openError}</p>
      )}
      {groupByFolder(files, t('fileList.rootFolder')).map(([folder, groupFiles]) => (
        <div key={folder}>
          <p className="text-xs font-semibold text-ink-300 mb-1 font-mono flex items-center gap-1">
            <FolderIcon className="w-3.5 h-3.5" />
            {folder}
          </p>
          <ul className="space-y-1">
            {groupFiles.map(file => {
              const isEditable = /\.(tex|mmd|mermaid)$/i.test(file.name)
              const canPreview = isPreviewable(file)
              const key = relativeOf(file)
              const sourceOpen = sourceFor === key
              const previewOpen = previewFor === key
              return (
                <li key={`${file.folder}/${file.name}`}>
                  <div className="flex items-center gap-2">
                    <button
                      onClick={() => openFile(file)}
                      title={t('fileList.openDefaultTooltip')}
                      className="flex-1 min-w-0 flex items-center justify-between bg-ink-800/60 hover:bg-ink-700/60 rounded px-3 py-1.5 transition-colors text-left"
                    >
                      <span className="text-sm text-parchment/90 flex items-center gap-1.5 min-w-0">
                        <FileIcon className={accent} />
                        {file.name}
                      </span>
                      <span className="text-xs text-ink-400 font-mono shrink-0">
                        {formatSize(file.sizeBytes)}
                      </span>
                    </button>
                    {isEditable && (
                      <button
                        onClick={() => {
                          setSourceFor(sourceOpen ? null : key)
                          setPreviewFor(null)
                        }}
                        className={`shrink-0 text-xs transition-colors ${
                          sourceOpen ? 'text-amber-300' : 'text-ink-300 hover:text-brass-400'
                        }`}
                      >
                        {sourceOpen ? t('diagrams.editSource') : t('diagrams.viewSource')}
                      </button>
                    )}
                    {canPreview && (
                      <button
                        onClick={() => {
                          setPreviewFor(previewOpen ? null : key)
                          setSourceFor(null)
                        }}
                        className={`shrink-0 text-xs transition-colors ${
                          previewOpen ? 'text-amber-300' : 'text-ink-300 hover:text-brass-400'
                        }`}
                      >
                        {previewOpen ? t('assetsPreview.hide') : t('assetsPreview.show')}
                      </button>
                    )}
                  </div>
                  {sourceOpen && (
                    <TextSourceEditor
                      key={key}
                      scan={{ projectPath }}
                      file={file}
                    />
                  )}
                  {previewOpen && (
                    <AssetPreview
                      key={key}
                      scan={{ projectPath }}
                      file={file}
                    />
                  )}
                </li>
              )
            })}
          </ul>
        </div>
      ))}
    </div>
  )
}
