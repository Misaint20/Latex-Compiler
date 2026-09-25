import { useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { useChrome, PROJECT_SECTIONS } from '../application/chrome'

// In-app menu bar for Windows/Linux, mirroring the macOS system menu bar.
// Hidden on macOS, where the native bar owns these commands.
const isMacos =
  typeof navigator !== 'undefined' && navigator.userAgent.includes('Mac')

type MenuEntry = {
  key: string
  label: string
  action: () => void
}

function browseFromMenu() {
  window.dispatchEvent(new CustomEvent('app:browse'))
}

export function MenuBar() {
  const { t } = useTranslation()
  const { setSection, isProjectOpen } = useChrome()
  const [openMenu, setOpenMenu] = useState<'file' | 'go' | null>(null)
  const rootRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (openMenu === null) {
      return
    }
    const onPointerDown = (event: PointerEvent) => {
      if (!rootRef.current?.contains(event.target as Node)) {
        setOpenMenu(null)
      }
    }
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        setOpenMenu(null)
      }
    }
    window.addEventListener('pointerdown', onPointerDown)
    window.addEventListener('keydown', onKeyDown)
    return () => {
      window.removeEventListener('pointerdown', onPointerDown)
      window.removeEventListener('keydown', onKeyDown)
    }
  }, [openMenu])

  if (isMacos) {
    return null
  }

  const goEntries: MenuEntry[] = [
    { key: 'welcome', label: t('topbar.menu.home'), action: () => setSection('welcome') },
    ...PROJECT_SECTIONS.map(section => ({
      key: section,
      label: t(`topbar.menu.${section}`),
      action: () => setSection(section),
    })),
  ]

  const fileEntries: MenuEntry[] = [
    { key: 'open', label: t('topbar.menu.openFolder'), action: browseFromMenu },
  ]

  const renderMenu = (entries: MenuEntry[], menuId: 'file' | 'go') => {
    if (openMenu !== menuId) {
      return null
    }
    return (
      <div className="menubar-dropdown">
        {entries.map(entry => {
          const disabled =
            menuId === 'go' && entry.key !== 'welcome' && !isProjectOpen
          return (
            <button
              key={entry.key}
              type="button"
              disabled={disabled}
              className="menubar-item"
              onClick={() => {
                setOpenMenu(null)
                entry.action()
              }}
            >
              {entry.label}
            </button>
          )
        })}
      </div>
    )
  }

  const goDisabled = !isProjectOpen

  return (
    <div ref={rootRef} className="menubar" role="menubar">
      <button
        type="button"
        className={`menubar-title ${openMenu === 'file' ? 'menubar-title-active' : ''}`}
        onClick={() => setOpenMenu(openMenu === 'file' ? null : 'file')}
      >
        {t('topbar.menu.file')}
      </button>
      {renderMenu(fileEntries, 'file')}

      <span
        className={`menubar-title ${openMenu === 'go' ? 'menubar-title-active' : ''} ${goDisabled ? 'menubar-title-disabled' : ''}`}
        role="button"
        tabIndex={goDisabled ? -1 : 0}
        aria-disabled={goDisabled}
        onClick={() => {
          if (!goDisabled) {
            setOpenMenu(openMenu === 'go' ? null : 'go')
          }
        }}
        onKeyDown={event => {
          if (!goDisabled && (event.key === 'Enter' || event.key === ' ')) {
            setOpenMenu(openMenu === 'go' ? null : 'go')
          }
        }}
      >
        {t('topbar.menu.go')}
      </span>
      {renderMenu(goEntries, 'go')}
    </div>
  )
}
