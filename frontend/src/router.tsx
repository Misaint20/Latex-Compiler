import { createRootRoute, createRoute, createRouter, createHashHistory } from '@tanstack/react-router'
import { App } from './App'
import { HomePage } from './pages/HomePage'

const rootRoute = createRootRoute({ component: App })

const homeRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  component: HomePage,
})

const routeTree = rootRoute.addChildren([homeRoute])

const hashHistory = createHashHistory()
export const router = createRouter({ routeTree, history: hashHistory })

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router
  }
}
