import { createContext, useContext } from 'react'

export type ChromeSection =
  | 'welcome'
  | 'chapters'
  | 'diagrams'
  | 'styles'
  | 'assets'
  | 'history'

export const PROJECT_SECTIONS: ChromeSection[] = ['chapters', 'diagrams', 'styles', 'assets', 'history']

export type ChromeActions = {
  activeSection: ChromeSection
  setSection: (section: ChromeSection) => void
  browseRequest: number
  requestBrowse: () => void
  /** True once a project folder has been scanned and is on screen. */
  isProjectOpen: boolean
}

export const chromeContext = createContext<ChromeActions>({
  activeSection: 'welcome',
  setSection: () => {},
  browseRequest: 0,
  requestBrowse: () => {},
  isProjectOpen: false,
})

export function useChrome(): ChromeActions {
  return useContext(chromeContext)
}
