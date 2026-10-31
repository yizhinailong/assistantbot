import { onMounted, onUnmounted, ref } from 'vue'
import router from '@/router'
import { clearAuthentication, safeInternalRoute } from '@/auth'
import { getAuthenticationStatus, type ChatMessage, type LogBatch } from '@/api/admin'

export interface LiveEvent extends Partial<LogBatch> {
  type: string
  user_id?: string
  message?: ChatMessage
  error?: string
}

/** Reconnect authenticated streams and let each view recover persisted gaps on opening. */
export function useAdminStream(
  path: string,
  onEvent: (event: LiveEvent) => void,
  onOpen: () => void,
) {
  const connected = ref(false)
  const error = ref('')
  let socket: WebSocket | undefined
  let timer: ReturnType<typeof setTimeout> | undefined
  let stopped = false
  let delay = 1000

  function schedule(): void {
    if (stopped) return
    timer = setTimeout(() => void connect(), delay)
    delay = Math.min(delay * 2, 30_000)
  }

  async function connect(): Promise<void> {
    if (stopped) return
    try {
      const status = await getAuthenticationStatus()
      if (stopped) return
      if (!status.authenticated) {
        clearAuthentication()
        await router.replace({
          name: 'login',
          query: { redirect: safeInternalRoute(router.currentRoute.value.fullPath) },
        })
        return
      }
    } catch {
      error.value = '连接中断，正在重试…'
      schedule()
      return
    }
    const url = new URL(path, window.location.href)
    url.protocol = url.protocol === 'https:' ? 'wss:' : 'ws:'
    const next = new WebSocket(url)
    socket = next
    next.onopen = () => {
      if (stopped || socket !== next) return
      connected.value = true
      error.value = ''
      delay = 1000
      onOpen()
    }
    next.onmessage = (message) => {
      if (stopped || socket !== next) return
      try {
        const event: unknown = JSON.parse(String(message.data))
        if (event && typeof event === 'object' && 'type' in event && typeof event.type === 'string')
          onEvent(event as LiveEvent)
      } catch {
        error.value = '实时消息格式错误，请刷新页面。'
      }
    }
    next.onerror = () => {
      error.value = '实时连接中断，正在重试…'
    }
    next.onclose = () => {
      if (socket !== next) return
      connected.value = false
      socket = undefined
      schedule()
    }
  }

  function send(value: unknown): void {
    if (socket?.readyState === WebSocket.OPEN) socket.send(JSON.stringify(value))
  }

  onMounted(() => void connect())
  onUnmounted(() => {
    stopped = true
    clearTimeout(timer)
    socket?.close()
  })
  return { connected, error, send }
}
