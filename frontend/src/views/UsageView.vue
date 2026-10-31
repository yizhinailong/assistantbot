<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { getUsage, type UsageReport } from '@/api/admin'
import { useAdminStream } from '@/composables/useAdminStream'

const DAY = 86400
const today = Math.floor(Date.now() / 1000 / DAY) * DAY
const fromDate = ref(new Date((today - 29 * DAY) * 1000).toISOString().slice(0, 10))
const toDate = ref(new Date(today * 1000).toISOString().slice(0, 10))
const report = ref<UsageReport>()
const busy = ref(false)
const error = ref('')
let range = { from: today - 29 * DAY, to: today + DAY }
let refreshTimer: ReturnType<typeof setTimeout> | undefined
let revision = 0
let stopped = false
const stream = useAdminStream(
  '/api/admin/ws',
  (event) => {
    if (event.type === 'usage_updated') scheduleRefresh()
  },
  scheduleRefresh,
)

const daily = computed(() => {
  if (!report.value) return []
  const days = new Map(report.value.days.map((day) => [day.timestamp, day]))
  return Array.from(
    { length: Math.ceil((report.value.to - report.value.from) / DAY) },
    (_, index) => {
      const timestamp = report.value!.from + index * DAY
      return {
        timestamp,
        label: new Date(timestamp * 1000).toISOString().slice(0, 10),
        tokens: days.get(timestamp)?.total_tokens ?? 0,
        requests: days.get(timestamp)?.requests ?? 0,
      }
    },
  )
})
const maxTokens = computed(() => Math.max(1, ...daily.value.map((day) => day.tokens)))
const users = computed(() =>
  [...(report.value?.users ?? [])].sort(
    (a, b) => b.total_tokens - a.total_tokens || b.requests - a.requests,
  ),
)
const number = (value: number): string => value.toLocaleString()

function scheduleRefresh(): void {
  clearTimeout(refreshTimer)
  refreshTimer = setTimeout(() => void refresh(), 300)
}

async function refresh(): Promise<void> {
  const current = ++revision
  busy.value = true
  try {
    const response = await getUsage(range.from, range.to)
    if (!stopped && current === revision) {
      report.value = response
      error.value = ''
    }
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) busy.value = false
  }
}

async function query(): Promise<void> {
  const from = Date.parse(`${fromDate.value}T00:00:00Z`) / 1000
  const to = Date.parse(`${toDate.value}T00:00:00Z`) / 1000 + DAY
  if (
    !Number.isFinite(from) ||
    !Number.isFinite(to) ||
    from < 0 ||
    to <= from ||
    to - from > 366 * DAY
  ) {
    error.value = '请选择 1970 年之后、长度不超过 366 天的有效日期范围。'
    return
  }
  range = { from, to }
  await refresh()
}

onMounted(() => void refresh())
onUnmounted(() => {
  stopped = true
  revision++
  clearTimeout(refreshTimer)
})
</script>

