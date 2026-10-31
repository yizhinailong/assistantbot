import { request } from './client'

export interface AuthenticationStatus {
  authenticated: boolean
}

export interface BotIdentity {
  user_id: string
  nickname: string
}

export interface NapCatStatus {
  online: boolean | null
  good: boolean | null
  app_name: string | null
  app_version: string | null
  protocol_version: string | null
}

export interface RuntimeStatus {
  version: string
  started_at: number
  model: string
  session_count: number
  bot: BotIdentity | null
  napcat: NapCatStatus
  paused: boolean
  today_usage: DailyUsage
}

export interface DailyUsage {
  from: number
  to: number
  timezone: 'UTC'
  requests: number
  successful_requests: number
  reported_requests: number
  prompt_tokens: number
  completion_tokens: number
  total_tokens: number
  cached_requests: number
  cached_tokens: number
}

export interface BotStatus {
  paused: boolean
}

export interface ProcessMetricSample {
  timestamp: number
  rss_bytes: number | null
  peak_rss_bytes: number | null
  threads: number | null
}

export interface ProcessMetrics {
  interval_seconds: 60
  capacity: 1440
  samples: ProcessMetricSample[]
}

export type AccessMode = 'all_friends' | 'allowlist'
export type AccessList = 'allowlist' | 'blacklist'

export interface AccessSettings {
  mode: AccessMode
}

export interface AccessEntries {
  qqs: string[]
}

export interface FriendInfo {
  user_id: string
  nickname: string
  remark: string
  in_allowlist: boolean
  in_blacklist: boolean
  allowed: boolean
}

export interface Friends {
  friends: FriendInfo[]
}

export function getAuthenticationStatus(): Promise<AuthenticationStatus> {
  return request<AuthenticationStatus>('/api/admin/auth/status', { redirectOnUnauthorized: false })
}

export function login(token: string): Promise<AuthenticationStatus> {
  return request<AuthenticationStatus>('/api/admin/auth/login', {
    method: 'POST',
    body: { token },
    redirectOnUnauthorized: false,
  })
}

export function logout(): Promise<AuthenticationStatus> {
  return request<AuthenticationStatus>('/api/admin/auth/logout', {
    method: 'POST',
    redirectOnUnauthorized: false,
  })
}

export function getRuntimeStatus(): Promise<RuntimeStatus> {
  return request<RuntimeStatus>('/api/admin/status')
}

export function getBotStatus(): Promise<BotStatus> {
  return request<BotStatus>('/api/admin/bot-status')
}

export function setBotPaused(paused: boolean): Promise<BotStatus> {
  return request<BotStatus>('/api/admin/bot-status', { method: 'POST', body: { paused } })
}

export function getProcessMetrics(): Promise<ProcessMetrics> {
  return request<ProcessMetrics>('/api/admin/process-metrics')
}

export function getAccessSettings(): Promise<AccessSettings> {
  return request<AccessSettings>('/api/admin/access')
}

export function setAccessMode(mode: AccessMode): Promise<AccessSettings> {
  return request<AccessSettings>('/api/admin/access', { method: 'PUT', body: { mode } })
}

export function getAccessEntries(list: AccessList): Promise<AccessEntries> {
  return request<AccessEntries>(`/api/admin/${list}`)
}

export function setAccessEntry(
  list: AccessList,
  qq: string,
  present: boolean,
): Promise<AccessEntries> {
  return present
    ? request<AccessEntries>(`/api/admin/${list}`, { method: 'POST', body: { qq } })
    : request<AccessEntries>(`/api/admin/${list}/${encodeURIComponent(qq)}`, { method: 'DELETE' })
}

export function getFriends(): Promise<Friends> {
  return request<Friends>('/api/admin/friends')
}

export interface SessionSummary {
  user_id: string
  nickname: string
  turns: number
  last_active: number
  busy: boolean
}

export interface SessionDetail extends SessionSummary {
  history: { user: string; assistant: string }[]
}

export interface ChatMessage {
  id: string
  timestamp: number
  user_id: string
  nickname: string
  direction: 'incoming' | 'outgoing'
  content: string
  status: 'received' | 'ignored' | 'sent' | 'failed'
  error: string
}

export interface MessagePage {
  messages: ChatMessage[]
  has_more: boolean
  next_before: string | null
  next_after: string | null
}

export interface ReplyValues {
  system_prompt: string
  history_turns: number
  idle_reset_minutes: number
  request_timeout: number
  max_tokens: number | null
  max_reply_chars: number
  model: string
}

export interface ReplySettings {
  values: ReplyValues
  overrides: Partial<ReplyValues>
  defaults: ReplyValues
  default_system_prompt: string
}

export type UsageCounters = Omit<DailyUsage, 'from' | 'to' | 'timezone'>
export interface UsageReport {
  from: number
  to: number
  timezone: 'UTC'
  total: UsageCounters
  today: DailyUsage
  days: (UsageCounters & { timestamp: number })[]
  users: (UsageCounters & { user_id: string })[]
}

export type LogLevel = 'TRACE' | 'DEBUG' | 'INFO' | 'WARN' | 'ERROR' | 'FATAL'
export interface LogFilter {
  level: LogLevel | ''
  keyword: string
  qq: string
}
export interface LogEntry {
  id: string
  timestamp: number
  level: LogLevel
  message: string
}
export interface LogBatch {
  entries: LogEntry[]
  cursor: string
  oldest_id: string
  truncated: boolean
  log_level: LogLevel
  filters: LogFilter
}

export function getSessions(): Promise<{ sessions: SessionSummary[] }> {
  return request('/api/admin/sessions')
}
export function getSession(qq: string): Promise<SessionDetail> {
  return request(`/api/admin/sessions/${encodeURIComponent(qq)}`)
}
export function resetSession(qq: string): Promise<SessionDetail> {
  return request(`/api/admin/sessions/${encodeURIComponent(qq)}/reset`, { method: 'POST' })
}
export function getMessages(
  qq: string,
  options: { before?: string; after?: string; limit?: number } = {},
): Promise<MessagePage> {
  const query = new URLSearchParams()
  for (const [key, value] of Object.entries(options)) query.set(key, String(value))
  return request(`/api/admin/sessions/${encodeURIComponent(qq)}/messages?${query}`)
}
export function getReplySettings(): Promise<ReplySettings> {
  return request('/api/admin/bot-config')
}
export function saveReplySettings(overrides: Partial<ReplyValues>): Promise<ReplySettings> {
  return request('/api/admin/bot-config', { method: 'PUT', body: overrides })
}
export function getUsage(from: number, to: number): Promise<UsageReport> {
  return request(`/api/admin/usage?${new URLSearchParams({ from: String(from), to: String(to) })}`)
}
export function getLogs(filter: LogFilter, after?: string): Promise<LogBatch> {
  const query = new URLSearchParams({ ...filter, limit: '200' })
  if (after !== undefined) query.set('after', after)
  return request(`/api/admin/logs?${query}`)
}
export function setLogLevel(level: LogLevel): Promise<{ level: LogLevel }> {
  return request('/api/admin/log-level', { method: 'PUT', body: { level } })
}
