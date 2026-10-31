import { createRouter, createWebHashHistory } from 'vue-router'
import { getAuthenticationStatus } from '@/api/admin'
import { safeInternalRoute, setAuthenticated } from '@/auth'
import LoginView from '@/views/LoginView.vue'
import OverviewView from '@/views/OverviewView.vue'
import AccessView from '@/views/AccessView.vue'
import SessionsView from '@/views/SessionsView.vue'
import ReplySettingsView from '@/views/ReplySettingsView.vue'
import UsageView from '@/views/UsageView.vue'
import LogsView from '@/views/LogsView.vue'
import HttpTracesView from '@/views/HttpTracesView.vue'
import ModelsView from '@/views/ModelsView.vue'
import MemoryView from '@/views/MemoryView.vue'
import TasksView from '@/views/TasksView.vue'
import ToolsView from '@/views/ToolsView.vue'
import McpView from '@/views/McpView.vue'
import AssetsView from '@/views/AssetsView.vue'
import GroupsView from '@/views/GroupsView.vue'

// Hash history keeps every page at `/`, so drogon serves index.html from document_root
// without a fallback route for the page paths.
const router = createRouter({
  history: createWebHashHistory(),
  routes: [
    { path: '/login', name: 'login', component: LoginView, meta: { public: true } },
    { path: '/', name: 'overview', component: OverviewView },
    { path: '/access', name: 'access', component: AccessView },
    { path: '/sessions', name: 'sessions', component: SessionsView },
    { path: '/settings', name: 'settings', component: ReplySettingsView },
    { path: '/usage', name: 'usage', component: UsageView },
    { path: '/logs', name: 'logs', component: LogsView },
    { path: '/models', name: 'models', component: ModelsView },
    { path: '/memory', name: 'memory', component: MemoryView },
    { path: '/tasks', name: 'tasks', component: TasksView },
    { path: '/tools', name: 'tools', component: ToolsView },
    { path: '/mcp', name: 'mcp', component: McpView },
    { path: '/assets', name: 'assets', component: AssetsView },
    { path: '/groups', name: 'groups', component: GroupsView },
    { path: '/http-traces', name: 'http-traces', component: HttpTracesView },
    { path: '/:pathMatch(.*)*', redirect: '/' },
  ],
})

router.beforeEach(async (to) => {
  try {
    const status = await getAuthenticationStatus()
    setAuthenticated(status.authenticated)

    if (to.meta.public) {
      if (to.name === 'login' && status.authenticated) {
        return safeInternalRoute(to.query.redirect)
      }
      return true
    }

    if (!status.authenticated) {
      return { name: 'login', query: { redirect: safeInternalRoute(to.fullPath) } }
    }

    return true
  } catch {
    // A network or server failure is not evidence that the session is unauthenticated.
    return true
  }
})

export default router