<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>用量统计</h1>
        <p class="muted">日期与按天分组统一使用 UTC。</p>
      </div>
      <span :class="stream.connected.value ? 'success' : 'warning'">{{
        stream.connected.value ? '实时更新中' : '等待重新连接'
      }}</span>
    </div>
    <form class="panel actions range" @submit.prevent="query">
      <label class="field"
        >开始日期（UTC）<input v-model="fromDate" type="date" min="1970-01-01" required /></label
      ><label class="field"
        >结束日期（UTC，含当天）<input
          v-model="toDate"
          type="date"
          min="1970-01-01"
          required /></label
      ><button type="submit" :disabled="busy">{{ busy ? '查询中…' : '查询' }}</button>
    </form>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <template v-if="report">
      <section class="panel kpis" aria-label="所选范围用量">
        <div>
          <span class="muted">请求数</span><strong>{{ number(report.total.requests) }}</strong
          ><small>成功 {{ number(report.total.successful_requests) }}</small>
        </div>
        <div>
          <span class="muted">输入令牌</span
          ><strong>{{ number(report.total.prompt_tokens) }}</strong>
        </div>
        <div>
          <span class="muted">输出令牌</span
          ><strong>{{ number(report.total.completion_tokens) }}</strong>
        </div>
        <div>
          <span class="muted">总令牌</span><strong>{{ number(report.total.total_tokens) }}</strong>
        </div>
        <div>
          <span class="muted">缓存命中令牌</span
          ><strong>{{ number(report.total.cached_tokens) }}</strong
          ><small>{{ number(report.total.cached_requests) }} 次请求报告缓存数据</small>
        </div>
      </section>
      <p class="muted">
        今日 UTC：{{ number(report.today.requests) }} 次请求，{{
          number(report.today.total_tokens)
        }}
        个令牌。所选范围有
        {{ number(report.total.requests - report.total.reported_requests) }}
        次请求未返回令牌报告，未计入令牌总数。
      </p>
      <section class="panel">
        <h2>每日令牌</h2>
        <div class="chart-scroll">
          <svg
            class="bar-chart"
            :viewBox="`0 0 ${Math.max(480, daily.length * 22)} 240`"
            :style="{ minWidth: `${Math.max(480, daily.length * 22)}px` }"
            role="img"
            aria-label="每天总令牌柱状图，下方可展开数据表"
          >
            <line x1="0" y1="205" :x2="Math.max(480, daily.length * 22)" y2="205" />
            <g v-for="(day, index) in daily" :key="day.timestamp">
              <rect
                :x="index * (Math.max(480, daily.length * 22) / daily.length) + 2"
                :y="205 - (day.tokens / maxTokens) * 185"
                :width="Math.max(480, daily.length * 22) / daily.length - 4"
                :height="Math.max(day.tokens ? 1 : 0, (day.tokens / maxTokens) * 185)"
                tabindex="0"
              >
                <title>
                  {{ day.label }}：{{ number(day.tokens) }} 个令牌，{{ number(day.requests) }}
                  次请求
                </title>
              </rect>
              <text
                v-if="index % Math.max(1, Math.ceil(daily.length / 8)) === 0"
                :x="index * (Math.max(480, daily.length * 22) / daily.length) + 2"
                y="226"
              >
                {{ day.label.slice(5) }}
              </text>
            </g>
          </svg>
        </div>
        <details>
          <summary>查看每日数据</summary>
          <div class="table-scroll">
            <table>
              <thead>
                <tr>
                  <th>日期（UTC）</th>
                  <th>请求数</th>
                  <th>总令牌</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="day in daily" :key="day.timestamp">
                  <td>{{ day.label }}</td>
                  <td>{{ number(day.requests) }}</td>
                  <td>{{ number(day.tokens) }}</td>
                </tr>
              </tbody>
            </table>
          </div>
        </details>
      </section>
      <section class="panel">
        <h2>好友排行</h2>
        <div class="table-scroll">
          <table>
            <thead>
              <tr>
                <th>QQ 号</th>
                <th>请求数</th>
                <th>总令牌</th>
                <th>缓存命中令牌</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="user in users" :key="user.user_id">
                <td>{{ user.user_id }}</td>
                <td>{{ number(user.requests) }}</td>
                <td>{{ number(user.total_tokens) }}</td>
                <td>{{ number(user.cached_tokens) }}</td>
              </tr>
            </tbody>
          </table>
        </div>
        <p v-if="!users.length" class="empty">所选范围暂无请求</p>
      </section>
    </template>
  </div>
</template>

<style scoped>
.range {
  justify-content: flex-start;
  align-items: end;
}
.chart-scroll {
  overflow-x: auto;
}
.bar-chart {
  display: block;
  width: 100%;
  height: 240px;
}
.bar-chart rect {
  fill: var(--color-accent);
}
.bar-chart rect:focus {
  stroke: var(--color-text);
  stroke-width: 2;
  outline: none;
}
.bar-chart line {
  stroke: var(--color-border);
}
.bar-chart text {
  fill: var(--color-muted);
  font-size: 11px;
}
</style>
