<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { getFeature, saveFeature, type FeatureSection } from '@/api/features'
import { probeMcp, type McpConfig } from '@/api/tooling'
const config = ref<FeatureSection<McpConfig>>()
const busy = ref(false),
  error = ref(''),
  notice = ref(''),
  result = ref('')
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  try {
    config.value = await getFeature<McpConfig>('mcp')
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function save(): Promise<void> {
  if (!config.value) return
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    config.value = await saveFeature('mcp', config.value.values, config.value.revision)
    notice.value = '已保存，后续回复使用新配置。'
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
function add(): void {
  config.value?.values.servers.push({
    name: '',
    description: '',
    url: '',
    token_env: '',
    enabled: true,
    timeout: 30,
    protocol_version: '2025-11-25',
  })
}
async function test(name: string): Promise<void> {
  busy.value = true
  error.value = ''
  result.value = ''
  try {
    result.value = JSON.stringify(await probeMcp(name), null, 2)
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
        <h1>MCP 服务</h1>
        <p class="muted">
          连接 Streamable HTTP 工具服务。模型按需发现和启用工具；保存后可测试连接。
        </p>
      </div>
      <button :disabled="busy" @click="load">重新读取</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form v-if="config" @submit.prevent="save">
      <fieldset :disabled="busy">
        <section v-for="(server, index) in config.values.servers" :key="index" class="panel">
          <div class="actions">
            <h2>{{ server.name || '新服务' }}</h2>
            <label><input v-model="server.enabled" type="checkbox" />启用</label
            ><button type="button" @click="config.values.servers.splice(index, 1)">移除</button>
          </div>
          <div class="fields">
            <label class="field"
              >名称<input
                v-model="server.name"
                pattern="[A-Za-z][A-Za-z0-9_-]{0,31}"
                required
                maxlength="32" /></label
            ><label class="field"
              >服务地址<input
                v-model="server.url"
                type="url"
                required
                maxlength="2048"
                placeholder="https://example.com/mcp" /></label
            ><label class="field"
              >令牌环境变量名<input
                v-model="server.token_env"
                maxlength="128"
                placeholder="例如 MCP_ACCESS_TOKEN" /></label
            ><label class="field"
              >超时（秒）<input
                v-model.number="server.timeout"
                type="number"
                min="1"
                max="300"
                required /></label
            ><label class="field"
              >协议版本<select v-model="server.protocol_version">
                <option>2025-11-25</option>
                <option>2026-07-28</option>
              </select></label
            >
          </div>
          <label class="field"
            >用途<textarea v-model="server.description" rows="2" maxlength="4096" /></label
          ><button type="button" :disabled="!server.name" @click="test(server.name)">
            测试已保存的服务并查看工具
          </button>
        </section>
        <div class="actions">
          <button type="button" :disabled="config.values.servers.length >= 16" @click="add">
            添加服务</button
          ><button class="primary">保存配置</button>
        </div>
      </fieldset>
    </form>
    <section v-if="result" class="panel">
      <h2>发现的工具</h2>
      <pre>{{ result }}</pre>
    </section>
  </div>
</template>
<style scoped>
.fields {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(15rem, 1fr));
  gap: 1rem;
}
input[type='checkbox'] {
  width: auto;
}
pre {
  white-space: pre-wrap;
  overflow-wrap: anywhere;
  max-height: 35rem;
  overflow: auto;
}
</style>
