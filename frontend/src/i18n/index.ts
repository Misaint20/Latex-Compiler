import i18n from 'i18next'
import { initReactI18next } from 'react-i18next'
import type { BackendModule, ReadCallback } from 'i18next'

import en from './locales/en.json'

export const LANGUAGE_KEY = 'app.language'

export const LANGUAGES: { code: string; label: string }[] = [
  { code: 'es-MX', label: 'Español (México)' },
  { code: 'en', label: 'English' },
  { code: 'pt-BR', label: 'Português (Brasil)' },
  { code: 'fr', label: 'Français' },
  { code: 'de', label: 'Deutsch' },
  { code: 'it', label: 'Italiano' },
  { code: 'ja', label: '日本語' },
  { code: 'zh-Hans', label: '简体中文' },
  { code: 'ko', label: '한국어' },
  { code: 'ru', label: 'Русский' },
  { code: 'uk', label: 'Українська' },
  { code: 'pl', label: 'Polski' },
  { code: 'tr', label: 'Türkçe' },
  { code: 'nl', label: 'Nederlands' },
  { code: 'vi', label: 'Tiếng Việt' },
]

// Latin-American Spanish ships the same strings as es-MX (the old esLatin
// block was verified identical to es at extraction time), so every es-*
// locale resolves to the es-MX bundle; Chinese variants to zh-Hans.
const SUPPORTED = [
  'en', 'es-MX', 'pt-BR', 'pt', 'fr', 'de', 'it', 'ja',
  'zh-Hans', 'ru', 'uk', 'pl', 'tr', 'nl', 'vi',
] as const

export function isSupportedLanguage(code: string): boolean {
  return (SUPPORTED as readonly string[]).includes(code)
}

// Resolves a navigator/stored locale tag to a supported language code:
// exact match, then base language, then the es/zh families.
function resolveSupported(candidate: string): string | undefined {
  if (isSupportedLanguage(candidate)) {
    return candidate
  }
  const base = candidate.split('-')[0]
  if (isSupportedLanguage(base)) {
    return base
  }
  if (base === 'es') {
    return 'es-MX'
  }
  if (base === 'zh') {
    return 'zh-Hans'
  }
  return undefined
}

function detectLanguage(): string {
  for (const candidate of typeof navigator !== 'undefined' ? navigator.languages : []) {
    const resolved = resolveSupported(candidate)
    if (resolved) {
      return resolved
    }
  }
  return 'en'
}

export function initialLanguage(stored: string | null): string {
  if (stored) {
    return resolveSupported(stored) ?? detectLanguage()
  }
  return detectLanguage()
}

// Vite turns this glob into one lazy chunk per locale JSON; the browser
// fetches a language file only the first time that language is used.
const localeLoaders = import.meta.glob<Record<string, unknown>>('./locales/*.json', {
  import: 'default',
})

const loadedLocales = new Set<string>(['en'])

// i18next backend that serves translations from the lazy locale chunks.
class LazyLocaleBackend implements BackendModule {
  type = 'backend' as const

  init(): void {}

  read(language: string, _namespace: string, callback: ReadCallback): void {
    if (loadedLocales.has(language)) {
      callback(null, {})
      return
    }
    const loader = localeLoaders[`./locales/${language}.json`]
    if (!loader) {
      callback(null, {})
      return
    }
    loader()
      .then(data => {
        loadedLocales.add(language)
        callback(null, data)
      })
      .catch((error: unknown) => {
        callback(error instanceof Error ? error : new Error(String(error)), null)
      })
  }
}

void i18n
  .use(new LazyLocaleBackend())
  .use(initReactI18next)
  .init({
    lng: detectLanguage(),
    fallbackLng: 'en',
    supportedLngs: [...SUPPORTED],
    load: 'currentOnly',
    // Only en is bundled synchronously; other languages arrive through the
    // backend, so i18next must not wait for "all languages loaded" to init.
    partialBundledLanguages: true,
    resources: { en: { translation: en } },
    interpolation: { escapeValue: false },
  })

export async function setLanguage(code: string): Promise<void> {
  const resolved = resolveSupported(code)
  if (!resolved) {
    return
  }
  await i18n.changeLanguage(resolved)
  document.documentElement.lang = resolved
}

export function currentLanguage(): string {
  return i18n.resolvedLanguage ?? i18n.language
}

export default i18n
