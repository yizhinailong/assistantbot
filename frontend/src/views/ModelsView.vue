<script setup lang="ts">
import { onMounted, ref } from 'vue'
import {
  getFeature,
  saveFeature,
  type FeatureSection,
  type ModelConfig,
  type ExecutionConfig,
} from '@/api/features'
const models = ref<FeatureSection<ModelConfig>>()
const execution = ref<FeatureSection<ExecutionConfig>>()
const busy = ref(false),
  error = ref(''),
  notice = ref('')
const labels: Record<string, string> = {
  router: '任务分析',
  decision: '回复判断',
  executor: '回复执行',
  executor_fallback: '执行失败备用',
  thinking: '深度思考',
  vision: '图片识别',
  image_generation: '图片生成',
  memory: '记忆整理',
  embedding: '记忆向量',
}
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    ;[models.value, execution.value] = await Promise.all([
      getFeature<ModelConfig>('models'),
      getFeature<ExecutionConfig>('execution'),
    ])
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function save(section: 'models' | 'execution', reset = false): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    if (section === 'models' && models.value)
      models.value = await saveFeature(
        section,
        reset ? models.value.defaults : models.value.values,
        models.value.revision,
      )
    if (section === 'execution' && execution.value)
      execution.value = await saveFeature(
        section,
        reset ? execution.value.defaults : execution.value.values,
        execution.value.revision,
      )
    // Sections share a revision; synchronize the untouched form without discarding edits.
    if (models.value && execution.value) {
      const revision = section === 'models' ? models.value.revision : execution.value.revision
      models.value.revision = revision
      execution.value.revision = revision
    }
    notice.value = '已保存，下一次回复起生效。'
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
        <h1>模型与执行</h1>
        <p class="muted">按职责配置模型；未启用的回复执行角色使用回复设置中的模型。</p>
      </div>
      <button :disabled="busy" @click="load">重新读取</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form v-if="models" @submit.prevent="save('models')">
      <fieldset :disabled="busy">
        <section v-for="(role, name) in models.values" :key="name" class="panel">
          <h2>{{ labels[name] ?? name }}</h2>
          <label class="toggle"><input v-model="role.enabled" type="checkbox" /> 启用</label>
          <div class="fields">
            <label class="field"
              >模型名<input
                v-model="role.model"
                maxlength="256"
                placeholder="留空使用主模型" /></label
            ><label class="field"
              >API 地址<input
                v-model="role.base_url"
                maxlength="2048"
                placeholder="留空使用主地址" /></label
            ><label class="field"
              >密钥环境变量名<input
                v-model="role.api_key_env"
                pattern="[A-Za-z_][A-Za-z0-9_]*"
                maxlength="128"
                placeholder="例如 VISION_API_KEY" /></label
            ><label class="field"
              >超时（秒）<input
                v-model.number="role.timeout"
                type="number"
                min="1"
                max="3600"
                required /></label
            ><label class="field"
              >失败重试次数<input
                v-model.number="role.max_retries"
                type="number"
                min="0"
                max="10"
                required /></label
            ><label class="field"
              >输出令牌上限（0 不覆盖请求默认值）<input
                v-model.number="role.max_tokens"
                type="number"
                min="0"
                max="1000000"
                required /></label
            ><label class="field"
              >温度<input
                v-model.number="role.temperature"
                type="number"
                min="0"
                max="2"
                step="0.05"
                required /></label
            ><label class="field"
              >推理强度<input
                v-model="role.reasoning_effort"
                maxlength="32"
                placeholder="按模型支持填写，如 low / medium / high"
            /></label>
          </div>
        </section>
        <div class="actions">
          <button class="primary">保存模型配置</button
          ><button type="button" @click="save('models', true)">恢复默认</button>
        </div>
      </fieldset>
    </form>
    <form v-if="execution" class="panel" @submit.prevent="save('execution')">
      <h2>执行限制</h2>
      <fieldset :disabled="busy">
        <div class="fields">
          <label class="field"
            >最大工具轮数<input
              v-model.number="execution.values.max_tool_rounds"
              type="number"
              min="1"
              max="32"
              required /></label
          ><label class="field"
            >工具结果最大字符数<input
              v-model.number="execution.values.max_tool_result_chars"
              type="number"
              min="256"
              max="32000"
              required /></label
          ><label class="field"
            >MCP 工具每页数量<input
              v-model.number="execution.values.mcp_page_size"
              type="number"
              min="1"
              max="100"
              required /></label
          ><label class="field"
            >MCP 每次启用工具数<input
              v-model.number="execution.values.mcp_max_tools_per_enable"
              type="number"
              min="1"
              max="32"
              required /></label
          ><label class="field"
            >每次回复 MCP 工具总数<input
              v-model.number="execution.values.mcp_max_tools_per_reply"
              type="number"
              min="1"
              max="64"
              required
          /></label>
        </div>
        <div class="actions">
          <button>保存执行配置</button
          ><button type="button" @click="save('execution', true)">恢复默认</button>
        </div>
      </fieldset>
    </form>
  </div>
</template>
<style scoped>
.toggle {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-bottom: 1rem;
}
.toggle input {
  width: auto;
}
.fields {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(15rem, 1fr));
  gap: 1rem;
}
</style>
