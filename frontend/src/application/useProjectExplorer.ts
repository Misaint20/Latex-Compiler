import { useCallback, useState } from 'react';
import { pickFolder, scanProject } from '../infrastructure/nativeClient';
import type { ProjectScanResult } from '../domain/ipc';

export type ExplorerPhase = 'welcome' | 'browsing' | 'exploring';

export function useProjectExplorer() {
  const [phase, setPhase] = useState<ExplorerPhase>('welcome');
  const [scan, setScan] = useState<ProjectScanResult | null>(null);
  const [isBusy, setIsBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const openPath = useCallback(async (path: string) => {
    setIsBusy(true);
    setError(null);
    try {
      const result = await scanProject(path);
      setScan(result);
      setPhase('exploring');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'No se pudo abrir la carpeta.');
    } finally {
      setIsBusy(false);
    }
  }, []);

  const browse = useCallback(async () => {
    setIsBusy(true);
    setError(null);
    try {
      const { path } = await pickFolder(scan?.projectPath);
      if (!path) {
        return;
      }
      const result = await scanProject(path);
      setScan(result);
      setPhase('exploring');
    } catch (e) {
      setError(e instanceof Error ? e.message : 'No se pudo abrir la carpeta.');
    } finally {
      setIsBusy(false);
    }
  }, [scan?.projectPath]);

  const rescan = useCallback(async (projectPath: string) => {
    setIsBusy(true);
    setError(null);
    try {
      const result = await scanProject(projectPath);
      setScan(result);
    } catch (e) {
      setError(e instanceof Error ? e.message : 'No se pudo escanear el proyecto.');
    } finally {
      setIsBusy(false);
    }
  }, []);

  const backToWelcome = useCallback(() => {
    setPhase('welcome');
    setScan(null);
    setError(null);
  }, []);

  return { phase, scan, isBusy, error, openPath, browse, rescan, backToWelcome };
}
