<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import {
  getFeature,
  saveFeature,
  getMemoryDocument,
  saveMemoryDocument,
  getMemories,
  saveMemory,
  deleteMemory,
  type FeatureSection,
  type FeatureRecord,
  type MemoryConfig,
  type PersonMemory,
  type Aliases,
  type LongMemory,
} from '@/api/features'
const settings = ref<FeatureSection<MemoryConfig>>()
const type = ref('private'),
  qq = ref(''),
  personQq = ref('')
const session = computed(() => (type.value === 'group' ? '-' : '') + qq.value)
const aliases = ref<FeatureRecord<Aliases | null>>()
const person = ref<FeatureRecord<PersonMemory | null>>()
const profile = ref(''),
  relationship = ref(''),
  recent = ref(''),
  affinity = ref(0)
const entries = ref<LongMemory[]>([]),
  content = ref('')
const busy = ref(false),
  error = ref(''),
  notice = ref('')
function lines(text: string): string[] {
  return text
    .split('\n')
    .map((line) => line.trim())
    .filter(Boolean)
}
async function action(operation: () => Promise<void>): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    await operation()
    notice.value = '操作完成。'
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function loadSession(): Promise<void> {
  await action(async () => {
    const [a, m] = await Promise.all([
      getMemoryDocument<Aliases>('aliases', session.value),
      getMemories(session.value),
    ])
    aliases.value = a
    if (!aliases.value.value) aliases.value.value = { aliases: [] }
    entries.value = m.memories
  })
}
async function loadPerson(): Promise<void> {
  await action(async () => {
    person.value = await getMemoryDocument<PersonMemory>('people', personQq.value)
    const value = person.value.value
    profile.value = value?.profile.join('\n') ?? ''
    relationship.value = value?.relationship.join('\n') ?? ''
    recent.value = value?.recent_interactions.join('\n') ?? ''
    affinity.value = value?.affinity ?? 0
  })
}
async function savePerson(): Promise<void> {
  await action(async () => {
    if (!person.value) return
    person.value = await saveMemoryDocument(
      'people',
      person.value.key,
      {
        profile: lines(profile.value),
        relationship: lines(relationship.value),
        recent_interactions: lines(recent.value),
        affinity: affinity.value,
      },
      person.value.revision,
    )
  })
}
async function saveAliases(): Promise<void> {
  await action(async () => {
    if (!aliases.value?.value) return
    aliases.value = await saveMemoryDocument(
      'aliases',
      aliases.value.key,
      aliases.value.value,
      aliases.value.revision,
    )
  })
}
async function changeMemory(entry?: LongMemory): Promise<void> {
  await action(async () => {
    await saveMemory(aliases.value!.key, entry?.content ?? content.value, entry)
    entries.value = (await getMemories(aliases.value!.key)).memories
    content.value = ''
  })
}
async function remove(entry: LongMemory): Promise<void> {
  await action(async () => {
    await deleteMemory(entry)
    entries.value = entries.value.filter((item) => item.id !== entry.id)
  })
}
onMounted(
  () =>
    void action(async () => {
      settings.value = await getFeature<MemoryConfig>('memory')
    }),
)
</script>
<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>人物与记忆</h1>
        <p class="muted">外号和长期记忆按会话保存，人物档案与好感度按 QQ 共享。</p>
      </div>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <form
      v-if="settings"
      class="panel"
      @submit.prevent="
        action(async () => {
          settings = await saveFeature('memory', settings!.values, settings!.revision)
        })
      "
    >
      <h2>记忆设置</h2>
      <fieldset :disabled="busy">
        <div class="switches">
          <label
            ><input v-model="settings.values.enabled" type="checkbox" /> 注入与自动整理记忆</label
          ><label
            ><input v-model="settings.values.affinity_enabled" type="checkbox" /> 使用好感度</label
          >
        </div>
        <div class="fields">
          <label class="field"
            >召回条数<input
              v-model.number="settings.values.max_memories"
              type="number"
              min="1"
              max="20"
              required /></label
          ><label class="field"
            >整理间隔（回复轮数）<input
              v-model.number="settings.values.maintenance_turns"
              type="number"
              min="1"
              max="100"
              required /></label
          ><label class="field"
            >召回相似度阈值<input
              v-model.number="settings.values.similarity_threshold"
              type="number"
              min="0"
              max="1"
              step="0.05"
              required
          /></label>
        </div>
        <button>保存记忆设置</button>
        <p class="muted">
          自动整理需启用记忆模型，向量检索需启用 embedding 模型；未启用向量模型时使用文字匹配。
        </p>
      </fieldset>
    </form>
    <section class="panel">
      <h2>会话记忆</h2>
      <form class="actions" @submit.prevent="loadSession">
        <select v-model="type" aria-label="会话类型">
          <option value="private">好友</option>
          <option value="group">群</option></select
        ><input
          v-model="qq"
          required
          pattern="[0-9]+"
          placeholder="QQ 号或群号"
          aria-label="会话 QQ"
        /><button :disabled="busy">读取会话</button>
      </form>
      <template v-if="aliases?.value"
        ><form @submit.prevent="saveAliases">
          <h3>外号</h3>
          <div v-for="(alias, index) in aliases.value.aliases" :key="index" class="actions">
            <input
              v-model="alias.qq"
              pattern="[0-9]+"
              placeholder="人物 QQ"
              required
              aria-label="人物 QQ"
            /><input
              :value="alias.names.join('、')"
              placeholder="外号，用顿号分隔"
              aria-label="外号"
              @input="
                alias.names = ($event.target as HTMLInputElement).value.split('、').filter(Boolean)
              "
            /><button
              type="button"
              :disabled="busy"
              @click="aliases!.value!.aliases.splice(index, 1)"
            >
              移除
            </button>
          </div>
          <div class="actions">
            <button
              type="button"
              :disabled="busy"
              @click="aliases.value!.aliases.push({ qq: '', names: [] })"
            >
              添加外号</button
            ><button :disabled="busy">保存外号</button>
          </div>
        </form>
        <form @submit.prevent="changeMemory()">
          <h3>长期记忆</h3>
          <label class="field"
            >新增记忆<textarea v-model="content" rows="3" maxlength="8192" required /></label
          ><button :disabled="busy">添加记忆</button>
        </form>
        <form
          v-for="entry in entries"
          :key="entry.id"
          class="memory"
          @submit.prevent="changeMemory(entry)"
        >
          <label class="field"
            >#{{ entry.id }} · {{ new Date(entry.created_at * 1000).toLocaleString() }} ·
            {{ entry.dimensions }} 维<textarea
              v-model="entry.content"
              rows="3"
              maxlength="8192"
              required
            />
          </label>
          <div class="actions">
            <button :disabled="busy">保存修改</button
            ><button type="button" :disabled="busy" @click="remove(entry)">删除</button>
          </div>
        </form>
        <p v-if="!entries.length" class="muted">此会话还没有长期记忆。</p></template
      >
    </section>
    <section class="panel">
      <h2>人物档案</h2>
      <form class="actions" @submit.prevent="loadPerson">
        <input
          v-model="personQq"
          required
          pattern="[0-9]+"
          placeholder="人物 QQ"
          aria-label="人物 QQ"
        /><button :disabled="busy">读取人物</button>
      </form>
      <form v-if="person" @submit.prevent="savePerson">
        <fieldset :disabled="busy">
          <div class="fields">
            <label class="field">人物信息（每行一条）<textarea v-model="profile" rows="6" /></label
            ><label class="field"
              >关系（每行一条）<textarea v-model="relationship" rows="6" /></label
            ><label class="field">近期互动（每行一条）<textarea v-model="recent" rows="6" /></label>
          </div>
          <label class="field"
            >好感度<input
              v-model.number="affinity"
              type="number"
              min="-100"
              max="100"
              required /></label
          ><button>保存人物档案</button>
        </fieldset>
      </form>
    </section>
  </div>
</template>
<style scoped>
.fields {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr));
  gap: 1rem;
}
.switches {
  display: flex;
  gap: 1rem;
  flex-wrap: wrap;
}
.switches input {
  width: auto;
}
.memory {
  margin-top: 1rem;
  padding-top: 1rem;
  border-top: 1px solid var(--color-border);
}
</style>
