export const protocolVersion = 1 as const

export type NativeRequest =
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.ping'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.engines'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.setEngine'
      params: { engine: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.pickFolder'
      params: { startPath?: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.recentProjects'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.recordRecent'
      params: { path: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.removeRecent'
      params: { path: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.removeMissingRecents'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.rememberMainFile'
      params: { projectPath: string, mainFile: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.rememberLastTab'
      params: { projectPath: string, tab: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.compile'
      params: { projectPath: string, mainFile: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.start'
      params: { projectPath: string, mainFile: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.status'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.cancel'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.scan'
      params: { projectPath: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.readFile'
      params: { projectPath: string, relativePath: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.writeProjectFile'
      params: { projectPath: string, relativePath: string, content: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.openFile'
      params: { projectPath: string, relativePath: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.openFileWith'
      params: { projectPath: string, relativePath: string, preset: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.openOutput'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'compiler.history'
      params: { projectPath: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'diagram.render'
      params: { code: string, type: string, format: string, scale?: number }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'editor.presets'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'editor.get'
      params: { scope?: 'diagram' }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'editor.set'
      params: { preferred: string, customTemplate?: string, scope?: 'diagram' }
    }
  | {
      // Hidden diagnostics method, for tests and manual refresh.
      version: typeof protocolVersion
      id: string
      method: 'editor.invalidatePresetsCache'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'project.readProjectFileBinary'
      params: { projectPath: string, relativePath: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.getLanguage'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.setProjectMenuEnabled'
      params: { enabled: boolean }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.setLanguage'
      params: { language: string }
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.getNotifications'
      params: Record<string, never>
    }
  | {
      version: typeof protocolVersion
      id: string
      method: 'app.setNotifications'
      params: { enabled: boolean }
    }

export type NativeError = {
  code: string
  message: string
}

export type NativeResponse<T> =
  | { version: typeof protocolVersion; id: string; ok: true; result: T }
  | { version: typeof protocolVersion; id: string; ok: false; error: NativeError }

export type PingResult = { message: 'pong' }

export type CompileResult = {
  success: boolean
  outputPath: string
  errorMessage: string
}

export type CompileJobState = 'idle' | 'running' | 'succeeded' | 'failed' | 'canceled'

export type CompileStartResult = {
  started: boolean
}

export type StageKey = 'download' | 'tex' | 'rerun' | 'assemble' | 'write'

export const STAGE_ORDER: StageKey[] = ['download', 'tex', 'rerun', 'assemble', 'write']

export const STAGE_LABELS: Record<StageKey, string> = {
  download: 'Paquetes',
  tex: 'TeX',
  rerun: 'Pasada 2',
  assemble: 'Ensamblado',
  write: 'Salida',
}

export type CompileStatus = {
  state: CompileJobState
  progress: number
  message: string
  stage: string
  stageKey: StageKey | ''
  stagesReached: StageKey[]
  failedStageKey: StageKey | ''
  stageDurationsMs: Partial<Record<StageKey, number>>
  currentFile: string
  outputPath: string
  logTail: string
  notice: string
}

export type EngineInfoIpc = {
  id: string
  title: string
  installed: boolean
}

export type EnginesResult = {
  engines: EngineInfoIpc[]
  preferred: string
  fallback: string
}

export type CompileCancelResult = {
  canceled: boolean
}

export type ProjectFile = {
  name: string
  folder: string
  sizeBytes: number
}

export type ProjectScanResult = {
  projectPath: string
  texFiles: string[]
  mainFileCandidate: string
  savedMainFile: string
  savedLastTab: string
  chapters: ProjectFile[]
  diagrams: ProjectFile[]
  styles: ProjectFile[]
  assets: ProjectFile[]
}

export type FolderPickResult = {
  path: string
}

export type RecentProject = {
  path: string
  lastOpenedMs: number
  exists: boolean
  lastTab: string
}

export type RecentProjectsResult = {
  projects: RecentProject[]
}

export type RemoveRecentResult = {
  removed: boolean
}

export type RemoveMissingRecentsResult = {
  removed: number
}

export type RememberMainFileResult = {
  saved: boolean
}

export type RecordRecentResult = {
  recorded: boolean
}

export type EditorPreset = {
  id: string
  label: string
  installed: boolean
}

export type EditorPresetsResult = {
  presets: EditorPreset[]
  availability: boolean
}

export type EditorPreference = {
  preferred: string
  customTemplate: string
}

export type EditorSetResult = {
  saved: boolean
}

export type ReadBinaryResult = {
  mime: string
  dataBase64: string
}

export type ReadFileResult = {
  content: string
}

export type DiagramRenderResult = {
  success: boolean
  data: string
  errorMessage: string
}

export type OpenFileResult = {
  opened: boolean
}

export type HistoryEntry = {
  timestampMs: number
  durationMs: number
  result: 'succeeded' | 'failed' | 'canceled'
  mainFile: string
  outputPath: string
  errorMessage: string
  stagesReached: StageKey[]
  stageDurationsMs: Partial<Record<StageKey, number>>
}

export type CompileHistoryResult = {
  entries: HistoryEntry[]
}
