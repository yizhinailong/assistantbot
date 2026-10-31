<script setup lang="ts">
import { computed, onUnmounted, ref, watch } from 'vue'
import { clearHttpTraces, getHttpTraces, type HttpTrace } from '@/api/admin'
import { useAdminStream } from '@/composables/useAdminStream'

const source = ref('')
const traces = ref<HttpTrace[]>([])
const selectedId = ref('')
const error = ref('')
const loading = ref(false)
const paused = ref(false)
let revision = 0
let timer: ReturnType<typeof setTimeout> | undefined
let stopped = false
const selected = computed(() => traces.value.find((trace) => trace.id === selectedId.value))
const download = computed(
  () => `/api/admin/http-traces/download?source=${encodeURIComponent(source.value)}`,
)

async function refresh(): Promise<void> {
  const current = ++revision
  loading.value = true
  try {
    const result = await getHttpTraces(source.value)
    if (stopped || current !== revision) return
    traces.value = result.traces
    if (!traces.value.some((trace) => trace.id === selectedId.value))
      selectedId.value = traces.value.at(-1)?.id ?? ''
    error.value = ''
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) loading.value = false
  }
}

function schedule(): void {
  if (paused.value || timer) return
  timer = setTimeout(() => {
    timer = undefined
    void refresh()
  }, 250)
}

async function clear(): Promise<void> {
  loading.value = true
  try {
    await clearHttpTraces()
    await refresh()
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
    loading.value = false
  }
}

function pretty(value: string): string {
  try {
    return JSON.stringify(JSON.parse(value), null, 2)
  } catch {
    return value
  }
}

const stream = useAdminStream(
  '/api/admin/ws',
  (event) => {
    if (event.type === 'http_trace_created') schedule()
  },
  () => {
    if (!paused.value) void refresh()
  },
)
watch(source, () => void refresh())
watch(paused, (value) => {
  if (!value) void refresh()
})
onUnmounted(() => {
  stopped = true
  revision++
  clearTimeout(timer)
})
</script>

<template>
  <section class="page">
    <header class="page-header">
      <div>
        <h1>请求追踪</h1>
        <p>最近 50 次出站请求，包含失败和重试。凭据及图片数据已隐藏。</p>
      </div>
      <span class="status-pill">{{ stream.connected.value ? '实时连接' : '连接中…' }}</span>
    </header>
    <div class="toolbar">
      <label>
        来源
        <select v-model="source">
          <option value="">全部</option>
          <option value="model">模型</option>
          <option value="onebot">OneBot</option>
          <option value="mcp">MCP</option>
        </select>
      </label>
      <label><input v-model="paused" type="checkbox" /> 暂停刷新</label>
      <button :disabled="loading" @click="refresh">刷新</button>
      <a :href="download" download>下载 JSON</a>
      <button :disabled="loading" @click="clear">清空记录</button>
    </div>
    <p v-if="error" class="form-error" role="alert">{{ error }}</p>
    <div class="table-wrap">
      <table>
        <thead>
          <tr>
            <th>时间</th>
            <th>来源 / 角色</th>
            <th>方法</th>
            <th>地址</th>
            <th>状态</th>
            <th>耗时</th>
            <th></th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="trace in [...traces].reverse()" :key="trace.id">
            <td>{{ new Date(trace.timestamp * 1000).toLocaleTimeString() }}</td>
            <td>{{ trace.source }} {{ trace.role }}</td>
            <td>{{ trace.method }}</td>
            <td class="url">{{ trace.url }}</td>
            <td>{{ trace.status || '连接失败' }}</td>
            <td>{{ trace.elapsed_ms.toFixed(1) }} ms</td>
            <td><button @click="selectedId = trace.id">查看</button></td>
          </tr>
          <tr v-if="!traces.length">
            <td colspan="7">暂无请求记录</td>
          </tr>
        </tbody>
      </table>
    </div>
    <article v-if="selected" class="card detail">
      <h2>请求 #{{ selected.id }}</h2>
      <p>{{ selected.method }} {{ selected.url }}</p>
      <p v-if="selected.session">会话：{{ selected.session }}</p>
      <p v-if="selected.error" class="form-error">{{ selected.error }}</p>
      <h3>请求正文 {{ selected.request_truncated ? '（已截断）' : '' }}</h3>
      <pre>{{ pretty(selected.request_body) || '（空）' }}</pre>
      <h3>响应正文 {{ selected.response_truncated ? '（已截断）' : '' }}</h3>
      <pre>{{ pretty(selected.response_body) || '（空）' }}</pre>
    </article>
  </section>
</template>

<style scoped>
.toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  align-items: center;
  margin-bottom: 1rem;
}
.toolbar label {
  display: flex;
  align-items: center;
  gap: 0.4rem;
}
.url {
  max-width: 28rem;
  overflow-wrap: anywhere;
}
.detail {
  margin-top: 1rem;
  overflow-wrap: anywhere;
}
pre {
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  max-height: 30rem;
  overflow: auto;
  padding: 1rem;
  background: var(--bg);
}
</style>
