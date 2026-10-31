import { request } from './client'

export interface FeatureSection<T> {
  values: T
  defaults: T
  revision: string
}
export interface GroupValues {
  name: string
  enabled: boolean
  auto_reply: boolean
  include_card: boolean
  commands_enabled: boolean
  additional_prompt: string
}
export interface Group {
  group_id: string
  name: string
  member_count: number
  values: GroupValues
}
export function getFeature<T>(section: string): Promise<FeatureSection<T>> {
  return request(`/api/admin/features/${encodeURIComponent(section)}`)
}
export function saveFeature<T>(
  section: string,
  values: T,
  revision: string,
): Promise<FeatureSection<T>> {
  return request(`/api/admin/features/${encodeURIComponent(section)}`, {
    method: 'PUT',
    body: { values, revision },
  })
}
export function getGroups(): Promise<{ groups: Group[]; revision: string }> {
  return request('/api/admin/groups')
}
export function saveGroup(
  id: string,
  values: GroupValues,
  revision: string,
): Promise<FeatureSection<Record<string, GroupValues>>> {
  return request(`/api/admin/groups/${encodeURIComponent(id)}`, {
    method: 'PUT',
    body: { values, revision },
  })
}
