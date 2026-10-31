import { computed, ref } from 'vue'

const authenticated = ref<boolean | null>(null)

export const isAuthenticated = computed(() => authenticated.value === true)

export function setAuthenticated(value: boolean): void {
  authenticated.value = value
}

export function clearAuthentication(): void {
  authenticated.value = false
}

export function safeInternalRoute(value: unknown): string {
  if (
    typeof value !== 'string' ||
    !value.startsWith('/') ||
    value.startsWith('//') ||
    value.includes('\\') ||
    /^\/login(?:[/?#]|$)/i.test(value)
  ) {
    return '/'
  }
  return value
}
