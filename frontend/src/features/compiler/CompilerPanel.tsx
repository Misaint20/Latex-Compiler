import { useEffect, useRef, useState } from 'react';
import { useTranslation } from 'react-i18next';
import { useCompiler } from '../../application/useCompiler';
import { compilerEngines, setCompileEngine, openCompiledOutput, getNotificationsEnabled, setNotificationsEnabled } from '../../infrastructure/nativeClient';
import { STAGE_ORDER, type StageKey, type EnginesResult } from '../../domain/ipc';

const formatDuration = (ms: number) =>
  ms < 1000 ? `${Math.max(1, Math.round(ms))} ms` : ms < 60000 ? `${(ms / 1000).toFixed(1)} s` : `${Math.floor(ms / 60000)} min ${Math.round((ms % 60000) / 1000)} s`;

function StageChips({
  reached,
  currentKey,
  failedKey,
  durations,
  showDurations,
}: {
  reached: StageKey[]
  currentKey: StageKey | ''
  failedKey: StageKey | ''
  durations: Partial<Record<StageKey, number>>
  showDurations: boolean
}) {
  const { t } = useTranslation();
  return (
    <div className="flex flex-wrap gap-1.5 mt-4">
      {STAGE_ORDER.map(key => {
        const failed = key === failedKey;
        const done = !failed && reached.includes(key) && key !== currentKey;
        const active = !failed && key === currentKey;
        const tone = failed
          ? 'border-red-500 bg-red-500/15 text-red-300'
          : done
            ? 'border-emerald-700/60 bg-emerald-500/10 text-emerald-300'
            : active
              ? 'border-brass-500 bg-brass-500/15 text-brass-200'
              : 'border-ink-600 bg-ink-800/60 text-ink-400';
        const ms = durations[key];
        const showMs = showDurations && typeof ms === 'number' && ms >= 0 && (done || failed);
        return (
          <span
            key={key}
            className={`px-2 py-0.5 rounded-full border text-[11px] font-medium transition-colors ${tone}`}
          >
            {failed ? '✕ ' : done ? '✓ ' : active ? '▸ ' : ''}{t(`stages.${key}`)}
            {showMs && <span className="ml-1.5 text-ink-300 font-normal">{formatDuration(ms)}</span>}
          </span>
        );
      })}
    </div>
  );
}

