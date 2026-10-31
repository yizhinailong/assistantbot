<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getFeature, saveFeature, type FeatureSection, type FeatureRecord } from '@/api/features'
import {
  getBuiltins,
  getLuaTools,
  saveLuaTool,
  deleteLuaTool,
  testLuaTool,
  type ToolConfig,
  type ToolDefinition,
  type LuaTool,
} from '@/api/tooling'
const defaultSchema = {
  type: 'object',
  properties: { text: { type: 'string' } },
  required: ['text'],
  additionalProperties: false,
}
const config = ref<FeatureSection<ToolConfig>>(),
  catalog = ref<ToolDefinition[]>([]),
  records = ref<FeatureRecord<LuaTool>[]>([])
const busy = ref(false),
  error = ref(''),
  notice = ref(''),
  name = ref(''),
  revision = ref('0'),
  description = ref(''),
  enabled = ref(true),
  script = ref('function run(args, ctx)\n  return args\nend'),
  schema = ref(JSON.stringify(defaultSchema, null, 2)),
  args = ref('{"text":"你好"}'),
  result = ref('')
function report(cause: unknown): void {
  error.value = cause instanceof Error ? cause.message : String(cause)
}
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    const [settings, builtins, custom] = await Promise.all([
      getFeature<ToolConfig>('tools'),
      getBuiltins(),
      getLuaTools(),
    ])
    for (const tool of builtins.tools) {
      settings.values.builtin[tool.name] ??= { enabled: true, description: '' }
    }
    config.value = settings
    catalog.value = builtins.tools
    records.value = custom.tools
  } catch (cause) {
    report(cause)
  } finally {
    busy.value = false
  }
}
async function saveSettings(): Promise<void> {
  if (!config.value) return
  busy.value = true
  error.value = ''
  try {
    config.value = await saveFeature('tools', config.value.values, config.value.revision)
    notice.value = '工具配置已保存。'
  } catch (cause) {
    report(cause)
  } finally {
    busy.value = false
  }
}
function edit(record?: FeatureRecord<LuaTool>): void {
  name.value = record?.key ?? ''
  revision.value = record?.revision ?? '0'
  description.value = record?.value.description ?? ''
  enabled.value = record?.value.enabled ?? true
  script.value = record?.value.script ?? 'function run(args, ctx)\n  return args\nend'
  schema.value = JSON.stringify(record?.value.parameters ?? defaultSchema, null, 2)
  if (!record) args.value = '{"text":"你好"}'
  result.value = ''
}
function parseObject(text: string): Record<string, unknown> {
  const value: unknown = JSON.parse(text)
  if (typeof value !== 'object' || value === null || Array.isArray(value))
    throw new Error('JSON 必须是对象。')
  return value as Record<string, unknown>
}
function toolDocument(): LuaTool {
  return {
    enabled: enabled.value,
    description: description.value,
    script: script.value,
    parameters: parseObject(schema.value),
  }
}
async function save(): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    const saved = await saveLuaTool(name.value, toolDocument(), revision.value)
    revision.value = saved.revision
    records.value = (await getLuaTools()).tools
    notice.value = 'Lua 工具已保存。'
  } catch (cause) {
    report(cause)
  } finally {
    busy.value = false
  }
}
async function remove(record: FeatureRecord<LuaTool>): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    await deleteLuaTool(record.key, record.revision)
    records.value = (await getLuaTools()).tools
    if (record.key === name.value) edit()
  } catch (cause) {
    report(cause)
  } finally {
    busy.value = false
  }
}
async function test(): Promise<void> {
  busy.value = true
  error.value = ''
  result.value = ''
  try {
    result.value = JSON.stringify(
      await testLuaTool(name.value, toolDocument(), parseObject(args.value)),
      null,
      2,
    )
  } catch (cause) {
    report(cause)
  } finally {
    busy.value = false
  }
}
function exportTool(): void {
  try {
    const text = JSON.stringify({ name: name.value, value: toolDocument() }, null, 2)
    const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }))
    const link = document.createElement('a')
    link.href = url
    link.download = `${name.value || 'tool'}.json`
    link.click()
    URL.revokeObjectURL(url)
  } catch (cause) {
    report(cause)
  }
}
async function importTool(event: Event): Promise<void> {
  error.value = ''
  try {
    const file = (event.target as HTMLInputElement).files?.[0]
    if (!file) return
    if (file.size > 1024 * 1024) throw new Error('工具文件不能超过 1 MiB。')
    const imported = parseObject(await file.text())
    const value = imported.value as LuaTool
    if (
      typeof imported.name !== 'string' ||
      !value ||
      typeof value.script !== 'string' ||
      typeof value.description !== 'string' ||
      typeof value.parameters !== 'object'
    )
      throw new Error('工具文件格式不正确。')
    edit({ key: imported.name, value, revision: '0' })
  } catch (cause) {
    report(cause)
  } finally {
    ;(event.target as HTMLInputElement).value = ''
  }
}
onMounted(() => void load())
</script>
<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>工具配置</h1>
        <p class="muted">启用工具后，模型可在一轮回复中查询信息或执行动作。</p>
      </div>
      <button :disabled="busy" @click="load">重新读取</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form v-if="config" class="panel" @submit.prevent="saveSettings">
      <h2>内置工具</h2>
      <fieldset :disabled="busy">
        <label
          ><input v-model="config.values.enabled" type="checkbox" /> 启用工具调用（含 Lua 和
          MCP）</label
        >
        <div v-for="tool in catalog" :key="tool.name" class="tool">
          <label
            ><input
              v-if="config.values.builtin[tool.name]"
              v-model="config.values.builtin[tool.name]!.enabled"
              type="checkbox"
            /><strong>{{ tool.name }}</strong> {{ tool.group_only ? '（仅群聊）' : '' }}</label
          >
          <p class="muted">{{ tool.description }}</p>
          <label class="field"
            >自定义描述（留空使用默认）<textarea
              v-if="config.values.builtin[tool.name]"
              v-model="config.values.builtin[tool.name]!.description"
              rows="2"
              maxlength="4096"
            />
          </label>
        </div>
        <button class="primary">保存内置工具配置</button>
      </fieldset>
    </form>
    <section class="panel">
      <div class="actions">
        <h2>Lua 自定义工具</h2>
        <button :disabled="busy" @click="edit()">新建</button
        ><label class="import"
          >导入 JSON<input
            type="file"
            accept="application/json,.json"
            :disabled="busy"
            @change="importTool"
        /></label>
      </div>
      <div v-for="record in records" :key="record.key" class="actions">
        <button :disabled="busy" @click="edit(record)">{{ record.key }}</button
        ><span>{{ record.value.enabled ? '已启用' : '已停用' }}</span
        ><button :disabled="busy" @click="remove(record)">删除</button>
      </div>
    </section>
    <form class="panel" @submit.prevent="save">
      <fieldset :disabled="busy">
        <label class="field"
          >工具名称<input
            v-model="name"
            :disabled="revision !== '0'"
            pattern="[A-Za-z][A-Za-z0-9_]{0,47}"
            maxlength="48"
            required /></label
        ><label><input v-model="enabled" type="checkbox" />启用此工具</label
        ><label class="field"
          >描述<textarea v-model="description" rows="2" maxlength="4096" /></label
        ><label class="field"
          >参数 JSON Schema<textarea v-model="schema" class="code" rows="8" required /></label
        ><label class="field"
          >Lua 脚本<textarea v-model="script" class="code" rows="12" required />
        </label>
        <div class="actions">
          <button class="primary">保存 Lua 工具</button
          ><button type="button" @click="exportTool">导出 JSON</button>
        </div>
        <h3>测试</h3>
        <p class="muted">测试使用模拟 HTTP、消息和任务，不会发送真实请求。</p>
        <label class="field">参数 JSON<textarea v-model="args" class="code" rows="3" /></label
        ><button type="button" @click="test">运行测试</button>
        <pre v-if="result">{{ result }}</pre>
      </fieldset>
    </form>
  </div>
</template>
<style scoped>
input[type='checkbox'] {
  width: auto;
}
.tool {
  border-top: 1px solid var(--border);
  margin: 1rem 0;
  padding-top: 1rem;
}
.code,
pre {
  font-family: monospace;
}
pre {
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  max-height: 30rem;
  overflow: auto;
}
.import input {
  max-width: 16rem;
}
</style>
