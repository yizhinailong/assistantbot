<script setup lang="ts">
import { computed, ref } from 'vue'
import type { ProcessMetricSample } from '@/api/admin'

const props = defineProps<{ samples: ProcessMetricSample[] }>()

const width = 800
const height = 280
const margin = { top: 22, right: 24, bottom: 44, left: 64 }
const plotWidth = width - margin.left - margin.right
const plotHeight = height - margin.top - margin.bottom
const activeIndex = ref<number | null>(null)

const measuredSamples = computed(() =>
  props.samples.filter((sample) => sample.rss_bytes !== null || sample.peak_rss_bytes !== null),
)
const maxBytes = computed(() => {
  const values = measuredSamples.value.flatMap((sample) =>
    [sample.rss_bytes, sample.peak_rss_bytes].filter((value): value is number => value !== null),
  )
  return Math.max(...values, 1)
})
const yTicks = computed(() => Array.from({ length: 5 }, (_, index) => (maxBytes.value * index) / 4))
const activeSample = computed(() =>
  activeIndex.value === null ? null : (props.samples[activeIndex.value] ?? null),
)

function x(index: number): number {
  if (props.samples.length <= 1) {
    return margin.left + plotWidth / 2
  }
  return margin.left + (index / (props.samples.length - 1)) * plotWidth
}

function y(value: number): number {
  return margin.top + plotHeight - (value / maxBytes.value) * plotHeight
}

function linePath(field: 'rss_bytes' | 'peak_rss_bytes'): string {
  let path = ''
  let drawing = false
  props.samples.forEach((sample, index) => {
    const value = sample[field]
    if (value === null) {
      drawing = false
      return
    }
    path += `${drawing ? 'L' : 'M'} ${x(index).toFixed(2)} ${y(value).toFixed(2)} `
    drawing = true
  })
  return path.trim()
}

function nearestIndex(clientX: number, target: SVGSVGElement): number {
  const rect = target.getBoundingClientRect()
  const localX = ((clientX - rect.left) / rect.width) * width
  const ratio = Math.max(0, Math.min(1, (localX - margin.left) / plotWidth))
  return Math.round(ratio * Math.max(0, props.samples.length - 1))
}

function selectFromPointer(event: PointerEvent): void {
  if (props.samples.length === 0) {
    return
  }
  activeIndex.value = nearestIndex(event.clientX, event.currentTarget as SVGSVGElement)
}

function moveSelection(direction: -1 | 1): void {
  if (props.samples.length === 0) {
    return
  }
  const current = activeIndex.value ?? props.samples.length - 1
  activeIndex.value = Math.max(0, Math.min(props.samples.length - 1, current + direction))
}

function formatBytes(value: number | null): string {
  if (value === null) {
    return '不可用'
  }
  const units = ['B', 'KiB', 'MiB', 'GiB', 'TiB']
  let size = value
  let unit = 0
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024
    unit += 1
  }
  return `${size.toFixed(unit >= 2 ? 1 : 0)} ${units[unit]}`
}

