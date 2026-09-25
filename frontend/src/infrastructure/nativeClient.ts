import {
  protocolVersion,
  type EnginesResult,
  type NativeRequest,
  type NativeResponse,
  type PingResult,
  type CompileResult,
  type CompileStartResult,
  type CompileStatus,
  type CompileCancelResult,
  type ProjectScanResult,
  type FolderPickResult,
  type ReadFileResult,
  type ReadBinaryResult,
  type OpenFileResult,
  type DiagramRenderResult,
  type CompileHistoryResult,
  type RecentProjectsResult,
  type RecordRecentResult,
  type RemoveRecentResult,
  type EditorPresetsResult,
  type EditorPreference,
  type EditorSetResult,
  type RemoveMissingRecentsResult,
  type RememberMainFileResult
} from '../domain/ipc'
import i18n from '../i18n'

type NativeBridge = (request: NativeRequest) => Promise<NativeResponse<unknown>>

declare global {
  interface Window {
    latexCompiler?: NativeBridge
  }
}

function bridge(): NativeBridge {
  if (!window.latexCompiler) {
    throw new Error(i18n.t('bridge.unavailable'))
  }
  return window.latexCompiler
}

async function request<T>(req: NativeRequest, timeoutMs?: number): Promise<T> {
  // Bounded wait for quick calls; dialogs the user may keep open (pickFolder)
  // wait unbounded.
  const call = bridge()(req);
  if (timeoutMs) {
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<never>((_, reject) => {
      timer = setTimeout(() => reject(new Error('request timed out')), timeoutMs);
    });
    try {
      const response = await Promise.race([call, timeout]);
      if (!response.ok) {
        throw new Error(response.error.message);
      }
      return response.result as T;
    } finally {
      clearTimeout(timer);
    }
  }
  const response = await call;
  if (!response.ok) {
    throw new Error(response.error.message);
  }
  return response.result as T;
}

export async function readProjectFileBinary(
  projectPath: string,
  relativePath: string,
): Promise<ReadBinaryResult> {
  return request<ReadBinaryResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.readProjectFileBinary',
    params: { projectPath, relativePath }
  });
}

export async function setNativeProjectMenuEnabled(enabled: boolean): Promise<void> {
  await request({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'app.setProjectMenuEnabled',
    params: { enabled }
  }).catch(() => {
    // On platforms without a native menu this is a no-op.
  });
}

export async function getAppLanguage(): Promise<string | null> {
  try {
    const result = await request<{ language?: string }>({
      version: protocolVersion,
      id: crypto.randomUUID(),
      method: 'app.getLanguage',
      params: {},
    }, 10000)
    return result.language ?? null
  } catch {
    return null
  }
}

export async function setAppLanguage(code: string): Promise<void> {
  await request({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'app.setLanguage',
    params: { language: code },
  }, 10000)
}

export async function getNotificationsEnabled(): Promise<boolean> {
  try {
    const result = await request<{ enabled?: boolean }>({
      version: protocolVersion,
      id: crypto.randomUUID(),
      method: 'app.getNotifications',
      params: {},
    }, 10000)
    return result.enabled ?? true
  } catch {
    // Default on when the bridge is not ready yet.
    return true
  }
}

export async function setNotificationsEnabled(enabled: boolean): Promise<void> {
  await request({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'app.setNotifications',
    params: { enabled },
  }, 10000)
}

export async function ping(): Promise<PingResult> {
  return request<PingResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'app.ping',
    params: {},
  }, 10000)
}

export async function pickFolder(startPath?: string): Promise<FolderPickResult> {
  return request<FolderPickResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.pickFolder',
    params: startPath ? { startPath } : {},
  })
}

export async function startCompile(projectPath: string, mainFile: string): Promise<CompileStartResult> {
  return request<CompileStartResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.start',
    params: { projectPath, mainFile }
  });
}

export async function compileStatus(): Promise<CompileStatus> {
  return request<CompileStatus>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.status',
    params: {}
  }, 10000);
}

export async function cancelCompile(): Promise<CompileCancelResult> {
  return request<CompileCancelResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.cancel',
    params: {}
  });
}

