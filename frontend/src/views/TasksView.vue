<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { cancelTask, createTask, getTasks, type ScheduledTask } from '@/api/tooling'
const tasks = ref<ScheduledTask[]>([])
const kind = ref('private'),
  qq = ref(''),
  due = ref(''),
  content = ref(''),
  daily = ref(false)
const busy = ref(false),
  error = ref(''),
  notice = ref('')
const states: Record<string, string> = {
  pending: '等待执行',
  running: '正在执行',
  done: '已完成',
  failed: '失败',
  cancelled: '已取消',
}
function session(): string {
  return qq.value.trim() ? `${kind.value === 'group' ? '-' : ''}${qq.value.trim()}` : ''
}
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    tasks.value = (await getTasks(session())).tasks
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function create(): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    const date = new Date(due.value).getTime()
    if (!session() || !Number.isFinite(date)) throw new Error('请填写会话和执行时间。')
    await createTask(session(), Math.floor(date / 1000), content.value, daily.value)
    content.value = ''
    notice.value = '任务已保存。'
    tasks.value = (await getTasks(session())).tasks
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function cancel(id: string): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    await cancelTask(id)
    tasks.value = (await getTasks(session())).tasks
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
        <h1>定时任务</h1>
        <p class="muted">到期后在所属会话执行任务内容，发送前会检查暂停状态和访问权限。</p>
      </div>
      <button :disabled="busy" @click="load">刷新</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form class="panel" @submit.prevent="create">
      <fieldset :disabled="busy">
        <div class="actions">
          <label class="field"
            >会话类型<select v-model="kind">
              <option value="private">好友</option>
              <option value="group">群聊</option>
            </select></label
          ><label class="field"
            >QQ 号 / 群号<input v-model="qq" inputmode="numeric" pattern="[0-9]+" /></label
          ><button type="button" @click="load">按会话查询</button>
        </div>
        <label class="field"
          >执行时间（本地时间）<input v-model="due" type="datetime-local" required /></label
        ><label class="field"
          >任务内容<textarea v-model="content" maxlength="8192" required rows="3" />
        </label>
        <label
          ><input v-model="daily" type="checkbox" /> 每天重复（固定 UTC 时间，每 24 小时）</label
        >
        <div class="actions"><button class="primary">创建任务</button></div>
      </fieldset>
    </form>
    <section class="panel">
      <p v-if="!tasks.length" class="muted">暂无任务。</p>
      <div v-for="task in tasks" :key="task.id" class="task">
        <div class="actions">
          <strong
            >#{{ task.id }} ·
            {{
              task.session_id.startsWith('-')
                ? '群 ' + task.session_id.slice(1)
                : '好友 ' + task.session_id
            }}</strong
          ><span>{{ states[task.status] ?? task.status }}</span
          ><button v-if="task.status === 'pending'" :disabled="busy" @click="cancel(task.id)">
            取消
          </button>
        </div>
        <p>{{ task.content }}</p>
        <small
          >{{ new Date(task.due_at * 1000).toLocaleString()
          }}{{ task.daily ? ' · 每天' : '' }}</small
        >
        <p v-if="task.error" class="feedback error">{{ task.error }}</p>
      </div>
    </section>
  </div>
</template>
<style scoped>
.task + .task {
  border-top: 1px solid var(--border);
  padding-top: 1rem;
  margin-top: 1rem;
}
input[type='checkbox'] {
  width: auto;
}
.task p {
  white-space: pre-wrap;
  overflow-wrap: anywhere;
}
</style>
