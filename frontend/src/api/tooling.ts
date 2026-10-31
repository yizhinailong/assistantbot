import { request } from './client'
import type { FeatureRecord } from './features'
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
export interface ToolDefinition {
  name: string
  description: string
  parameters: Record<string, unknown>
  group_only: boolean
}
export interface ToolConfig {
  enabled: boolean
  builtin: Record<string, { enabled: boolean; description: string }>
}
export interface LuaTool {
  enabled: boolean
  description: string
  parameters: Record<string, unknown>
  script: string
}
export function getBuiltins(): Promise<{ tools: ToolDefinition[] }> {
  return request('/api/admin/tools/builtin')
}
export function getLuaTools(): Promise<{ tools: FeatureRecord<LuaTool>[] }> {
  return request('/api/admin/tools/lua')
}
export function saveLuaTool(
  name: string,
  value: LuaTool,
  revision: string,
): Promise<FeatureRecord<LuaTool>> {
  return request(`/api/admin/tools/lua/${encodeURIComponent(name)}`, {
    method: 'PUT',
    body: { value, revision },
  })
}
export function deleteLuaTool(name: string, revision: string): Promise<unknown> {
  return request(
    `/api/admin/tools/lua/${encodeURIComponent(name)}?${new URLSearchParams({ revision })}`,
    { method: 'DELETE' },
  )
}
export function testLuaTool(
  name: string,
  value: LuaTool,
  args: Record<string, unknown>,
): Promise<unknown> {
  return request('/api/admin/tools/lua-test', {
    method: 'POST',
    body: { name, value, arguments: args },
  })
}
export interface McpServer {
  name: string
  description: string
  url: string
  token_env: string
  enabled: boolean
  timeout: number
  protocol_version: string
}
export interface McpConfig {
  servers: McpServer[]
}
export function probeMcp(name: string): Promise<{ tools: unknown[] }> {
  return request(`/api/admin/mcp/${encodeURIComponent(name)}/test`, { method: 'POST' })
}
