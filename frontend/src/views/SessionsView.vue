<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import {
  getMessages,
  getSession,
  getSessions,
  resetSession,
  type ChatMessage,
  type SessionDetail,
  type SessionSummary,
} from '@/api/admin'
import { useAdminStream } from '@/composables/useAdminStream'

const sessions = ref<SessionSummary[]>([])
const selected = ref('')
const search = ref('')
const onlySelected = ref(false)
const detail = ref<SessionDetail>()
const messages = ref<ChatMessage[]>([])
const hasOlder = ref(false)
const loading = ref(false)
const loadingOlder = ref(false)
const resetting = ref(false)
const error = ref('')
const notice = ref('')
const viewport = ref<HTMLElement>()
let fetchedCursor: string | undefined
let revision = 0
let listRevision = 0
let refreshTimer: ReturnType<typeof setTimeout> | undefined
let catchingUp = false
let stopped = false

const visibleSessions = computed(() =>
  sessions.value.filter((session) =>
    `${session.user_id} ${session.nickname}`
      .toLowerCase()
      .includes(search.value.trim().toLowerCase()),
  ),
)
const stream = useAdminStream(
  '/api/admin/ws',
  (event) => {
    if (event.type === 'message_created' && event.message && event.user_id === selected.value)
      mergeMessages([event.message])
    if (
      event.type === 'message_created' ||
      event.type === 'session_updated' ||
      event.type === 'settings_updated'
    )
      scheduleRefresh()
  },
  () => {
    subscribe()
    scheduleRefresh()
    void catchUp()
  },
)

function subscribe(): void {
  stream.send({
    type: 'subscribe',
    user_id: onlySelected.value && selected.value ? selected.value : null,
  })
}

function time(timestamp: number): string {
  return new Date(timestamp * 1000).toLocaleString()
}

function mergeMessages(incoming: ChatMessage[], follow = true): void {
  const element = viewport.value
  const atBottom =
    !messages.value.length ||
    !element ||
    element.scrollHeight - element.scrollTop - element.clientHeight < 40
  const merged = new Map(messages.value.map((message) => [message.id, message]))
  for (const message of incoming) merged.set(message.id, message)
  messages.value = [...merged.values()].sort(
    (a, b) => a.id.length - b.id.length || a.id.localeCompare(b.id),
  )
  if (follow && atBottom)
    void nextTick(() => {
      if (viewport.value) viewport.value.scrollTop = viewport.value.scrollHeight
    })
}

async function refreshList(): Promise<void> {
  const current = ++listRevision
  try {
    const response = await getSessions()
    if (stopped || current !== listRevision) return
    sessions.value = response.sessions
    if (!selected.value && sessions.value[0]) selected.value = sessions.value[0].user_id
  } catch (cause) {
    if (!stopped && current === listRevision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  }
}

async function refreshDetail(): Promise<void> {
  const qq = selected.value
  const current = revision
  if (!qq) return
  try {
    const result = await getSession(qq)
    if (!stopped && current === revision) detail.value = result
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  }
}

function scheduleRefresh(): void {
  clearTimeout(refreshTimer)
  refreshTimer = setTimeout(() => {
    void refreshList()
    void refreshDetail()
  }, 150)
}

async function refreshAll(): Promise<void> {
  error.value = ''
  await Promise.all([refreshList(), refreshDetail(), catchUp()])
}

async function loadSelection(): Promise<void> {
  const current = ++revision
  const qq = selected.value
  messages.value = []
  detail.value = undefined
  hasOlder.value = false
  error.value = ''
  notice.value = ''
  catchingUp = false
  fetchedCursor = undefined
  loadingOlder.value = false
  subscribe()
  if (!qq) return
  loading.value = true
  try {
    const [session, page] = await Promise.all([getSession(qq), getMessages(qq)])
    if (stopped || current !== revision) return
    detail.value = session
    mergeMessages(page.messages)
    hasOlder.value = page.has_more
    fetchedCursor = page.next_after ?? '0'
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) loading.value = false
  }
  if (!stopped && current === revision && fetchedCursor !== undefined && stream.connected.value)
    void catchUp()
}

async function catchUp(): Promise<void> {
  if (!selected.value || loading.value || catchingUp) return
  const qq = selected.value
  const current = revision
  let after = fetchedCursor
  if (after === undefined) {
    await loadSelection()
    return
  }
  catchingUp = true
  try {
    let more = true
    while (more && !stopped && current === revision) {
      const page = await getMessages(qq, { after, limit: 200 })
      if (stopped || current !== revision) return
      mergeMessages(page.messages)
      more = page.has_more
      after = page.next_after ?? after
      fetchedCursor = after
    }
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) catchingUp = false
  }
}

async function older(): Promise<void> {
  const before = messages.value[0]?.id
  if (!before || loadingOlder.value) return
  const current = revision
  loadingOlder.value = true
  try {
    const page = await getMessages(selected.value, { before })
    if (stopped || current !== revision) return
    const height = viewport.value?.scrollHeight ?? 0
    const top = viewport.value?.scrollTop ?? 0
    mergeMessages(page.messages, false)
    hasOlder.value = page.has_more
    await nextTick()
    if (viewport.value && current === revision)
      viewport.value.scrollTop = viewport.value.scrollHeight - height + top
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    if (current === revision) loadingOlder.value = false
  }
}

async function reset(): Promise<void> {
  if (resetting.value || !selected.value) return
  const current = revision
  resetting.value = true
  error.value = ''
  notice.value = ''
  try {
    const result = await resetSession(selected.value)
    if (stopped || current !== revision) return
    detail.value = result
    notice.value = '上下文和待处理消息已清空。正在生成的回复仍会发出，但不会写回上下文。'
    await refreshList()
  } catch (cause) {
    if (!stopped && current === revision)
      error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    resetting.value = false
  }
}

