import router from '@/router'
import { clearAuthentication, safeInternalRoute } from '@/auth'

/**
 * A failed request to the bot: status 0 when the bot could not be reached, otherwise the
 * HTTP status of a non-2xx answer.
 */
export class ApiError extends Error {
  readonly status: number

  constructor(status: number, message: string) {
    super(message)
    this.name = 'ApiError'
    this.status = status
  }
}

export interface RequestOptions {
  method?: 'GET' | 'POST' | 'PUT' | 'DELETE'
  /** Sent as JSON when set. */
  body?: unknown
  /** Authentication endpoints handle their own 401 answers. */
  redirectOnUnauthorized?: boolean
}

/**
 * Send a request to the bot's API and parse its JSON answer.
 *
 * An empty answer, such as a 204, resolves to `undefined`. A non-2xx answer throws `ApiError`
 * with the message of an `{"error": "..."}` body, or else with the status line.
 */
export async function request<T>(path: string, options: RequestOptions = {}): Promise<T> {
  const { method = 'GET', body, redirectOnUnauthorized = true } = options
  const init: RequestInit = { method, credentials: 'same-origin' }
  if (body !== undefined) {
    init.headers = { 'Content-Type': 'application/json' }
    init.body = JSON.stringify(body)
  }

  let response: Response
  try {
    response = await fetch(path, init)
  } catch {
    throw new ApiError(0, '无法连接到 bot')
  }

  const text = await response.text()
  if (!response.ok) {
    if (response.status === 401 && redirectOnUnauthorized) {
      clearAuthentication()
      const current = router.currentRoute.value
      if (current.name !== 'login') {
        await router.replace({
          name: 'login',
          query: { redirect: safeInternalRoute(current.fullPath) },
        })
      }
    }
    throw new ApiError(response.status, errorMessage(response, text))
  }
  if (text === '') {
    return undefined as T
  }
  try {
    return JSON.parse(text) as T
  } catch {
    throw new ApiError(response.status, '响应不是 JSON')
  }
}

/** Read the message of an `{"error": "..."}` body, or fall back to the status line. */
function errorMessage(response: Response, text: string): string {
  try {
    const data: unknown = JSON.parse(text)
    if (typeof data === 'object' && data !== null && 'error' in data) {
      if (typeof data.error === 'string' && data.error !== '') {
        return data.error
      }
    }
  } catch {
    // Not JSON, such as drogon's HTML error pages.
  }
  return `HTTP ${response.status} ${response.statusText}`.trim()
}
