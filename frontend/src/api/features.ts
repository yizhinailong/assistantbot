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

export interface FeatureRecord<T> {
  key: string
  value: T
  revision: string
  updated_at?: number
}
export interface Sticker {
  emoji_id: string
  res_id: string
  md5: string
  url: string
  description: string
}
export function getStickers(): Promise<{ stickers: Sticker[] }> {
  return request('/api/admin/stickers')
}
export function renameSticker(sticker: Sticker): Promise<unknown> {
  return request(`/api/admin/stickers/${encodeURIComponent(sticker.res_id)}`, {
    method: 'PUT',
    body: sticker,
  })
}
export function deleteSticker(id: string): Promise<unknown> {
  return request(`/api/admin/stickers/${encodeURIComponent(id)}`, { method: 'DELETE' })
}
export function addSticker(file: string): Promise<unknown> {
  return request('/api/admin/stickers', { method: 'POST', body: { file } })
}
export interface CharacterImage {
  data_url: string
  bytes: number
}
export function getCharacter(): Promise<FeatureRecord<CharacterImage | null>> {
  return request('/api/admin/character-image')
}
export function saveCharacter(
  data_url: string,
  revision: string,
): Promise<FeatureRecord<CharacterImage>> {
  return request('/api/admin/character-image', { method: 'POST', body: { data_url, revision } })
}
export function deleteCharacter(revision: string): Promise<FeatureRecord<null>> {
  return request(`/api/admin/character-image?revision=${encodeURIComponent(revision)}`, {
    method: 'DELETE',
  })
}
