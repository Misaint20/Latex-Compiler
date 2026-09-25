import { useTranslation } from 'react-i18next'
import { LANGUAGES, currentLanguage, setLanguage } from '../i18n'
import { setAppLanguage } from '../infrastructure/nativeClient'

export function LanguagePicker() {
  const { i18n } = useTranslation()

  const onChange = (code: string) => {
    void setLanguage(code).then(() => {
      void setAppLanguage(code).catch(() => {
        // The choice still applies this session when persisting fails.
      })
    })
  }

  return (
    <select
      value={currentLanguage()}
      onChange={e => onChange(e.target.value)}
      aria-label={i18n.t('topbar.language')}
      title={i18n.t('topbar.language')}
      className="select-input !py-1 !text-xs"
    >
      {LANGUAGES.map(language => (
        <option key={language.code} value={language.code}>
          {language.label}
        </option>
      ))}
    </select>
  )
}
