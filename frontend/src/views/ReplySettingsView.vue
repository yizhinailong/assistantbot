<script setup lang="ts">
import { onMounted, ref } from 'vue'
import {
  getReplySettings,
  saveReplySettings,
  type ReplySettings,
  type ReplyValues,
} from '@/api/admin'

const settings = ref<ReplySettings>()
const form = ref<ReplyValues>()
const maxTokens = ref('')
const busy = ref(false)
const error = ref('')
const notice = ref('')

function apply(value: ReplySettings): void {
  settings.value = value
  form.value = { ...value.values }
  maxTokens.value = value.values.max_tokens === null ? '' : String(value.values.max_tokens)
}

async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    apply(await getReplySettings())
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}

async function save(restore = false): Promise<void> {
  if (busy.value || !form.value) return
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    apply(
      await saveReplySettings(
        restore
          ? {}
          : { ...form.value, max_tokens: maxTokens.value === '' ? null : Number(maxTokens.value) },
      ),
    )
    notice.value = restore
      ? '已恢复 config.json 中的默认设置。'
      : '设置已保存，从下一次回复起生效。'
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
        <h1>回复设置</h1>
        <p class="muted">保存后立即生效，正在生成的回复使用原设置。</p>
      </div>
      <button type="button" :disabled="busy" @click="load">重新读取</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <p v-if="busy && !form" role="status">正在读取设置…</p>
    <form v-if="form" class="panel" @submit.prevent="save()">
      <fieldset :disabled="busy">
        <label class="field"
          >系统提示词<textarea
            v-model="form.system_prompt"
            rows="10"
            placeholder="留空时不发送系统提示词"
          />
        </label>
        <details v-if="settings" class="builtin">
          <summary>内置默认提示词（DEFAULT_SYSTEM_PROMPT）</summary>
          <pre>{{ settings.default_system_prompt }}</pre>
          <button type="button" @click="form.system_prompt = settings.default_system_prompt">
            使用内置提示词
          </button>
        </details>
        <div class="fields">
          <label class="field"
            >模型名<input
              v-model="form.model"
              type="text"
              placeholder="留空使用插件的默认模型"
            /><small class="muted">仅覆盖每次聊天请求的模型名。</small></label
          >
          <label class="field"
            >保留对话轮数<input
              v-model.number="form.history_turns"
              type="number"
              min="0"
              step="1"
              required
            /><small class="muted">0 表示不保留上下文。</small></label
          >
          <label class="field"
            >空闲重置（分钟）<input
              v-model.number="form.idle_reset_minutes"
              type="number"
              min="1"
              step="1"
              required
          /></label>
          <label class="field"
            >请求超时（秒）<input
              v-model.number="form.request_timeout"
              type="number"
              min="0.001"
              step="any"
              required
          /></label>
          <label class="field"
            >最大输出令牌<input
              v-model="maxTokens"
              type="number"
              min="1"
              step="1"
              placeholder="不限制"
            /><small class="muted">留空时由模型服务决定。</small></label
          >
          <label class="field"
            >每条回复最大字数<input
              v-model.number="form.max_reply_chars"
              type="number"
              min="1"
              step="1"
              required
          /></label>
        </div>
        <div class="actions save-actions">
          <button class="primary" type="submit">{{ busy ? '保存中…' : '保存设置' }}</button
          ><button type="button" @click="save(true)">恢复默认</button>
        </div>
      </fieldset>
    </form>
  </div>
</template>

<style scoped>
fieldset {
  border: 0;
  padding: 0;
  margin: 0;
  min-width: 0;
}
.builtin {
  margin: 1rem 0;
}
.save-actions {
  justify-content: flex-start;
  margin-top: 1.5rem;
}
</style>
