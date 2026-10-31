<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  getAccessEntries,
  getAccessSettings,
  getFriends,
  setAccessEntry,
  setAccessMode,
  type AccessList,
  type AccessMode,
  type FriendInfo,
} from '@/api/admin'
import { ApiError } from '@/api/client'

const mode = ref<AccessMode | null>(null)
const draftMode = ref<AccessMode>('all_friends')
const allowlist = ref<string[]>([])
const blacklist = ref<string[]>([])
const friends = ref<FriendInfo[]>([])
const loading = ref(false)
const saving = ref(false)
const errorMessage = ref('')
const friendsError = ref('')
const notice = ref('')
const manualQq = ref('')
const search = ref('')
const listFilter = ref<'all' | AccessList>('all')
const busy = computed(() => loading.value || saving.value)
const disabled = computed(() => busy.value || mode.value === null)
const allowedIds = computed(() => new Set(allowlist.value))
const blockedIds = computed(() => new Set(blacklist.value))

const rows = computed(() => {
  const identities = new Map(friends.value.map((friend) => [friend.user_id, friend]))
  const ids = new Set([...identities.keys(), ...allowlist.value, ...blacklist.value])
  const query = search.value.trim().toLocaleLowerCase('zh-CN')
  return [...ids]
    .map((qq) => {
      const friend = identities.get(qq)
      const inAllowlist = allowedIds.value.has(qq)
      const inBlacklist = blockedIds.value.has(qq)
      return {
        qq,
        nickname: friend?.nickname ?? '',
        remark: friend?.remark ?? '',
        isFriend: friend !== undefined,
        inAllowlist,
        inBlacklist,
        allowed: !inBlacklist && (mode.value === 'all_friends' || inAllowlist),
      }
    })
    .filter((row) => {
      if (listFilter.value === 'allowlist' && !row.inAllowlist) return false
      if (listFilter.value === 'blacklist' && !row.inBlacklist) return false
      return [row.qq, row.nickname, row.remark].some((text) =>
        text.toLocaleLowerCase('zh-CN').includes(query),
      )
    })
    .sort((a, b) => a.qq.length - b.qq.length || a.qq.localeCompare(b.qq))
})

function describeError(error: unknown): string {
  if (error instanceof ApiError) {
    if (error.status === 409) return '有另一项访问设置正在保存，请稍后重试。'
    if (error.status === 502) return '暂时无法读取好友，请确认 NapCat 在线后刷新。'
    if (error.status === 500) return '访问设置操作失败，请重试并检查 bot 日志。'
  }
  return error instanceof Error ? error.message : String(error)
}

async function refresh(): Promise<void> {
  if (busy.value) return
  loading.value = true
  errorMessage.value = ''
  friendsError.value = ''
  notice.value = ''
  const [policyResult, friendsResult] = await Promise.allSettled([
    Promise.all([
      getAccessSettings(),
      getAccessEntries('allowlist'),
      getAccessEntries('blacklist'),
    ]),
    getFriends(),
  ])
  if (policyResult.status === 'fulfilled') {
    const [settings, allowed, blocked] = policyResult.value
    mode.value = settings.mode
    draftMode.value = settings.mode
    allowlist.value = allowed.qqs
    blacklist.value = blocked.qqs
  } else {
    mode.value = null
    errorMessage.value = `读取访问设置失败：${describeError(policyResult.reason)}`
  }
  if (friendsResult.status === 'fulfilled') {
    friends.value = friendsResult.value.friends
  } else {
    friends.value = []
    friendsError.value = `${describeError(friendsResult.reason)} 仍可按 QQ 号管理名单。`
  }
  loading.value = false
}

async function saveMode(): Promise<void> {
  if (disabled.value || draftMode.value === mode.value) return
  saving.value = true
  errorMessage.value = ''
  notice.value = ''
  try {
    const saved = await setAccessMode(draftMode.value)
    mode.value = saved.mode
    draftMode.value = saved.mode
    notice.value = '回复范围已保存，将用于接下来的新消息。'
  } catch (error) {
    errorMessage.value = describeError(error)
  } finally {
    saving.value = false
  }
}

