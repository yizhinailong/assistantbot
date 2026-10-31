<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import {
  getBotStatus,
  getProcessMetrics,
  getRuntimeStatus,
  setBotPaused,
  type ProcessMetrics,
  type RuntimeStatus,
} from '@/api/admin'
import MemoryChart from '@/components/MemoryChart.vue'

const status = ref<RuntimeStatus | null>(null)
const metrics = ref<ProcessMetrics | null>(null)
const loading = ref(true)
const refreshing = ref(false)
const changingPause = ref(false)
const errorMessage = ref('')
const lastUpdated = ref<Date | null>(null)
let refreshTimer: ReturnType<typeof setInterval> | undefined
const latestSample = computed(() => metrics.value?.samples.at(-1))
const successRate = computed(() => {
  const usage = status.value?.today_usage
  return usage?.requests
    ? `${((usage.successful_requests / usage.requests) * 100).toFixed(1)}%`
    : '—'
})

const uptime = computed(() => {
  if (!status.value) return '—'
  let seconds = Math.max(0, Math.floor(Date.now() / 1000 - status.value.started_at))
  const days = Math.floor(seconds / 86400)
  seconds %= 86400
  const hours = Math.floor(seconds / 3600)
  seconds %= 3600
  const minutes = Math.floor(seconds / 60)
  return [days ? `${days} 天` : '', hours ? `${hours} 小时` : '', `${minutes} 分钟`]
    .filter(Boolean)
    .join(' ')
})

async function refresh(initial = false): Promise<void> {
  if (refreshing.value || changingPause.value) return
  if (initial) loading.value = true
  else refreshing.value = true
  errorMessage.value = ''

  const [statusResult, metricsResult] = await Promise.allSettled([
    getRuntimeStatus(),
    getProcessMetrics(),
  ])
  if (statusResult.status === 'fulfilled') {
    status.value = statusResult.value
  }
  if (metricsResult.status === 'fulfilled') {
    metrics.value = metricsResult.value
  }
  const failures = [statusResult, metricsResult].filter(
    (result): result is PromiseRejectedResult => result.status === 'rejected',
  )
  if (failures.length > 0) {
    errorMessage.value = failures
      .map((failure) =>
        failure.reason instanceof Error ? failure.reason.message : String(failure.reason),
      )
      .join('；')
  }
  if (failures.length === 0) lastUpdated.value = new Date()
  loading.value = false
  refreshing.value = false
}

async function togglePaused(): Promise<void> {
  if (!status.value || changingPause.value || refreshing.value) return
  changingPause.value = true
  errorMessage.value = ''
  try {
    const result = await setBotPaused(!status.value.paused)
    status.value = { ...status.value, paused: result.paused }
    const confirmed = await getBotStatus()
    status.value = { ...status.value, paused: confirmed.paused }
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : String(error)
  } finally {
    changingPause.value = false
  }
}

function formatStartedAt(timestamp: number): string {
  return new Intl.DateTimeFormat('zh-CN', { dateStyle: 'medium', timeStyle: 'medium' }).format(
    new Date(timestamp * 1000),
  )
}

function yesNoUnknown(value: boolean | null): string {
  if (value === null) return '未知'
  return value ? '是' : '否'
}

onMounted(() => {
  void refresh(true)
  refreshTimer = setInterval(() => void refresh(), 60_000)
})
onUnmounted(() => clearInterval(refreshTimer))
</script>

