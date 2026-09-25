import { useCallback, useEffect, useRef, useState } from 'react';
import { cancelCompile, compileStatus, startCompile } from '../infrastructure/nativeClient';
import { onNativeEvent } from '../infrastructure/nativeEvents';
import type { CompileJobState, CompileStatus, StageKey } from '../domain/ipc';

const FALLBACK_POLL_MS = 1000;

const IDLE_STATUS: CompileStatus = {
  state: 'idle',
  progress: 0,
  message: '',
  stage: '',
  stageKey: '',
  stagesReached: [],
  failedStageKey: '',
  stageDurationsMs: {},
  currentFile: '',
  outputPath: '',
  logTail: '',
  notice: '',
};

type OutputEvent = { line?: string, partial?: string }
type ProgressEvent = { progress: number, stage?: string, stageKey?: StageKey, stagesReached?: StageKey[], file?: string }

export function useCompiler() {
  const [status, setStatus] = useState<CompileStatus>(IDLE_STATUS);
  const [error, setError] = useState<string | null>(null);
  const pollRef = useRef<number | null>(null);
  const logLinesRef = useRef<string[]>([]);
  const lastLogFlushRef = useRef(0);

  const isRunning = status.state === 'running';

  const poll = useCallback(async (): Promise<CompileJobState> => {
    try {
      const next = await compileStatus();
      setStatus(next);
      return next.state;
    } catch (e) {
      setError(e instanceof Error ? e.message : 'Error consultando el estado.');
      return 'idle';
    }
  }, []);

  useEffect(() => {
    // Pushed events deliver output and progress instantly; keep the local log
    // mirror in sync and refresh the authoritative status on finish.
    const unsubscribe = onNativeEvent((topic, payload) => {
      if (topic === 'compile_output') {
        try {
          const event = JSON.parse(payload) as OutputEvent;
          const text = event.line ?? event.partial;
          if (text === undefined) {
            return;
          }
          logLinesRef.current.push(text);
          if (logLinesRef.current.length > 400) {
            logLinesRef.current.splice(0, logLinesRef.current.length - 400);
          }
          // Joining + rendering the log is the expensive part; throttle the
          // visible tail to 100 ms while keeping every line in the buffer.
          const now = Date.now();
          if (now - lastLogFlushRef.current >= 100) {
            lastLogFlushRef.current = now;
            const tail = logLinesRef.current.join('\n') + '\n';
            setStatus(previous => ({ ...previous, logTail: tail }));
          }
        } catch {
          // Malformed payloads are ignored; polling still recovers the log.
        }
        return;
      }
      if (topic === 'compile_progress') {
        try {
          const event = JSON.parse(payload) as ProgressEvent;
          setStatus(previous => ({
            ...previous,
            progress: Math.max(previous.progress, event.progress),
            stage: event.stage ?? previous.stage,
            stageKey: event.stageKey ?? previous.stageKey,
            stagesReached: event.stagesReached ?? previous.stagesReached,
            currentFile: event.file ?? previous.currentFile,
          }));
        } catch {
          // Progress is cosmetic; the next poll repairs any drift.
        }
        return;
      }
      if (topic === 'compile_success' || topic === 'compile_error' || topic === 'compile_canceled') {
        void poll();
      }
    });
    return unsubscribe;
  }, [poll]);

  useEffect(() => {
    // Fallback polling while a job runs, in case any push event is lost.
    if (!isRunning) {
      return;
    }
    pollRef.current = window.setInterval(() => {
      void poll();
    }, FALLBACK_POLL_MS);
    return () => {
      if (pollRef.current !== null) {
        window.clearInterval(pollRef.current);
        pollRef.current = null;
      }
    };
  }, [isRunning, poll]);

  const compile = useCallback(
    async (projectPath: string, mainFile: string) => {
      setError(null);
      logLinesRef.current = [];
      try {
        await startCompile(projectPath, mainFile);
        await poll();
      } catch (e) {
        setError(e instanceof Error ? e.message : 'No se pudo iniciar la compilación.');
      }
    },
    [poll],
  );

  const cancel = useCallback(async () => {
    try {
      await cancelCompile();
      await poll();
    } catch (e) {
      setError(e instanceof Error ? e.message : 'No se pudo cancelar.');
    }
  }, [poll]);

  return { status, isRunning, error, compile, cancel };
}