async function changeEntry(list: AccessList, qq: string, present: boolean): Promise<void> {
  if (disabled.value) return
  saving.value = true
  errorMessage.value = ''
  notice.value = ''
  try {
    const saved = await setAccessEntry(list, qq, present)
    if (list === 'allowlist') allowlist.value = saved.qqs
    else blacklist.value = saved.qqs
    notice.value = `${qq} 已${present ? '加入' : '移出'}${list === 'allowlist' ? '允许名单' : '黑名单'}。`
  } catch (error) {
    errorMessage.value = describeError(error)
  } finally {
    saving.value = false
  }
}

async function addManual(list: AccessList): Promise<void> {
  const qq = manualQq.value.trim()
  if (!/^\d+$/.test(qq) || BigInt(qq) <= 0n || BigInt(qq) > 9223372036854775807n) {
    errorMessage.value = '请输入有效的 QQ 号（正整数，最大 9223372036854775807）。'
    notice.value = ''
    return
  }
  await changeEntry(list, BigInt(qq).toString(), true)
}

onMounted(() => void refresh())
</script>

<template>
  <header class="page-header">
    <div>
      <h1>访问控制</h1>
      <p class="muted">设置哪些私聊好友可以收到回复。保存后立即用于新消息，重启后保留。</p>
    </div>
    <button type="button" :disabled="busy" @click="refresh">
      {{ loading ? '刷新中…' : '刷新' }}
    </button>
  </header>

  <p v-if="errorMessage" class="form-error feedback" role="alert">{{ errorMessage }}</p>
  <p v-if="notice" class="success feedback" role="status">{{ notice }}</p>
  <p v-if="loading && mode === null" class="muted" role="status">正在读取访问设置…</p>

  <section class="card mode-card" aria-labelledby="mode-title" :aria-busy="busy">
    <h2 id="mode-title">回复范围</h2>
    <form @submit.prevent="saveMode">
      <fieldset :disabled="disabled">
        <legend class="muted">黑名单优先于以下设置，命中后也不会执行 /help 和 /reset。</legend>
        <label class="mode-option">
          <input v-model="draftMode" type="radio" name="access-mode" value="all_friends" />
          <span>所有好友 <small class="muted">回复所有未在黑名单中的好友</small></span>
        </label>
        <label class="mode-option">
          <input v-model="draftMode" type="radio" name="access-mode" value="allowlist" />
          <span
            >只回复允许名单内的好友 <small class="muted">允许名单为空时不回复任何好友</small></span
          >
        </label>
        <button class="primary" type="submit" :disabled="draftMode === mode">保存回复范围</button>
      </fieldset>
    </form>
    <p class="muted note">已接受的消息会继续处理。群消息和临时会话仍会忽略。</p>
  </section>

  <section class="card friends-card" aria-labelledby="friends-title" :aria-busy="busy">
    <div class="table-header">
      <div>
        <h2 id="friends-title">好友和名单</h2>
        <p class="muted counts">
          好友 {{ friends.length }} · 允许名单 {{ allowlist.length }} · 黑名单
          {{ blacklist.length }}
        </p>
      </div>
      <span v-if="saving" class="muted" role="status">保存中…</span>
    </div>
    <p v-if="friendsError" class="warning" role="alert">{{ friendsError }}</p>

    <form class="manual-entry" @submit.prevent="addManual('allowlist')">
      <div>
        <label for="manual-qq">按 QQ 号添加</label>
        <input
          id="manual-qq"
          v-model="manualQq"
          type="text"
          inputmode="numeric"
          maxlength="19"
          placeholder="输入 QQ 号"
          :disabled="disabled"
        />
      </div>
      <button type="submit" :disabled="disabled || !manualQq.trim()">加入允许名单</button>
      <button
        type="button"
        :disabled="disabled || !manualQq.trim()"
        @click="addManual('blacklist')"
      >
        加入黑名单
      </button>
    </form>

    <div class="filters">
      <div>
        <label for="friend-search">查找好友</label>
        <input id="friend-search" v-model="search" type="search" placeholder="QQ 号、昵称或备注" />
      </div>
      <div>
        <label for="list-filter">显示范围</label>
        <select id="list-filter" v-model="listFilter">
          <option value="all">全部好友和名单</option>
          <option value="allowlist">允许名单</option>
          <option value="blacklist">黑名单</option>
        </select>
      </div>
    </div>

    <div class="table-scroll" tabindex="0" role="region" aria-label="好友访问名单表格">
      <table>
        <caption class="muted">
          回复状态仅表示名单规则；暂停期间仍不会回复。仅在名单中的 QQ 号也会列出，便于移除。
        </caption>
        <thead>
          <tr>
            <th scope="col">QQ 号</th>
            <th scope="col">昵称 / 备注</th>
            <th scope="col">回复状态</th>
            <th scope="col">允许名单</th>
            <th scope="col">黑名单</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="row in rows" :key="row.qq">
            <th scope="row">{{ row.qq }}</th>
            <td>
              <span>{{ row.nickname || '—' }}</span>
              <small class="muted">{{
                row.remark || (row.isFriend ? '无备注' : '仅名单记录')
              }}</small>
            </td>
            <td :class="mode !== null && row.allowed ? 'success' : 'muted'">
              {{
                mode === null
                  ? '未知'
                  : row.inBlacklist
                    ? '黑名单拦截'
                    : row.allowed
                      ? row.isFriend
                        ? '可回复'
                        : '好友私聊时可回复'
                      : '不在允许名单'
              }}
            </td>
            <td>
              <button
                type="button"
                :disabled="disabled"
                :aria-label="`${row.inAllowlist ? '移出允许名单' : '加入允许名单'} ${row.qq}`"
                @click="changeEntry('allowlist', row.qq, !row.inAllowlist)"
              >
                {{ row.inAllowlist ? '移出允许名单' : '加入允许名单' }}
              </button>
            </td>
            <td>
              <button
                type="button"
                :disabled="disabled"
                :aria-label="`${row.inBlacklist ? '移出黑名单' : '加入黑名单'} ${row.qq}`"
                @click="changeEntry('blacklist', row.qq, !row.inBlacklist)"
              >
                {{ row.inBlacklist ? '移出黑名单' : '加入黑名单' }}
              </button>
            </td>
          </tr>
          <tr v-if="rows.length === 0">
            <td colspan="5" class="muted empty">
              {{
                loading
                  ? '读取中…'
                  : search || listFilter !== 'all'
                    ? '没有匹配的记录'
                    : '暂无好友和名单记录'
              }}
            </td>
          </tr>
        </tbody>
      </table>
    </div>
  </section>