<template>
  <header class="page-header">
    <div>
      <p class="eyebrow">系统监控</p>
      <h1>运行状态</h1>
      <p class="muted">查看 bot、NapCat 和进程资源状态。</p>
    </div>
    <div class="header-actions">
      <span v-if="lastUpdated" class="muted">
        更新于 {{ lastUpdated.toLocaleTimeString('zh-CN') }}
      </span>
      <button type="button" :disabled="loading || refreshing" @click="refresh()">
        {{ refreshing ? '刷新中…' : '刷新' }}
      </button>
    </div>
  </header>

  <p v-if="errorMessage" class="notice error" role="alert">部分状态加载失败：{{ errorMessage }}</p>
  <p v-if="loading" class="notice" role="status">正在加载运行状态…</p>

  <template v-else>
    <section v-if="status" class="kpi-grid" aria-label="运行摘要">
      <article class="stat-card">
        <span class="stat-label">bot 状态</span>
        <strong :class="status.paused ? 'warning-text' : 'ok-text'">
          {{ status.paused ? '已暂停' : '运行中' }}
        </strong>
        <span class="muted">{{
          status.bot ? `${status.bot.nickname} (${status.bot.user_id})` : '账号未知'
        }}</span>
      </article>
      <article class="stat-card">
        <span class="stat-label">会话数</span>
        <strong>{{ status.session_count }}</strong>
        <span class="muted">当前内存会话</span>
      </article>
      <article class="stat-card">
        <span class="stat-label">运行时间</span>
        <strong>{{ uptime }}</strong>
        <span class="muted">启动于 {{ formatStartedAt(status.started_at) }}</span>
      </article>
      <article class="stat-card">
        <span class="stat-label">模型</span>
        <strong class="model-value">{{ status.model }}</strong>
        <span class="muted">版本 {{ status.version }}</span>
      </article>
    </section>

    <section v-if="status" class="card control-card">
      <div>
        <h2>消息处理</h2>
        <p class="muted">暂停后 bot 不再处理新的聊天消息；恢复后继续运行。</p>
      </div>
      <button
        :class="status.paused ? 'primary' : 'danger'"
        type="button"
        :disabled="changingPause || refreshing"
        @click="togglePaused"
      >
        {{ changingPause ? '更新中…' : status.paused ? '恢复运行' : '暂停 bot' }}
      </button>
    </section>

    <div class="detail-grid">
      <section v-if="status" class="card">
        <h2>NapCat</h2>
        <dl>
          <div>
            <dt>在线</dt>
            <dd>{{ yesNoUnknown(status.napcat.online) }}</dd>
          </div>
          <div>
            <dt>健康</dt>
            <dd>{{ yesNoUnknown(status.napcat.good) }}</dd>
          </div>
          <div>
            <dt>应用</dt>
            <dd>{{ status.napcat.app_name ?? '未知' }}</dd>
          </div>
          <div>
            <dt>应用版本</dt>
            <dd>{{ status.napcat.app_version ?? '未知' }}</dd>
          </div>
          <div>
            <dt>协议版本</dt>
            <dd>{{ status.napcat.protocol_version ?? '未知' }}</dd>
          </div>
        </dl>
      </section>
      <section v-if="status" class="card">
        <h2>当天用量（UTC）</h2>
        <dl>
          <div>
            <dt>模型请求</dt>
            <dd>{{ status.today_usage.requests }}</dd>
          </div>
          <div>
            <dt>成功率</dt>
            <dd>{{ successRate }}</dd>
          </div>
          <div>
            <dt>输入令牌</dt>
            <dd>{{ status.today_usage.prompt_tokens.toLocaleString('zh-CN') }}</dd>
          </div>
          <div>
            <dt>输出令牌</dt>
            <dd>{{ status.today_usage.completion_tokens.toLocaleString('zh-CN') }}</dd>
          </div>
          <div>
            <dt>总令牌</dt>
            <dd>{{ status.today_usage.total_tokens.toLocaleString('zh-CN') }}</dd>
          </div>
        </dl>
        <p class="muted">
          {{ status.today_usage.reported_requests }} 次请求返回了令牌用量；未返回的用量不计入总数。
        </p>
      </section>
    </div>

    <section class="card chart-card">
      <div class="section-heading">
        <div>
          <h2>进程内存</h2>
          <p class="muted">
            最多保留 {{ metrics?.capacity ?? 1440 }} 个样本，每
            {{ metrics?.interval_seconds ?? 60 }} 秒采集。
          </p>
        </div>
        <span v-if="metrics" class="muted"
          >{{ metrics.samples.length }} 个样本 · 当前线程
          {{ latestSample?.threads ?? '不可用' }}</span
        >
      </div>
      <MemoryChart :samples="metrics?.samples ?? []" />
    </section>
  </template>
</template>

<style scoped>
.page-header,
.section-heading,
.control-card {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}
.page-header {
  margin-bottom: 1.25rem;
}
.eyebrow {
  margin: 0 0 0.2rem;
  color: var(--color-accent);
  font-size: 0.78rem;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}
.page-header h1 {
  margin-bottom: 0.15rem;
}
.page-header p,
.section-heading p,
.control-card p {
  margin: 0;
}
.header-actions {
  display: flex;
  align-items: center;
  gap: 0.75rem;
}
.notice {
  padding: 0.75rem 1rem;
  border: 1px solid var(--color-border);
  border-radius: 8px;
  background: var(--color-surface);
}
.error {
  border-color: var(--color-error);
  color: var(--color-error);
}
.kpi-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 1rem;
  margin-bottom: 1rem;
}
.stat-card {
  display: grid;
  gap: 0.25rem;
  min-width: 0;
  padding: 1rem;
  border: 1px solid var(--color-border);
  border-radius: 8px;
  background: var(--color-surface);
}
.stat-label {
  color: var(--color-muted);
  font-size: 0.82rem;
}
.stat-card strong {
  font-size: 1.35rem;
  line-height: 1.25;
}
.model-value {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.ok-text {
  color: var(--color-ok);
}
.warning-text {
  color: var(--color-warning);
}
.control-card,
.chart-card {
  max-width: none;
  margin-bottom: 1rem;
}
.danger {
  border-color: var(--color-error);
  color: var(--color-error);
}
.detail-grid {
  display: grid;
  grid-template-columns: minmax(0, 2fr) minmax(15rem, 1fr);
  gap: 1rem;
  margin-bottom: 1rem;
}
.detail-grid .card {
  max-width: none;
}
dl {
  margin: 0;
}
dl div {
  display: flex;
  justify-content: space-between;
  gap: 1rem;
  padding: 0.55rem 0;
  border-bottom: 1px solid var(--color-border);
}
dl div:last-child {
  border-bottom: 0;
}
dt {
  color: var(--color-muted);
}
dd {
  margin: 0;
  text-align: right;
}
.chart-card {
  padding-bottom: 1.25rem;
}
.section-heading {
  margin-bottom: 1rem;
}

@media (max-width: 1050px) {
  .kpi-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
@media (max-width: 720px) {
  .page-header,
  .control-card {
    align-items: stretch;
    flex-direction: column;
  }
  .header-actions {
    justify-content: space-between;
  }
  .detail-grid {
    grid-template-columns: 1fr;
  }
}
@media (max-width: 480px) {
  .kpi-grid {
    grid-template-columns: 1fr;
  }
}
</style>
