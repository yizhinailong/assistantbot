<script setup lang="ts">
import { onMounted, ref } from 'vue'
import {
  getStickers,
  renameSticker,
  deleteSticker,
  addSticker,
  getCharacter,
  saveCharacter,
  deleteCharacter,
  type Sticker,
  type FeatureRecord,
  type CharacterImage,
} from '@/api/features'
const stickers = ref<Sticker[]>([])
const character = ref<FeatureRecord<CharacterImage | null>>()
const path = ref('')
const busy = ref(false)
const error = ref('')
const notice = ref('')
function imageUrl(value: string): string | undefined {
  return /^https?:\/\//.test(value) ? value : undefined
}
async function load(): Promise<void> {
  busy.value = true
  error.value = ''
  const results = await Promise.allSettled([getStickers(), getCharacter()])
  if (results[0].status === 'fulfilled') stickers.value = results[0].value.stickers
  else error.value = String(results[0].reason)
  if (results[1].status === 'fulfilled') character.value = results[1].value
  else error.value += ' ' + String(results[1].reason)
  busy.value = false
}
async function action(operation: () => Promise<unknown>, refresh = false): Promise<void> {
  busy.value = true
  error.value = ''
  notice.value = ''
  try {
    await operation()
    notice.value = '已保存。'
    if (refresh) await load()
  } catch (cause) {
    error.value = cause instanceof Error ? cause.message : String(cause)
  } finally {
    busy.value = false
  }
}
async function upload(event: Event): Promise<void> {
  const file = (event.target as HTMLInputElement).files?.[0]
  if (!file || !character.value) return
  if (file.size > 8 * 1024 * 1024) {
    error.value = '图片不能超过 8 MiB。'
    return
  }
  const reader = new FileReader()
  reader.onload = () =>
    void action(async () => {
      character.value = await saveCharacter(String(reader.result), character.value!.revision)
    })
  reader.onerror = () => {
    error.value = '无法读取图片。'
  }
  reader.readAsDataURL(file)
}
onMounted(() => void load())
</script>
<template>
  <div class="admin-page">
    <div class="page-heading">
      <div>
        <h1>表情与角色图</h1>
        <p class="muted">管理账号收藏的表情，以及图片生成使用的角色参考图。</p>
      </div>
      <button :disabled="busy" @click="load">刷新</button>
    </div>
    <p v-if="error" class="feedback error" role="alert">{{ error }}</p>
    <p v-if="notice" class="feedback success" role="status">{{ notice }}</p>
    <section class="panel">
      <h2>角色形象图</h2>
      <p class="muted">PNG、JPEG 或静态 WebP，最大 8 MiB。</p>
      <img
        v-if="character?.value"
        :src="character.value.data_url"
        alt="当前角色形象图"
        class="character"
      />
      <p v-else>尚未设置形象图。</p>
      <input
        type="file"
        accept="image/png,image/jpeg,image/webp"
        :disabled="busy || !character"
        aria-label="上传角色形象图"
        @change="upload"
      /><button
        v-if="character?.value"
        :disabled="busy"
        @click="
          action(async () => {
            character = await deleteCharacter(character!.revision)
          })
        "
      >
        删除形象图
      </button>
    </section>
    <form class="panel" @submit.prevent="action(() => addSticker(path), true)">
      <h2>添加收藏</h2>
      <label class="field"
        >NapCat 主机上的图片路径<input
          v-model="path"
          required
          maxlength="4096"
          placeholder="/path/on/napcat/sticker.png"
          :disabled="busy" /></label
      ><button :disabled="busy">添加</button>
    </form>
    <section class="panel">
      <h2>收藏表情（{{ stickers.length }}）</h2>
      <p v-if="!stickers.length" class="muted">暂无收藏，或当前 OneBot 实现不支持收藏扩展。</p>
      <div class="stickers">
        <form
          v-for="sticker in stickers"
          :key="sticker.res_id"
          class="sticker"
          @submit.prevent="action(() => renameSticker(sticker))"
        >
          <img
            v-if="imageUrl(sticker.url)"
            :src="imageUrl(sticker.url)"
            alt="收藏表情"
            loading="lazy"
            referrerpolicy="no-referrer"
          /><label class="field"
            >描述<input v-model="sticker.description" :disabled="busy" maxlength="4096"
          /></label>
          <div class="actions">
            <button :disabled="busy">保存描述</button
            ><button
              type="button"
              :disabled="busy"
              @click="action(() => deleteSticker(sticker.res_id), true)"
            >
              移出收藏
            </button>
          </div>
        </form>
      </div>
    </section>
  </div>
</template>
<style scoped>
.character {
  display: block;
  max-width: 20rem;
  max-height: 20rem;
  margin-bottom: 1rem;
}
.stickers {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(14rem, 1fr));
  gap: 1rem;
}
.sticker {
  border: 1px solid var(--color-border);
  border-radius: 6px;
  padding: 1rem;
}
.sticker img {
  width: 100%;
  height: 8rem;
  object-fit: contain;
}
</style>
