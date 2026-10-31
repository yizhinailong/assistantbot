<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import {
  getLogs,
  setLogLevel,
  type LogBatch,
  type LogEntry,
  type LogFilter,
  type LogLevel,
} from '@/api/admin'
import { useAdminStream, type LiveEvent } from '@/composables/useAdminStream'

const levels: LogLevel[] = ['TRACE', 'DEBUG', 'INFO', 'WARN', 'ERROR', 'FATAL']
const draft = ref<LogFilter>({ level: '', keyword: '', qq: '' })
const applied = ref<LogFilter>({ ...draft.value })
const entries = ref<LogEntry[]>([])
const level = ref<LogLevel>('INFO')
const saving = ref(false)
const loading = ref(false)
const paused = ref(false)
const autoScroll = ref(true)
const error = ref('')
const notice = ref('')
const viewport = ref<HTMLElement>()
let cursor = '0'
let revision = 0
let stopped = false
const stream = useAdminStream('/api/admin/logs/ws', receive, subscribe)

function subscribe(): void {
  if (!paused.value && !loading.value)
    stream.send({ type: 'subscribe', ...applied.value, after: cursor })
}

function sameFilter(filter: LogFilter): boolean {
  return (
    filter.level === applied.value.level &&
    filter.keyword === applied.value.keyword &&
    filter.qq === applied.value.qq
  )
}

function append(batch: LogBatch): void {
  if (paused.value || !sameFilter(batch.filters)) return
  const merged = new Map(entries.value.map((entry) => [entry.id, entry]))
  for (const entry of batch.entries) merged.set(entry.id, entry)
  entries.value = [...merged.values()]
    .sort((a, b) => a.id.length - b.id.length || a.id.localeCompare(b.id))
    .slice(-5000)
  if (BigInt(batch.cursor) > BigInt(cursor)) cursor = batch.cursor
  if (batch.truncated) notice.value = '部分日志已超出内存缓冲范围，已显示仍可读取的记录。'
  if (autoScroll.value)
    void nextTick(() => {
      if (viewport.value) viewport.value.scrollTop = viewport.value.scrollHeight
    })
}

function receive(event: LiveEvent): void {
  if (event.type === 'error') error.value = event.error ?? '日志订阅失败'
  if (
    event.type === 'logs' &&
    event.entries &&
    event.filters &&
    event.cursor !== undefined &&
    event.oldest_id !== undefined &&
    event.truncated !== undefined &&
    event.log_level
  )
    append(event as LogBatch)
}

async function applyFilters(): Promise<void> {
  if (draft.value.qq && !/^[0-9]+$/.test(draft.value.qq)) {
    error.value = 'QQ 号必须为正整数。'
    return
  }
  const qq = draft.value.qq ? BigInt(draft.value.qq).toString() : ''
  if (qq && (qq === '0' || BigInt(qq) > 9223372036854775807n)) {
    error.value = 'QQ 号超出有效范围。'
    return
  }
  const current = ++revision
  applied.value = { ...draft.value, qq }
  loading.value = true
  paused.value = false
  error.value = ''
  notice.value = ''
  entries.value = []
  cursor = '0'
  try {
    const batch = await getLogs(applied.value)
    if (stopped || current !== revision) return
    level.value = batch.log_level
    append(batch)
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) {
      loading.value = false
      subscribe()
    }
  }
}

async function changeLevel(): Promise<void> {
  saving.value = true
  error.value = ''
  try {
    level.value = (await setLogLevel(level.value)).level
    notice.value = `运行日志等级已改为 ${level.value}。`
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    saving.value = false
  }
}

watch(paused, (value) => {
  if (!value) subscribe()
})
onMounted(() => void applyFilters())
onUnmounted(() => {
  stopped = true
  revision++
})
</script>

<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>日志</h1>
        <p class="muted">内存保留最近 5000 条日志，重启后清空。</p>
      </div>
      <span :class="stream.connected.value ? 'success' : 'warning'">{{
        stream.connected.value ? '实时连接正常' : '实时连接重试中'
      }}</span>
    </div>
    <form class="panel" @submit.prevent="applyFilters">
      <div class="fields">
        <label class="field"
          >最低等级<select v-model="draft.level">
            <option value="">所有等级</option>
            <option v-for="item in levels" :key="item">{{ item }}</option>
          </select></label
        ><label class="field"
          >关键词<input
            v-model="draft.keyword"
            type="search"
            maxlength="256"
            placeholder="区分大小写" /></label
        ><label class="field"
          >QQ 号<input v-model="draft.qq" type="text" inputmode="numeric" placeholder="完整 QQ 号"
        /></label>
      </div>
      <div class="actions controls">
        <button type="submit" :disabled="loading">
          {{ loading ? '读取中…' : '应用过滤并刷新' }}</button
        ><label class="check"
          ><input v-model="paused" type="checkbox" :disabled="loading" />暂停显示</label
        ><label class="check"><input v-model="autoScroll" type="checkbox" />自动滚动</label>
      </div>
    </form>
    <form class="panel actions level-control" @submit.prevent="changeLevel">
      <label class="field"
        >运行日志等级<select v-model="level" :disabled="saving">
          <option v-for="item in levels" :key="item">{{ item }}</option>
        </select></label
      ><button type="submit" :disabled="saving">{{ saving ? '应用中…' : '应用等级' }}</button
      ><span class="muted">控制后续日志的产生，重启后使用配置文件中的等级。</span>
    </form>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback warning" role="status">{{ notice }}</p>
    <p v-if="paused" class="warning" role="status">显示已暂停，继续后会补取缓冲内的日志。</p>
    <div
      ref="viewport"
      class="panel log-viewport"
      role="log"
      aria-label="运行日志"
      :aria-live="paused ? 'off' : 'polite'"
      aria-relevant="additions"
    >
      <p v-if="!entries.length" class="empty">
        {{ loading ? '正在读取日志…' : '暂无符合条件的日志' }}
      </p>
      <div v-for="entry in entries" :key="entry.id" class="log-line">
        <span :class="['severity', entry.level.toLowerCase()]">{{ entry.level }}</span
        ><time class="muted" :datetime="new Date(entry.timestamp * 1000).toISOString()">{{
          new Date(entry.timestamp * 1000).toLocaleTimeString()
        }}</time>
        <pre>{{ entry.message }}</pre>
      </div>
    </div>
  </div>
</template>

<style scoped>
.controls {
  justify-content: flex-start;
  margin-top: 1rem;
}
.level-control {
  justify-content: flex-start;
  align-items: end;
}
.log-viewport {
  height: 60vh;
  overflow: auto;
}
.log-line {
  display: grid;
  grid-template-columns: 3.5rem 5rem minmax(0, 1fr);
  gap: 0.65rem;
  padding: 0.4rem 0;
  border-bottom: 1px solid var(--color-border);
  font-family: monospace;
  font-size: 0.8rem;
}
.log-line pre {
  margin: 0;
}
.severity {
  font-weight: 700;
}
.warn {
  color: var(--color-warning);
}
.error,
.fatal {
  color: var(--color-error);
}
@media (max-width: 700px) {
  .log-line {
    grid-template-columns: 3.5rem minmax(0, 1fr);
  }
  .log-line pre {
    grid-column: 1 / -1;
  }
}
</style>
