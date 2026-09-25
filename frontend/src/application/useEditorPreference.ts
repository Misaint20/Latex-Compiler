import { useCallback, useEffect, useState } from 'react';
import {
  editorPreference,
  editorPresets,
  setEditorPreference,
  type EditorScope,
} from '../infrastructure/nativeClient';
import { onNativeEvent } from '../infrastructure/nativeEvents';
import { emitEditorPreferenceChange, onEditorPreferenceChange } from './preferenceEvents';
import type { EditorPreset } from '../domain/ipc';

export function useEditorPreference(scope: EditorScope = 'editor') {
  const [presets, setPresets] = useState<EditorPreset[]>([]);
  const [availability, setAvailability] = useState(true);
  const [preferred, setPreferredState] = useState<string>('');
  const [customTemplate, setCustomTemplateState] = useState<string>('');
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    const load = () => {
      void Promise.all([editorPresets(), editorPreference(scope)]).then(
        ([presetsResult, preference]) => {
          if (cancelled) {
            return;
          }
          setPresets(presetsResult.presets);
          setAvailability(presetsResult.availability !== false);
          setPreferredState(preference.preferred);
          setCustomTemplateState(preference.customTemplate);
        },
        () => {
          if (!cancelled) {
            setError('No se pudieron cargar los editores disponibles.');
          }
        },
      );
    };
    load();

    // Stale-cached presets are refreshed in the background by the service;
    // this push tells the UI the fresh values are ready.
    const unsubscribe = onNativeEvent((topic: string) => {
      if (topic === 'editor.presetsChanged' && !cancelled) {
        void editorPresets().then(result => {
          if (!cancelled) {
            setPresets(result.presets);
            setAvailability(result.availability !== false);
          }
        });
      }
    });
    // When another picker instance saves a preference, refresh this one so
    // both scopes stay consistent without remounting.
    const unsubscribePreference = onEditorPreferenceChange(nextScope => {
      if (nextScope === scope && !cancelled) {
        load();
        return;
      }
      if (!cancelled) {
        void editorPresets().then(result => {
          if (!cancelled) {
            setPresets(result.presets);
            setAvailability(result.availability !== false);
          }
        });
      }
    });
    return () => {
      cancelled = true;
      unsubscribe();
      unsubscribePreference();
    };
  }, [scope]);

  const save = useCallback(
    async (preferredId: string, template: string) => {
      setSaving(true);
      setError(null);
      try {
        await setEditorPreference(preferredId, template, scope);
        setPreferredState(preferredId);
        setCustomTemplateState(template);
        emitEditorPreferenceChange(scope);
      } catch (e) {
        setError(e instanceof Error ? e.message : 'No se pudo guardar la preferencia.');
      } finally {
        setSaving(false);
      }
    },
    [scope],
  );

  return { presets, availability, preferred, customTemplate, saving, error, save };
}
