import { useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import type { ProjectFile } from '../../domain/ipc'

type MermaidApi = typeof import('mermaid').default

let mermaidPromise: Promise<MermaidApi> | null = null

function getMermaid(): Promise<MermaidApi> {
  if (!mermaidPromise) {
    mermaidPromise = import('mermaid').then(module => {
      const mermaid = module.default
      mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'strict',
        theme: 'dark',
      })
      return mermaid
    })
  }
  return mermaidPromise
}

// mmdc convention: a diagram named flujo.mmd picks up flujo.json / flujo.css
// next to it, then mermaid.config.json / mermaid.css in the same folder,
// then those two files at the project root. All files are optional.
function configCandidatesFor(file: ProjectFile): string[] {
  const baseName = file.name.replace(/\.(mmd|mermaid)$/i, '')
  const folderPrefix = file.folder === '' ? '' : `${file.folder}/`
  return [
    `${folderPrefix}${baseName}.json`,
    `${folderPrefix}mermaid.config.json`,
    'mermaid.config.json',
  ]
}

function cssCandidatesFor(file: ProjectFile): string[] {
  const baseName = file.name.replace(/\.(mmd|mermaid)$/i, '')
  const folderPrefix = file.folder === '' ? '' : `${file.folder}/`
  return [
    `${folderPrefix}${baseName}.css`,
    `${folderPrefix}mermaid.css`,
    'mermaid.css',
  ]
}

async function readFirstAvailable(
  projectPath: string,
  candidates: string[],
): Promise<string | null> {
  const { readProjectFile } = await import('../../infrastructure/nativeClient')
  for (const candidate of candidates) {
    try {
      const { content } = await readProjectFile(projectPath, candidate)
      return content
    } catch {
      // Optional file: keep walking the fallback chain.
    }
  }
  return null
}

function parseDiagramConfig(raw: string | null): {
  config: Record<string, unknown> | null
  warningKey: 'diagrams.configNotObject' | 'diagrams.configInvalid' | null
  warning: string | null
} {
  if (raw === null || raw.trim() === '') {
    return { config: null, warningKey: null, warning: null }
  }
  try {
    const parsed: unknown = JSON.parse(raw)
    if (parsed !== null && typeof parsed === 'object' && !Array.isArray(parsed)) {
      return { config: parsed as Record<string, unknown>, warningKey: null, warning: null }
    }
    return { config: null, warningKey: 'diagrams.configNotObject' as const, warning: null }
  } catch (e) {
    return {
      config: null,
      warning: e instanceof Error ? e.message : null,
      warningKey: 'diagrams.configInvalid' as const,
    }
  }
}

export function DiagramPreview({
  scan,
  file,
}: {
  scan: { projectPath: string }
  file: ProjectFile
}) {
  const [svg, setSvg] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)
  const { t } = useTranslation()
  const [configWarningKey, setConfigWarningKey] = useState<'diagrams.configNotObject' | 'diagrams.configInvalid' | null>(null)
  const [configWarningArg, setConfigWarningArg] = useState<string | null>(null)
  const [cssApplied, setCssApplied] = useState(false)
  const containerRef = useRef<HTMLDivElement>(null)
  const relativePath = file.folder === '' ? file.name : `${file.folder}/${file.name}`

  useEffect(() => {
    let canceled = false
    let styleNode: HTMLStyleElement | null = null

    void (async () => {
      try {
        const [content, configRaw, cssRaw] = await Promise.all([
          (async () => {
            const { readProjectFile } = await import('../../infrastructure/nativeClient')
            const { content } = await readProjectFile(scan.projectPath, relativePath)
            return content
          })(),
          readFirstAvailable(scan.projectPath, configCandidatesFor(file)),
          readFirstAvailable(scan.projectPath, cssCandidatesFor(file)),
        ])
        if (canceled) return

        const { config, warningKey, warning } = parseDiagramConfig(configRaw)
        if (!canceled) {
          setConfigWarningKey(warningKey)
          setConfigWarningArg(warning)
        }

        const mermaid = await getMermaid()
        // Re-initialize on every render so one diagram's config never leaks
        // into the next; securityLevel stays forced for the native bridge.
        mermaid.initialize({
          startOnLoad: false,
          securityLevel: 'strict',
          ...(config ?? {}),
        })
        const { svg: rendered } = await mermaid.render(
          `preview-${crypto.randomUUID()}`,
          content,
        )
        if (!canceled) {
          setSvg(rendered)
          if (cssRaw !== null && cssRaw.trim() !== '') {
            styleNode = document.createElement('style')
            styleNode.textContent = cssRaw
            document.head.appendChild(styleNode)
            setCssApplied(true)
          }
        }
      } catch (e) {
        if (!canceled) {
          setError(e instanceof Error ? e.message : t('diagrams.renderError'))
        }
      }
    })()

    return () => {
      canceled = true
      styleNode?.remove()
    }
  }, [scan.projectPath, relativePath, file])

  if (error) {
    return <p className="text-xs text-red-400 px-2 py-1">{error}</p>
  }
  if (svg === null) {
    return (
      <div className="flex items-center gap-2 px-2 py-2 text-xs text-ink-400">
        <span className="inline-block w-2.5 h-2.5 border-2 border-brass-500 border-t-transparent rounded-full animate-spin" />
        {t('diagrams.rendering')}
      </div>
    )
  }
  return (
    <div>
      {configWarningKey && (
        <p className="text-xs text-amber-400 px-3 pt-2">
          {t(configWarningKey, configWarningArg ? { message: configWarningArg } : {})}
        </p>
      )}
      <div
        ref={containerRef}
        className="px-3 py-2 overflow-x-auto bg-ink-950/60 rounded"
        dangerouslySetInnerHTML={{ __html: svg }}
      />
      {cssApplied && (
        <p className="text-[11px] text-ink-400 px-3 py-1">
          {t('diagrams.cssApplied')}
        </p>
      )}
    </div>
  )
}
