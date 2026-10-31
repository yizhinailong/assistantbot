<script setup lang="ts">
import { ref } from 'vue'
import { RouterLink, RouterView, useRoute, useRouter } from 'vue-router'
import { logout } from '@/api/admin'
import { ApiError } from '@/api/client'
import { clearAuthentication } from '@/auth'

const route = useRoute()
const router = useRouter()
const signingOut = ref(false)
const logoutError = ref('')
const pages = [
  { to: '/', label: '运行状态' },
  { to: '/sessions', label: '会话' },
  { to: '/settings', label: '回复设置' },
  { to: '/access', label: '访问控制' },
  { to: '/usage', label: '用量统计' },
  { to: '/logs', label: '日志' },
]

async function signOut(): Promise<void> {
  if (signingOut.value) {
    return
  }
  signingOut.value = true
  logoutError.value = ''
  try {
    await logout()
    clearAuthentication()
    await router.replace({ name: 'login' })
  } catch (error) {
    if (error instanceof ApiError && error.status === 401) {
      clearAuthentication()
      await router.replace({ name: 'login' })
    } else {
      logoutError.value = error instanceof Error ? error.message : String(error)
    }
  } finally {
    signingOut.value = false
  }
}
</script>

<template>
  <RouterView v-if="route.name === 'login'" />
  <div v-else class="layout">
    <aside class="sidebar">
      <div class="brand">
        <strong>assistantbot</strong>
        <span>管理后台</span>
      </div>
      <nav aria-label="主导航">
        <RouterLink v-for="page in pages" :key="page.to" :to="page.to">
          {{ page.label }}
        </RouterLink>
      </nav>
      <button class="logout" type="button" :disabled="signingOut" @click="signOut">
        {{ signingOut ? '退出中…' : '退出登录' }}
      </button>
      <p v-if="logoutError" class="form-error" role="alert">{{ logoutError }}</p>
    </aside>
    <main class="content">
      <RouterView />
    </main>
  </div>
</template>

<style scoped>
.layout {
  display: flex;
  min-height: 100vh;
}

.sidebar {
  flex: 0 0 var(--sidebar-width);
  display: flex;
  flex-direction: column;
  padding: 1.25rem 0.75rem;
  border-right: 1px solid var(--color-border);
  background: var(--color-surface);
}

.brand {
  display: grid;
  padding: 0 0.5rem 1.25rem;
}

.brand span {
  color: var(--color-muted);
  font-size: 0.78rem;
}

nav {
  display: flex;
  flex-direction: column;
  gap: 0.25rem;
}

nav a {
  padding: 0.5rem 0.65rem;
  border-radius: 6px;
  color: var(--color-muted);
  text-decoration: none;
}

nav a:hover {
  color: var(--color-text);
}

nav a.router-link-exact-active {
  background: var(--color-bg);
  color: var(--color-text);
}

.logout {
  margin-top: auto;
}

.content {
  flex: 1;
  min-width: 0;
  padding: 1.5rem 2rem 3rem;
}

@media (max-width: 720px) {
  .layout {
    flex-direction: column;
  }

  .sidebar {
    flex: none;
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: center;
    gap: 0.75rem;
    padding: 0.65rem 1rem;
    border-right: none;
    border-bottom: 1px solid var(--color-border);
  }

  .brand {
    padding: 0;
  }

  .brand span {
    display: none;
  }

  nav {
    flex-direction: row;
    overflow-x: auto;
  }

  .logout {
    margin: 0;
  }

  .content {
    padding: 1rem 1rem 2rem;
  }
}
</style>
