import { Component, type ErrorInfo, type ReactNode } from 'react'
import i18n from '../i18n'

type Props = {
  children: ReactNode
}

type State = {
  error: Error | null
}

// A render crash anywhere unmounts the whole React tree and leaves only the
// page background. The boundary keeps the failure visible and recoverable.
export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null }

  static getDerivedStateFromError(error: Error): State {
    return { error }
  }

  componentDidCatch(error: Error, info: ErrorInfo): void {
    console.error('[ui] render crash', error, info.componentStack)
    const detail = `${error.message}\n${error.stack ?? ''}\n${info.componentStack ?? ''}`
    try {
      window.onerror?.(error.message, 'react-render', 0, 0, error)
    } catch {
      // The inline trap is best effort; the boundary UI below still shows.
    }
    void detail
  }

  render(): ReactNode {
    const { error } = this.state
    if (error) {
      return (
        <div className="min-h-screen flex items-center justify-center p-8">
          <div className="max-w-xl w-full rounded-lg border border-red-800 bg-red-900/30 p-6">
            <h1 className="text-lg font-semibold text-red-300 mb-2">
              {i18n.t('errorBoundary.title')}
            </h1>
            <pre className="text-xs text-red-200/90 whitespace-pre-wrap font-mono max-h-64 overflow-y-auto mb-4">
              {error.message}
              {'\n'}
              {error.stack}
            </pre>
            <button
              onClick={() => {
                this.setState({ error: null })
                window.location.hash = ''
                window.location.reload()
              }}
              className="px-4 py-2 bg-brass-500 hover:bg-brass-400 text-ink-950 rounded font-medium transition-colors"
            >
              {i18n.t('errorBoundary.retry')}
            </button>
          </div>
        </div>
      )
    }
    return this.props.children
  }
}
