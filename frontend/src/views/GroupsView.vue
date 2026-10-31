<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getGroups, saveGroup, getFeature, saveFeature, type Group } from '@/api/features'
const groups = ref<Group[]>([])
const revision = ref('')
const admins = ref('')
const adminsRevision = ref('')
const busy = ref(false)
const error = ref('')
const notice = ref('')
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    const [list, administrators] = await Promise.all([getGroups(), getFeature<string[]>('admins')])
    groups.value = list.groups
    revision.value = list.revision
    admins.value = administrators.values.join('\n')
    adminsRevision.value = administrators.revision
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function save(group?: Group): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    const result = group
      ? await saveGroup(group.group_id, group.values, revision.value)
      : await saveFeature(
          'admins',
          admins.value.split(/[\s,，]+/).filter(Boolean),
          adminsRevision.value,
        )
    revision.value = result.revision
    adminsRevision.value = result.revision
    notice.value = '已保存，下一条消息起生效。'
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
onMounted(() => void load())
</script>
<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>群聊</h1>
        <p class="muted">群默认关闭。自动回复关闭时，提及机器人才能触发回复。</p>
      </div>
      <button :disabled="busy" @click="load">刷新群列表</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form class="panel" @submit.prevent="save()">
      <h2>群命令管理员</h2>
      <label class="field"
        >QQ 号（每行一个）<textarea v-model="admins" rows="3" :disabled="busy" />
      </label>
      <p class="muted">
        仅用于群命令权限。支持 /on、/off、/auto on、/auto off、/prompt 提示词、/reset、/help。
      </p>
      <button :disabled="busy">保存管理员</button>
    </form>
    <p v-if="!busy && !groups.length" class="muted">当前账号没有加入群。</p>
    <form v-for="group in groups" :key="group.group_id" class="panel" @submit.prevent="save(group)">
      <h2>
        {{ group.name }}
        <small class="muted">{{ group.group_id }} · {{ group.member_count }} 人</small>
      </h2>
      <fieldset :disabled="busy">
        <div class="switches">
          <label><input v-model="group.values.enabled" type="checkbox" /> 启用会话</label
          ><label><input v-model="group.values.auto_reply" type="checkbox" /> 自动回复</label
          ><label
            ><input v-model="group.values.include_card" type="checkbox" /> 向模型提供群名片</label
          ><label
            ><input v-model="group.values.commands_enabled" type="checkbox" /> 启用群命令</label
          >
        </div>
        <label class="field"
          >群附加提示词<textarea
            v-model="group.values.additional_prompt"
            rows="4"
            maxlength="16000"
          /></label
        ><button>保存此群</button>
      </fieldset>
    </form>
  </div>
</template>
<style scoped>
.switches {
  display: flex;
  flex-wrap: wrap;
  gap: 1rem;
  margin-bottom: 1rem;
}
small {
  font-size: 0.8rem;
  font-weight: normal;
}
</style>
