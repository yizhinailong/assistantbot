import { request } from './client'
export interface ScheduledTask {
  id: string
  session_id: string
  sender_id: string
  due_at: number
  content: string
  daily: boolean
  status: string
  error: string
}
export function getTasks(session = ''): Promise<{ tasks: ScheduledTask[] }> {
  return request(`/api/admin/tasks${session ? `?${new URLSearchParams({ session })}` : ''}`)
}
export function createTask(
  session: string,
  due_at: number,
  content: string,
  daily: boolean,
): Promise<ScheduledTask> {
  return request('/api/admin/tasks', { method: 'POST', body: { session, due_at, content, daily } })
}
export function cancelTask(id: string): Promise<unknown> {
  return request(`/api/admin/tasks/${encodeURIComponent(id)}/cancel`, { method: 'POST' })
}