function formatTime(timestamp: number): string {
  return new Intl.DateTimeFormat('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  }).format(new Date(timestamp * 1000))
}
</script>

<template>
  <div class="memory-chart">
    <div v-if="samples.length === 0" class="empty-state">尚无进程指标样本。</div>
    <template v-else>
      <div class="legend" aria-hidden="true">
        <span><i class="legend-rss"></i>常驻内存</span>
        <span><i class="legend-peak"></i>峰值内存</span>
      </div>
      <div class="chart-wrap">
        <svg
          :viewBox="`0 0 ${width} ${height}`"
          role="img"
          aria-labelledby="memory-title memory-description"
          tabindex="0"
          @pointermove="selectFromPointer"
          @pointerleave="activeIndex = null"
          @focus="activeIndex ??= samples.length - 1"
          @keydown.left.prevent="moveSelection(-1)"
          @keydown.right.prevent="moveSelection(1)"
        >
          <title id="memory-title">进程内存历史</title>
          <desc id="memory-description">
            常驻内存和峰值内存随时间的折线图。使用左右方向键查看各样本，详细数据也列在图表后的表格中。
          </desc>
          <g class="grid">
            <template v-for="tick in yTicks" :key="tick">
              <line :x1="margin.left" :x2="width - margin.right" :y1="y(tick)" :y2="y(tick)" />
              <text :x="margin.left - 10" :y="y(tick) + 4" text-anchor="end">
                {{ formatBytes(tick) }}
              </text>
            </template>
          </g>
          <line
            class="axis"
            :x1="margin.left"
            :x2="width - margin.right"
            :y1="height - margin.bottom"
            :y2="height - margin.bottom"
          />
          <path
            v-if="linePath('peak_rss_bytes')"
            class="peak-line"
            :d="linePath('peak_rss_bytes')"
          />
          <path v-if="linePath('rss_bytes')" class="rss-line" :d="linePath('rss_bytes')" />
          <template v-if="samples.length === 1">
            <circle
              v-if="samples[0]!.peak_rss_bytes !== null"
              class="peak-point"
              :cx="x(0)"
              :cy="y(samples[0]!.peak_rss_bytes)"
              r="4"
            />
            <circle
              v-if="samples[0]!.rss_bytes !== null"
              class="rss-point"
              :cx="x(0)"
              :cy="y(samples[0]!.rss_bytes)"
              r="4"
            />
          </template>
          <template v-if="activeSample && activeIndex !== null">
            <line
              class="crosshair"
              :x1="x(activeIndex)"
              :x2="x(activeIndex)"
              :y1="margin.top"
              :y2="height - margin.bottom"
            />
            <circle
              v-if="activeSample.rss_bytes !== null"
              class="rss-point"
              :cx="x(activeIndex)"
              :cy="y(activeSample.rss_bytes)"
              r="5"
            />
          </template>
          <text :x="margin.left" :y="height - 14">{{ formatTime(samples[0]!.timestamp) }}</text>
          <text :x="width - margin.right" :y="height - 14" text-anchor="end">
            {{ formatTime(samples[samples.length - 1]!.timestamp) }}
          </text>
        </svg>
        <div v-if="activeSample" class="tooltip" role="status">
          <strong>{{ formatTime(activeSample.timestamp) }}</strong>
          <span>常驻内存：{{ formatBytes(activeSample.rss_bytes) }}</span>
          <span>峰值内存：{{ formatBytes(activeSample.peak_rss_bytes) }}</span>
          <span>线程：{{ activeSample.threads ?? '不可用' }}</span>
        </div>
      </div>
      <details>
        <summary>查看数据表</summary>
        <div class="table-scroll">
          <table>
            <thead>
              <tr>
                <th>时间</th>
                <th>常驻内存</th>
                <th>峰值内存</th>
                <th>线程</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="sample in samples" :key="sample.timestamp">
                <td>{{ formatTime(sample.timestamp) }}</td>
                <td>{{ formatBytes(sample.rss_bytes) }}</td>
                <td>{{ formatBytes(sample.peak_rss_bytes) }}</td>
                <td>{{ sample.threads ?? '不可用' }}</td>
              </tr>
            </tbody>
          </table>
        </div>
      </details>
    </template>
  </div>
</template>

<style scoped>
.memory-chart {
  --chart-series: #2563eb;
  --chart-context: #6b7280;
  --chart-grid: #dfe3e8;
  --chart-axis: #9ca3af;
}

.legend {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  margin-bottom: 0.5rem;
  color: var(--color-muted);
  font-size: 0.85rem;
}

.legend span {
  display: inline-flex;
  align-items: center;
  gap: 0.4rem;
}
.legend i {
  width: 1.25rem;
  border-top: 3px solid;
}
.legend-rss {
  border-color: var(--chart-series) !important;
}
.legend-peak {
  border-color: var(--chart-context) !important;
  border-top-style: dashed !important;
}
.chart-wrap {
  position: relative;
}
svg {
  display: block;
  width: 100%;
  min-height: 15rem;
  overflow: visible;
  touch-action: none;
}
svg:focus-visible {
  outline: 2px solid var(--color-accent);
  outline-offset: 3px;
}
.grid line {
  stroke: var(--chart-grid);
  stroke-width: 1;
}
.grid text,
svg > text {
  fill: var(--color-muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.axis {
  stroke: var(--chart-axis);
}
.rss-line,
.peak-line {
  fill: none;
  vector-effect: non-scaling-stroke;
  stroke-linejoin: round;
  stroke-linecap: round;
}
.rss-line {
  stroke: var(--chart-series);
  stroke-width: 3;
}
.peak-line {
  stroke: var(--chart-context);
  stroke-width: 2;
  stroke-dasharray: 6 5;
}
.crosshair {
  stroke: var(--chart-axis);
  stroke-width: 1;
  stroke-dasharray: 3 3;
}
.rss-point {
  fill: var(--color-surface);
  stroke: var(--chart-series);
  stroke-width: 3;
}
.peak-point {
  fill: var(--chart-context);
}
.tooltip {
  position: absolute;
  top: 0.5rem;
  right: 0.5rem;
  display: grid;
  gap: 0.15rem;
  max-width: 15rem;
  padding: 0.55rem 0.7rem;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  background: color-mix(in srgb, var(--color-surface) 94%, transparent);
  box-shadow: 0 4px 12px rgb(15 23 42 / 10%);
  font-size: 0.8rem;
  pointer-events: none;
}
summary {
  cursor: pointer;
  color: var(--color-accent);
}
.table-scroll {
  overflow-x: auto;
  margin-top: 0.75rem;
}
table {
  width: 100%;
  border-collapse: collapse;
  font-size: 0.85rem;
  font-variant-numeric: tabular-nums;
}
th,
td {
  padding: 0.45rem 0.6rem;
  border-bottom: 1px solid var(--color-border);
  text-align: right;
  white-space: nowrap;
}
th:first-child,
td:first-child {
  text-align: left;
}
.empty-state {
  padding: 2rem 1rem;
  color: var(--color-muted);
  text-align: center;
}

@media (prefers-color-scheme: dark) {
  .memory-chart {
    --chart-series: #4c8dff;
    --chart-context: #a8b3c2;
    --chart-grid: #30363d;
    --chart-axis: #6e7681;
  }
}

@media (max-width: 560px) {
  .tooltip {
    position: static;
    margin-top: 0.5rem;
    max-width: none;
  }
}
</style>