export async function readProjectFile(projectPath: string, relativePath: string): Promise<ReadFileResult> {
  return request<ReadFileResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.readFile',
    params: { projectPath, relativePath }
  });
}

export async function writeProjectTextFile(
  projectPath: string,
  relativePath: string,
  content: string,
): Promise<{ saved: boolean }> {
  return request<{ saved: boolean }>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.writeProjectFile',
    params: { projectPath, relativePath, content }
  });
}

export async function openProjectFileWith(
  projectPath: string,
  relativePath: string,
  preset: string,
): Promise<{ opened: boolean }> {
  return request<{ opened: boolean }>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.openFileWith',
    params: { projectPath, relativePath, preset }
  });
}

export async function openProjectFile(projectPath: string, relativePath: string): Promise<OpenFileResult> {
  return request<OpenFileResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.openFile',
    params: { projectPath, relativePath }
  });
}

export async function openCompiledOutput(): Promise<OpenFileResult> {
  return request<OpenFileResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.openOutput',
    params: {}
  });
}

export async function recentProjects(): Promise<RecentProjectsResult> {
  return request<RecentProjectsResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.recentProjects',
    params: {}
  });
}

export async function recordRecentProject(path: string): Promise<RecordRecentResult> {
  return request<RecordRecentResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.recordRecent',
    params: { path }
  });
}

export async function removeRecentProject(path: string): Promise<RemoveRecentResult> {
  return request<RemoveRecentResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.removeRecent',
    params: { path }
  });
}

export async function editorPresets(): Promise<EditorPresetsResult> {
  return request<EditorPresetsResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'editor.presets',
    params: {}
  }, 10000);
}

// Hidden diagnostics call, for tests and manual refresh: drops the presets
// TTL cache so the next editorPresets() re-probes the disk synchronously.
export async function invalidateEditorPresetsCache(): Promise<{ invalidated: boolean }> {
  return request<{ invalidated: boolean }>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'editor.invalidatePresetsCache',
    params: {}
  });
}

export type EditorScope = 'editor' | 'diagram'

export async function editorPreference(scope: EditorScope = 'editor'): Promise<EditorPreference> {
  return request<EditorPreference>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'editor.get',
    params: scope === 'diagram' ? { scope } : {}
  });
}

export async function setEditorPreference(
  preferred: string,
  customTemplate: string,
  scope: EditorScope = 'editor',
): Promise<EditorSetResult> {
  return request<EditorSetResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'editor.set',
    params: scope === 'diagram' ? { preferred, customTemplate, scope } : { preferred, customTemplate }
  });
}

export async function removeMissingRecents(): Promise<RemoveMissingRecentsResult> {
  return request<RemoveMissingRecentsResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.removeMissingRecents',
    params: {}
  });
}

export async function rememberLastTab(projectPath: string, tab: string): Promise<{ saved: boolean }> {
  return request<{ saved: boolean }>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.rememberLastTab',
    params: { projectPath, tab }
  });
}

export async function rememberMainFile(projectPath: string, mainFile: string): Promise<RememberMainFileResult> {
  return request<RememberMainFileResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.rememberMainFile',
    params: { projectPath, mainFile }
  });
}

export async function compileHistory(projectPath: string): Promise<CompileHistoryResult> {
  return request<CompileHistoryResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.history',
    params: { projectPath }
  });
}

export async function compileProject(projectPath: string, mainFile: string): Promise<CompileResult> {
  return request<CompileResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.compile',
    params: { projectPath, mainFile }
  });
}

export async function scanProject(projectPath: string): Promise<ProjectScanResult> {
  return request<ProjectScanResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'project.scan',
    params: { projectPath }
  });
}

export async function compilerEngines(): Promise<EnginesResult> {
  return request<EnginesResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.engines',
    params: {}
  }, 10000);
}

export async function setCompileEngine(engine: string): Promise<{ saved: boolean }> {
  return request<{ saved: boolean }>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'compiler.setEngine',
    params: { engine }
  }, 10000);
}

export async function renderDiagram(code: string, type: string, format: string): Promise<DiagramRenderResult> {
  return request<DiagramRenderResult>({
    version: protocolVersion,
    id: crypto.randomUUID(),
    method: 'diagram.render',
    params: { code, type, format }
  });
}
