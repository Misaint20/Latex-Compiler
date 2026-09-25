import { useEffect, useState } from 'react'
import { useTranslation } from 'react-i18next'
import type { ProjectFile } from '../../domain/ipc'
import { readProjectFileBinary } from '../../infrastructure/nativeClient'

const IMAGE_MIMES = new Set([
  'image/png',
  'image/jpeg',
  'image/gif',
  'image/webp',
  'image/bmp',
  'image/svg+xml',
])

function isPreviewable(file: ProjectFile): boolean {
  return /\.(png|jpe?g|gif|webp|bmp|svg|pdf)$/i.test(file.name)
}

// Renders an image or PDF asset inline. Images come over the bridge as base64
// data URLs; PDFs render through an object URL in an embed. Only reads files
// below ~25 MB so scanning the section stays responsive.
const MAX_PREVIEW_BYTES = 25 * 1024 * 1024

export function AssetPreview({ scan, file }: { scan: { projectPath: string }, file: ProjectFile }) {
  const { t } = useTranslation()
  const [url, setUrl] = useState<string | null>(null)
  const [kind, setKind] = useState<'image' | 'pdf' | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [tooLarge, setTooLarge] = useState(false)
  const relativePath = file.folder === '' ? file.name : `${file.folder}/${file.name}`

  useEffect(() => {
    let canceled = false
    let objectUrl: string | null = null

    if (file.sizeBytes > MAX_PREVIEW_BYTES) {
      setTooLarge(true)
      return
    }
    setTooLarge(false)

    void (async () => {
      try {
        const { mime, dataBase64 } = await readProjectFileBinary(scan.projectPath, relativePath)
        if (canceled) return
        if (IMAGE_MIMES.has(mime)) {
          setKind('image')
          setUrl(`data:${mime};base64,${dataBase64}`)
        } else if (mime === 'application/pdf') {
          const binary = atob(dataBase64)
          const bytes = new Uint8Array(binary.length)
          for (let i = 0; i < binary.length; i++) {
            bytes[i] = binary.charCodeAt(i)
          }
          const blob = new Blob([bytes], { type: 'application/pdf' })
          objectUrl = URL.createObjectURL(blob)
          setKind('pdf')
          setUrl(objectUrl)
        } else {
          setError(t('assetsPreview.unsupported'))
        }
      } catch (e) {
        if (!canceled) {
          setError(e instanceof Error ? e.message : t('assetsPreview.loadError'))
        }
      }
    })()

    return () => {
      canceled = true
      if (objectUrl) {
        URL.revokeObjectURL(objectUrl)
      }
    }
  }, [scan.projectPath, relativePath, file.sizeBytes, t])

  if (tooLarge) {
    return (
      <p className="px-3 py-2 text-xs text-ink-400">
        {t('assetsPreview.tooLarge')}
      </p>
    )
  }
  if (error) {
    return <p className="px-3 py-2 text-xs text-red-400">{error}</p>
  }
  if (!url || !kind) {
    return (
      <div className="flex items-center gap-2 px-3 py-2 text-xs text-ink-400">
        <span className="inline-block w-2.5 h-2.5 border-2 border-brass-500 border-t-transparent rounded-full animate-spin" />
        {t('assetsPreview.loading')}
      </div>
    )
  }
  if (kind === 'image') {
    return (
      <div className="flex justify-center px-3 py-2 bg-ink-950/60 rounded">
        <img src={url} alt={file.name} className="max-w-full max-h-96 object-contain" />
      </div>
    )
  }
  return (
    <div className="px-3 py-2">
      <object data={url} type="application/pdf" className="w-full h-96 rounded">
        <p className="text-xs text-ink-400">
          {t('assetsPreview.pdfEmbedBlocked')}{' '}
          <button onClick={() => window.open(url, '_blank')} className="text-brass-400 hover:text-brass-300">
            {t('assetsPreview.pdfOpenExternal')}
          </button>
        </p>
      </object>
    </div>
  )
}

export { isPreviewable }