export function CompilerPanel({ projectPath, mainFile }: { projectPath: string, mainFile: string }) {
  const { status, isRunning, error, compile, cancel } = useCompiler();
  const { t } = useTranslation();
  const logRef = useRef<HTMLPreElement>(null);
  const [openError, setOpenError] = useState<string | null>(null);
  const [engines, setEngines] = useState<EnginesResult | null>(null);
  const [copied, setCopied] = useState(false);
  const [notificationsOn, setNotificationsOn] = useState(true);

  useEffect(() => {
    let cancelled = false;
    void compilerEngines().then(
      result => {
        if (!cancelled) setEngines(result);
      },
      () => {
        // The panel still works with the default engine when metadata fails.
      },
    );
    void getNotificationsEnabled().then(enabled => {
      if (!cancelled) setNotificationsOn(enabled);
    });
    return () => {
      cancelled = true;
    };
  }, []);

  const toggleNotifications = async (enabled: boolean) => {
    // Optimistic: the checkbox flips immediately; persistence is silent.
    setNotificationsOn(enabled);
    try {
      await setNotificationsEnabled(enabled);
    } catch {
      // Revert on failure so the UI keeps telling the truth.
      setNotificationsOn(!enabled);
    }
  };

  const changeEngine = async (engine: string) => {
    try {
      await setCompileEngine(engine);
      setEngines(previous => (previous ? { ...previous, preferred: engine } : previous));
    } catch {
      // Keep the previous selection; the next poll of settings will show it.
    }
  };

  const copyLog = async () => {
    try {
      await navigator.clipboard.writeText(status.logTail);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2000);
    } catch {
      // Clipboard can be unavailable; the log stays visible for manual copy.
    }
  };

  const finished = status.state === 'succeeded' || status.state === 'failed' || status.state === 'canceled';
  const showLog = status.logTail.trim() !== '' && (isRunning || finished);
  const showChips = isRunning || (finished && status.stagesReached.length > 0);

  useEffect(() => {
    const log = logRef.current;
    if (log && isRunning) {
      log.scrollTop = log.scrollHeight;
    }
  }, [status.logTail, isRunning]);

  const failedStageLabel = STAGE_ORDER.find(key => key === status.failedStageKey);

  const openPdf = async () => {
    setOpenError(null);
    try {
      await openCompiledOutput();
    } catch (e) {
      setOpenError(e instanceof Error ? e.message : t('compiler.openPdfError'));
    }
  };

  return (
    <div className="mt-6 border border-ink-600 rounded-lg p-6 bg-ink-800">
      <div className="flex items-center justify-between mb-4">
        <h2 className="text-xl font-semibold text-white">{t('compiler.title')}</h2>
        {isRunning && (
          <span className="flex items-center gap-2 text-sm text-brass-300">
            <span className="inline-block w-3 h-3 border-2 border-brass-400 border-t-transparent rounded-full animate-spin" />
            {(status.stage || status.message || t('compiler.compiling')) + ` (${status.progress}%)`}
          </span>
        )}
      </div>

      <div className="flex flex-wrap items-center gap-2">
        <button
          onClick={() => compile(projectPath, mainFile)}
          disabled={isRunning || !projectPath || !mainFile}
          className="px-4 py-2 bg-brass-500 hover:bg-brass-400 disabled:bg-ink-600 text-ink-950 rounded font-medium transition-colors"
        >
          {isRunning ? t('compiler.compiling') : t('compiler.compile')}
        </button>
        {isRunning && (
          <button
            onClick={cancel}
            className="px-4 py-2 bg-red-700 hover:bg-red-600 text-white rounded font-medium transition-colors"
          >
            {t('compiler.cancel')}
          </button>
        )}
        {engines && engines.engines.length > 0 && (
          <select
            value={engines.preferred || 'auto'}
            onChange={event => void changeEngine(event.target.value)}
            disabled={isRunning}
            title={t('compiler.engineHint')}
            className="ml-auto px-2 py-1.5 bg-ink-900 border border-ink-600 rounded text-sm text-parchment disabled:opacity-50"
          >
            <option value="auto">{t('compiler.engineAuto')}</option>
            {engines.engines.map(engine => (
              <option key={engine.id} value={engine.id} disabled={!engine.installed}>
                {engine.title}
                {engine.installed ? '' : ` (${t('compiler.engineMissing')})`}
              </option>
            ))}
          </select>
        )}
      </div>

      <label className="mt-2 flex items-center gap-2 text-sm text-parchment/80 cursor-pointer select-none">
        <input
          type="checkbox"
          checked={notificationsOn}
          onChange={event => void toggleNotifications(event.target.checked)}
          className="accent-brass-500 w-4 h-4"
        />
        {t('compiler.notificationsToggle')}
      </label>

      {showChips && (
        <StageChips
          reached={status.stagesReached}
          currentKey={isRunning ? status.stageKey : ''}
          failedKey={status.state === 'failed' ? status.failedStageKey : ''}
          durations={status.stageDurationsMs}
          showDurations={!isRunning}
        />
      )}

      {isRunning && (
        <div className="mt-3 h-2 w-full bg-ink-700 rounded overflow-hidden">
          <div
            className="h-full bg-brass-500 transition-all duration-300"
            style={{ width: `${Math.max(status.progress, 4)}%` }}
          />
        </div>
      )}

      {/* Non-fatal compile warnings (diagram toolchain, engine fallback).
          Persist through the whole job: a degraded success must never be
          silent — the user sees what was affected without reading the log. */}
      {status.notice && (
        <div
          role="status"
          className="mt-3 flex items-start gap-2 p-3 rounded bg-amber-900/30 border border-amber-800/70"
        >
          <svg
            aria-hidden
            viewBox="0 0 16 16"
            className="mt-0.5 w-4 h-4 shrink-0 text-amber-400"
            fill="currentColor"
          >
            <path d="M8 1.5 15 14H1L8 1.5Zm0 4a.75.75 0 0 0-.75.75v3a.75.75 0 0 0 1.5 0v-3A.75.75 0 0 0 8 5.5Zm0 6.25a1 1 0 1 0 0 2 1 1 0 0 0 0-2Z" />
          </svg>
          <p className="text-amber-300 text-sm">{status.notice}</p>
        </div>
      )}

      {error && <div className="mt-4 text-red-400">Error: {error}</div>}
      {openError && <div className="mt-4 text-red-400">{openError}</div>}

      {isRunning && status.currentFile && (
        <p className="mt-3 text-xs text-ink-300 font-mono truncate" title={status.currentFile}>
          {t('compiler.currentFile', { file: status.currentFile })}
        </p>
      )}

      {showLog && (
        <div className="mt-4">
          <div className="flex justify-end mb-1">
            <button
              onClick={() => void copyLog()}
              className="px-2 py-1 text-xs btn-ghost"
            >
              {copied ? t('compiler.logCopied') : t('compiler.copyLog')}
            </button>
          </div>
          <pre
            ref={logRef}
            className="max-h-48 overflow-y-auto text-xs text-ink-300 bg-ink-950/70 rounded p-3 whitespace-pre-wrap font-mono"
          >
            {status.logTail}
          </pre>
        </div>
      )}

      {finished && (
        <div
          className={`mt-4 p-4 rounded ${
            status.state === 'succeeded'
              ? 'bg-emerald-900/30 border border-emerald-800'
              : 'bg-red-900/30 border border-red-800'
          }`}
        >
          <div className="flex flex-wrap items-center justify-between gap-3">
            <h4
              className={`font-semibold ${
                status.state === 'succeeded' ? 'text-emerald-400' : 'text-red-400'
              }`}
            >
              {status.state === 'succeeded' ? t('compiler.success') : status.state === 'failed' ? t('compiler.failed') : t('compiler.canceled')}
            </h4>
            {status.state === 'succeeded' && (
              <button
                onClick={openPdf}
                className="px-3 py-1.5 bg-emerald-700 hover:bg-emerald-600 text-white rounded text-sm font-medium transition-colors"
              >
                {t('compiler.openPdf')}
              </button>
            )}
          </div>
          {status.state === 'succeeded' && (
            <p className="text-parchment/70 text-sm mt-1 font-mono break-all">{status.outputPath}</p>
          )}
          {status.state === 'failed' && failedStageLabel && (
            <p className="text-red-300 text-sm mt-1">
              {t('compiler.failedAtStage')}<span className="font-semibold">{t(`stages.${failedStageLabel}`)}</span>
            </p>
          )}
          {status.state === 'failed' && status.message && (
            <p className="text-red-300 text-sm mt-1">{status.message}</p>
          )}
        </div>
      )}
    </div>
  );
}
