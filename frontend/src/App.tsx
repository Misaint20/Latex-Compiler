import { useEffect, useMemo, useState } from 'react'
import { Outlet } from '@tanstack/react-router'
import { ErrorBoundary } from './components/ErrorBoundary'
import { EditorMissingBanner } from './components/EditorMissingBanner'
import { TopBar, type TopBarSection } from './components/TopBar'
import { MenuBar } from './components/MenuBar'
import { chromeContext, type ChromeSection } from './application/chrome'
import { onNativeEvent } from './infrastructure/nativeEvents'
import { setNativeProjectMenuEnabled } from './infrastructure/nativeClient'

const TOPBAR_SECTIONS: TopBarSection[] = ['welcome', 'chapters', 'diagrams', 'styles', 'assets', 'history']

export function App() {
  const [activeSection, setActiveSection] = useState<ChromeSection>('welcome')
  const [browseRequest, setBrowseRequest] = useState(0)

  const isProjectOpen = activeSection !== 'welcome'

  const chrome = useMemo(
    () => ({
      activeSection,
      setSection: (section: ChromeSection) => setActiveSection(section),
      browseRequest,
      requestBrowse: () => setBrowseRequest(n => n + 1),
      isProjectOpen,
    }),
    [activeSection, browseRequest, isProjectOpen],
  )

  // Native menu commands arrive as regular pushed events.
  useEffect(() => {
    const unsubscribe = onNativeEvent((topic, payload) => {
      if (topic === 'menu.openFolder') {
        setActiveSection('welcome')
        setBrowseRequest(n => n + 1)
      } else if (topic === 'menu.goTo') {
        const section = payload as ChromeSection
        setActiveSection(current => (current === 'welcome' ? current : section))
      }
    })
    return unsubscribe
  }, [])

  // The macOS Go menu mirrors the frontend project state.
  useEffect(() => {
    void setNativeProjectMenuEnabled(isProjectOpen)
  }, [isProjectOpen])

  return (
    <ErrorBoundary>
      <chromeContext.Provider value={chrome}>
        <div className="min-h-screen bg-ink-900">
          <TopBar
            active={activeSection as TopBarSection}
            onNavigate={section => setActiveSection(section as ChromeSection)}
            onBrowse={() => {
              setActiveSection('welcome')
              setBrowseRequest(n => n + 1)
            }}
          />
          <MenuBar />
          <EditorMissingBanner />
          <Outlet />
        </div>
      </chromeContext.Provider>
    </ErrorBoundary>
  )
}

export { TOPBAR_SECTIONS }