function messageStatus(message: ChatMessage): string {
  return { received: '已收到', ignored: '未处理', sent: '已发送', failed: '发送失败' }[
    message.status
  ]
}

watch(selected, () => void loadSelection())
watch(onlySelected, subscribe)
onMounted(() => void refreshList())
onUnmounted(() => {
  stopped = true
  revision++
  listRevision++
  clearTimeout(refreshTimer)
})
</script>

<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>会话</h1>
        <p class="muted">查看聊天记录与当前模型上下文</p>
      </div>
      <div class="actions">
        <span :class="stream.connected.value ? 'success' : 'warning'">{{
          stream.connected.value ? '实时连接正常' : '实时连接重试中'
        }}</span
        ><button type="button" @click="refreshAll">刷新</button>
      </div>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <div class="session-layout">
      <section class="panel session-list" aria-label="好友会话">
        <label class="field"
          >搜索好友<input v-model="search" type="search" placeholder="QQ 号或昵称"
        /></label>
        <p v-if="!visibleSessions.length" class="empty">暂无会话</p>
        <button
          v-for="session in visibleSessions"
          :key="session.user_id"
          type="button"
          :class="{ selected: selected === session.user_id }"
          :aria-pressed="selected === session.user_id"
          @click="selected = session.user_id"
        >
          <strong>{{ session.nickname || session.user_id }}</strong
          ><span class="muted">{{ session.user_id }}</span>
          <span>{{ session.turns }} 轮 · {{ session.busy ? '等待模型回复' : '空闲' }}</span
          ><small class="muted">{{ time(session.last_active) }}</small>
        </button>
      </section>
      <section class="panel conversation" aria-label="聊天记录">
        <template v-if="selected">
          <div class="actions">
            <h2>
              {{ detail?.nickname || selected }} <small class="muted">{{ selected }}</small>
            </h2>
            <button type="button" :disabled="resetting || loading" @click="reset">
              {{ resetting ? '重置中…' : '重置上下文' }}
            </button>
          </div>
          <p class="muted">重置会清空模型上下文和待处理消息。聊天记录会继续保留。</p>
          <label class="check"
            ><input v-model="onlySelected" type="checkbox" />只接收当前好友的实时事件</label
          >
          <p v-if="loading" role="status">正在读取…</p>
          <button v-if="hasOlder" type="button" :disabled="loadingOlder" @click="older">
            {{ loadingOlder ? '读取中…' : '加载更早的消息' }}
          </button>
          <div
            ref="viewport"
            class="messages"
            role="log"
            aria-label="逐条聊天记录"
            aria-live="polite"
            aria-relevant="additions"
          >
            <p v-if="!loading && !messages.length" class="empty">暂无聊天记录</p>
            <article
              v-for="message in messages"
              :key="message.id"
              :class="['message', message.direction]"
            >
              <div class="message-meta">
                <strong>{{
                  message.direction === 'incoming' ? message.nickname || message.user_id : '机器人'
                }}</strong
                ><time :datetime="new Date(message.timestamp * 1000).toISOString()">{{
                  time(message.timestamp)
                }}</time
                ><span
                  :class="{
                    error: message.status === 'failed',
                    warning: message.status === 'ignored',
                  }"
                  >{{ messageStatus(message) }}</span
                >
              </div>
              <pre>{{ message.content }}</pre>
              <p v-if="message.error" class="muted">{{ message.error }}</p>
            </article>
          </div>
          <details v-if="detail">
            <summary>
              当前模型上下文：{{ detail.turns }} 轮{{ detail.busy ? '，正在等待模型回复' : '' }}
            </summary>
            <p class="muted">上下文只保存在内存，重启后清空。</p>
            <div v-for="(turn, index) in detail.history" :key="index" class="context-turn">
              <strong>第 {{ index + 1 }} 轮</strong>
              <pre>好友：{{ turn.user }}</pre>
              <pre>助手：{{ turn.assistant }}</pre>
            </div>
          </details>
        </template>
        <p v-else class="empty">选择一个好友查看会话</p>
      </section>
    </div>
  </div>
</template>

<style scoped>
.session-layout {
  display: grid;
  grid-template-columns: minmax(14rem, 19rem) minmax(0, 1fr);
  gap: 1rem;
}
.session-list {
  align-self: start;
  max-height: 78vh;
  overflow-y: auto;
}
.session-list button {
  display: grid;
  width: 100%;
  gap: 0.2rem;
  margin-top: 0.7rem;
  text-align: left;
}
.session-list button.selected {
  border-color: var(--color-accent);
  background: var(--color-bg);
}
.messages {
  display: grid;
  gap: 0.75rem;
  max-height: 65vh;
  overflow-y: auto;
  padding: 1rem 0;
}
.message {
  padding: 0.75rem;
  border: 1px solid var(--color-border);
  border-radius: 8px;
  max-width: 95%;
}
.message.outgoing {
  margin-left: 2rem;
  background: var(--color-bg);
}
.message-meta {
  display: flex;
  flex-wrap: wrap;
  gap: 0.6rem;
  font-size: 0.8rem;
  color: var(--color-muted);
}
.message pre {
  margin: 0.5rem 0 0;
}
.context-turn {
  border-top: 1px solid var(--color-border);
  padding-top: 0.75rem;
}
@media (max-width: 900px) {
  .session-layout {
    grid-template-columns: 1fr;
  }
  .session-list {
    max-height: 18rem;
  }
}
</style>