</template>

<style scoped>
.page-header,
.table-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 1rem;
}

.page-header {
  margin-bottom: 1.25rem;
}

.page-header h1,
.page-header p,
.counts {
  margin: 0;
}

.page-header button {
  flex-shrink: 0;
}

.card {
  max-width: none;
  margin-bottom: 1.25rem;
}

fieldset {
  margin: 0;
  padding: 0;
  border: 0;
}

legend {
  padding: 0;
  margin-bottom: 0.75rem;
}

.mode-option {
  display: flex;
  align-items: flex-start;
  gap: 0.65rem;
  margin-bottom: 0.75rem;
}

.mode-option input {
  width: auto;
  margin: 0.35rem 0 0;
  accent-color: var(--color-accent);
}

small {
  display: block;
  font-weight: 400;
}

.note {
  margin-bottom: 0;
  font-size: 0.88rem;
}

.feedback {
  margin: 0 0 1rem;
}

.success {
  color: var(--color-ok);
}

.warning {
  color: var(--color-warning);
}

.manual-entry,
.filters {
  display: flex;
  align-items: flex-end;
  flex-wrap: wrap;
  gap: 0.75rem;
  margin: 1rem 0;
}

.manual-entry > div,
.filters > div {
  flex: 1 1 12rem;
}

label {
  display: block;
  margin-bottom: 0.3rem;
}

select {
  width: 100%;
  padding: 0.55rem 0.65rem;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  background: var(--color-bg);
  color: var(--color-text);
  font: inherit;
}

select:focus-visible,
.table-scroll:focus-visible {
  outline: 2px solid var(--color-accent);
  outline-offset: 2px;
}

.table-scroll {
  overflow-x: auto;
}

table {
  width: 100%;
  border-collapse: collapse;
  text-align: left;
  font-size: 0.9rem;
}

caption {
  padding-bottom: 0.75rem;
  text-align: left;
  font-size: 0.83rem;
}

th,
td {
  padding: 0.75rem 0.5rem;
  border-bottom: 1px solid var(--color-border);
}

td button {
  white-space: nowrap;
}

.empty {
  text-align: center;
}

@media (max-width: 600px) {
  .page-header {
    align-items: flex-start;
  }

  .card {
    padding: 1rem;
  }
}
</style>
