<script setup lang="ts">
import { ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import { login } from '@/api/admin'
import { safeInternalRoute, setAuthenticated } from '@/auth'

const route = useRoute()
const router = useRouter()
const token = ref('')
const submitting = ref(false)
const errorMessage = ref('')

async function submit(): Promise<void> {
  if (token.value === '' || submitting.value) {
    return
  }

  submitting.value = true
  errorMessage.value = ''
  try {
    const result = await login(token.value)
    setAuthenticated(result.authenticated)
    token.value = ''
    await router.replace(safeInternalRoute(route.query.redirect))
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error)
  } finally {
    submitting.value = false
  }
}
</script>

<template>
  <main class="login-page">
    <section class="login-card" aria-labelledby="login-title">
      <p class="eyebrow">assistantbot 管理后台</p>
      <h1 id="login-title">登录</h1>
      <p class="muted">输入管理员令牌以查看运行状态。令牌只用于本次登录请求。</p>
      <form @submit.prevent="submit">
        <label for="token">管理员令牌</label>
        <input
          id="token"
          v-model="token"
          name="token"
          type="password"
          autocomplete="current-password"
          required
          autofocus
        />
        <p v-if="errorMessage" class="form-error" role="alert">{{ errorMessage }}</p>
        <button class="primary" type="submit" :disabled="submitting || token === ''">
          {{ submitting ? '登录中…' : '登录' }}
        </button>
      </form>
    </section>
  </main>
</template>

<style scoped>
.login-page {
  display: grid;
  min-height: 100vh;
  place-items: center;
  padding: 1rem;
}

.login-card {
  width: min(100%, 25rem);
  padding: 2rem;
  border: 1px solid var(--color-border);
  border-radius: 12px;
  background: var(--color-surface);
  box-shadow: 0 12px 36px rgb(15 23 42 / 10%);
}

.eyebrow {
  margin: 0 0 0.35rem;
  color: var(--color-accent);
  font-size: 0.8rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

form {
  display: grid;
  gap: 0.75rem;
  margin-top: 1.5rem;
}

button {
  margin-top: 0.25rem;
}
</style>
