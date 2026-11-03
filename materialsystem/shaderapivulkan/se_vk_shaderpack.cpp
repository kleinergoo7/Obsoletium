// Copyright (c) 2026 The Source Authors.  All rights reserved.
//
// se_vk_shaderpack.cpp -- Phase 2 native Vulkan backend, step S2.6.
//
// Original code. SHA1 below is a minimal public-domain-style implementation
// written for this file (manifest keys); not copied from any project.

#include "se_vk_shaderpack.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "tier0/dbg.h"

namespace se::vulkan {
namespace {

// --- minimal SHA1 (manifest keys only) ---
struct Sha1 {
  uint32_t h[5];
  uint64_t len;
  uint8_t buf[64];
  size_t used;
  static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
  Sha1() { reset(); }
  void reset() {
    h[0] = 0x67452301;
    h[1] = 0xEFCDAB89;
    h[2] = 0x98BADCFE;
    h[3] = 0x10325476;
    h[4] = 0xC3D2E1F0;
    len = 0;
    used = 0;
  }
  void block() {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(buf[4 * i]) << 24) | (uint32_t(buf[4 * i + 1]) << 16) |
             (uint32_t(buf[4 * i + 2]) << 8) | uint32_t(buf[4 * i + 3]);
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  void update(const void *data, size_t n) {
    const uint8_t *p = static_cast<const uint8_t *>(data);
    len += n;
    while (n > 0) {
      size_t take = 64 - used < n ? 64 - used : n;
      memcpy(buf + used, p, take);
      used += take;
      p += take;
      n -= take;
      if (used == 64) {
        block();
        used = 0;
      }
    }
  }
  void hex(char out41[41]) {
    uint64_t bits = len * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (used != 56) {
      update(&zero, 1);
    }
    uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) {
      lenBytes[i] = uint8_t(bits >> (56 - 8 * i));
    }
    // feed length without re-padding
    for (int i = 0; i < 8; ++i) {
      buf[used++] = lenBytes[i];
    }
    len += 8;  // account manually; block below
    block();
    used = 0;
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 5; ++i) {
      out41[8 * i + 0] = hexd[(h[i] >> 28) & 0xf];
      out41[8 * i + 1] = hexd[(h[i] >> 24) & 0xf];
      out41[8 * i + 2] = hexd[(h[i] >> 20) & 0xf];
      out41[8 * i + 3] = hexd[(h[i] >> 16) & 0xf];
      out41[8 * i + 4] = hexd[(h[i] >> 12) & 0xf];
      out41[8 * i + 5] = hexd[(h[i] >> 8) & 0xf];
      out41[8 * i + 6] = hexd[(h[i] >> 4) & 0xf];
      out41[8 * i + 7] = hexd[(h[i]) & 0xf];
    }
    out41[40] = '\0';
  }
};

// --- minimal JSON reader for static_index.json ---
// Format: {"name": [{"key": "<40hex>", "model": "vs_2_0"} | null, ...], ...}
// Only "key" strings are extracted, in order, per name. Returns with
// partial data kept on truncation.
struct JsonCursor {
  const char *p;
  const char *end;
  void skipWs() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      ++p;
    }
  }
  bool lit(char c) {
    skipWs();
    if (p < end && *p == c) {
      ++p;
      return true;
    }
    return false;
  }
  // Reads "str" into out (no escape handling beyond backslash skip; shader
  // names/hashes never contain escapes).
  bool str(char *out, size_t cap) {
    skipWs();
    if (p >= end || *p != '"') {
      return false;
    }
    ++p;
    size_t n = 0;
    while (p < end && *p != '"') {
      char c = *p++;
      if (c == '\\' && p < end) {
        c = *p++;
      }
      if (n + 1 < cap) {
        out[n++] = c;
      }
    }
    if (p >= end) {
      return false;
    }
    ++p;  // closing quote
    out[n] = '\0';
    return true;
  }
};

void LoadStaticIndex(SeVkShaderPack *pack, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    return;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > 16 * 1024 * 1024) {
    fclose(f);
    return;
  }
  char *text = new char[size];
  size_t got = fread(text, 1, size, f);
  fclose(f);
  JsonCursor c{text, text + got};
  if (!c.lit('{')) {
    delete[] text;
    return;
  }
  char name[128];
  char dummy[32];
  char kbuf[64];
  while (pack->nStatics < SeVkShaderPack::kMaxStatics) {
    if (!c.str(name, sizeof(name))) {
      break;
    }
    if (!c.lit(':') || !c.lit('[')) {
      break;
    }
    // One combo per entry: [flatIndex, "sha1", "model"].
    int cap = 64;
    int n = 0;
    SeVkShaderPack::StaticCombo *combos = new SeVkShaderPack::StaticCombo[cap];
    bool first = true;
    while (c.p < c.end) {
      c.skipWs();
      if (c.p < c.end && *c.p == ']') {
        ++c.p;
        break;
      }
      if (!first && !c.lit(',')) {
        break;
      }
      first = false;
      c.skipWs();
      if (!c.lit('[')) {
        break;
      }
      int flat = 0;
      char kbuf[64] = {};
      char mbuf[32] = {};
      // flat index
      c.skipWs();
      bool neg = false;
      if (c.p < c.end && *c.p == '-') {
        neg = true;
        ++c.p;
      }
      while (c.p < c.end && *c.p >= '0' && *c.p <= '9') {
        flat = flat * 10 + (*c.p - '0');
        ++c.p;
      }
      if (neg) {
        flat = -flat;
      }
      if (!c.lit(',') || !c.str(kbuf, sizeof(kbuf))) {
        break;
      }
      if (c.lit(',')) {
        c.str(mbuf, sizeof(mbuf));
      }
      c.skipWs();
      if (c.p < c.end && *c.p == ']') {
        ++c.p;
      }
      if (n >= cap) {
        cap *= 2;
        SeVkShaderPack::StaticCombo *grown =
            new SeVkShaderPack::StaticCombo[cap];
        memcpy(grown, combos, n * sizeof(SeVkShaderPack::StaticCombo));
        delete[] combos;
        combos = grown;
      }
      combos[n].flat = flat;
      snprintf(combos[n].hash, 41, "%s", kbuf);
      ++n;
    }
    auto &e = pack->statics[pack->nStatics++];
    snprintf(e.name, sizeof(e.name), "%s", name);
    e.combos = combos;
    e.nCombos = n;
    c.skipWs();
    if (c.p < c.end && *c.p == ',') {
      ++c.p;
      continue;
    }
    break;
  }
  delete[] text;
  Msg("[vulkan] static shader index: %d files\n", pack->nStatics);
}

}  // namespace

bool SeVkShaderPack_Load(VkDevice device, const char *dir, SeVkShaderPack *pack) {
  (void)device;
  snprintf(pack->dir, sizeof(pack->dir), "%s", dir ? dir : "");
  pack->count = 0;
  pack->nShaders = 0;
  for (int i = 0; i < pack->nStatics; ++i) {
    delete[] pack->statics[i].combos;
    pack->statics[i].combos = nullptr;
    pack->statics[i].nCombos = 0;
  }
  pack->nStatics = 0;
  char manifest[576];
  snprintf(manifest, sizeof(manifest), "%s/manifest.json", pack->dir);
  FILE *f = fopen(manifest, "rb");
  if (!f) {
    Warning("[vulkan] no shader pack manifest at %s: the renderer cannot draw "
            "engine materials. Build the pack with tools/vcs_pack.py.\n",
            manifest);
    return false;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  // Full-content pack is ~2-3 MB (21k entries); cap well above that.
  if (size <= 0 || size > 16 * 1024 * 1024) {
    fclose(f);
    Warning("[vulkan] shader pack manifest %s has implausible size %ld\n",
            manifest, size);
    return false;
  }
  char *text = new char[size + 1];
  size_t got = fread(text, 1, size, f);
  fclose(f);
  text[got] = '\0';
  // Minimal JSON scan: "hash": {"file": "x", ...}. Keys are 40-hex, values
  // on following lines; parse liberally.
  const char *p = text;
  while (pack->count < SeVkShaderPack::kMaxShaders && (p = strchr(p, '"')) != nullptr) {
    const char *q = strchr(p + 1, '"');
    if (!q || q - p != 41) {
      p = q ? q + 1 : p + 1;
      continue;
    }
    const char *fr = strstr(q, "\"file\"");
    if (!fr) {
      break;
    }
    const char *c0 = strchr(fr + 6, '"');
    const char *c1 = c0 ? strchr(c0 + 1, '"') : nullptr;
    if (!c0 || !c1 || c1 - c0 >= 128) {
      p = q + 1;
      continue;
    }
    auto &e = pack->entries[pack->count++];
    memcpy(e.hash, p + 1, 40);
    e.hash[40] = '\0';
    memcpy(e.file, c0 + 1, c1 - c0 - 1);
    e.file[c1 - c0 - 1] = '\0';
    p = c1 + 1;
  }
  delete[] text;
  if (pack->count == 0) {
    Warning("[vulkan] shader pack manifest %s yielded no entries\n", manifest);
    return false;
  }
  Msg("[vulkan] shader pack: %d entries from %s\n", pack->count, manifest);
  char staticIndex[576];
  snprintf(staticIndex, sizeof(staticIndex), "%s/static_index.json",
           pack->dir);
  LoadStaticIndex(pack, staticIndex);
  return true;
}

SeVkShader *SeVkShaderPack_Create(VkDevice device, SeVkShaderPack *pack,
                                  const void *tokens, size_t size,
                                  bool isVertex, const char *debugName) {
  if (pack->count == 0 || !tokens || size < 8) {
    return nullptr;
  }
  Sha1 sha;
  sha.update(tokens, size);
  char hex[41];
  sha.hex(hex);
  const char *file = nullptr;
  for (int i = 0; i < pack->count; ++i) {
    if (memcmp(pack->entries[i].hash, hex, 40) == 0) {
      file = pack->entries[i].file;
      break;
    }
  }
  if (!file) {
    DevMsg("[vulkan] no pack entry for %s shader sha1 %s (%zu bytes)\n",
           isVertex ? "vertex" : "pixel", hex, size);
    return nullptr;
  }
  return SeVkShaderPack_CreateFromFile(device, pack, file, hex, isVertex,
                                       debugName);
}

SeVkShader *SeVkShaderPack_CreateStatic(VkDevice device, SeVkShaderPack *pack,
                                        const char *name, int staticIndex,
                                        bool isVertex) {
  if (!name || staticIndex < 0) {
    return nullptr;
  }
  // Strip directory + extension: stdshader passes bare names, but be liberal.
  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  const char *bs = strrchr(name, '\\');
  if (bs && bs + 1 > base) {
    base = bs + 1;
  }
  char bare[96];
  snprintf(bare, sizeof(bare), "%s", base);
  char *dot = strrchr(bare, '.');
  if (dot && (strcmp(dot, ".vcs") == 0 || strcmp(dot, ".fxc") == 0)) {
    *dot = '\0';
  }
  // Mirror the DX9 vsh/psh-then-fxc search order.
  const char *dirs[2] = {isVertex ? "shaders/vsh" : "shaders/psh",
                         "shaders/fxc"};
  for (int d = 0; d < 2; ++d) {
    char key[160];
    snprintf(key, sizeof(key), "%s/%s", dirs[d], bare);
    // The statics are sorted by name at load time (see
    // SeVkShaderPack_Load), so a binary search finds the entry.
    int lo = 0, hi = pack->nStatics - 1, found = -1;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      int cmp = strcmp(pack->statics[mid].name, key);
      if (cmp == 0) {
        found = mid;
        break;
      }
      if (cmp < 0) {
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    if (found < 0) {
      continue;
    }
    const auto &e = pack->statics[found];
      // The engine addresses a combo by one flat index; the combos are
      // sorted by it (the static combo ids are sparse).
      int lo2 = 0, hi2 = e.nCombos - 1, found2 = -1;
      while (lo2 <= hi2) {
        int mid = (lo2 + hi2) / 2;
        if (e.combos[mid].flat == staticIndex) {
          found2 = mid;
          break;
        }
        if (e.combos[mid].flat < staticIndex) {
          lo2 = mid + 1;
        } else {
          hi2 = mid - 1;
        }
      }
      if (found2 < 0) {
        // Once per shader: this is asked for every material that uses the
        // shader, every frame, and the console keeps every line it is given
        // (the console's text is re-laid-out in full each frame).
        static std::set<std::string> reported;
        if (reported.insert(key).second) {
          DevMsg("[vulkan] static combo %s [%d] absent (n=%d)\n", key,
                 staticIndex, e.nCombos);
        }
        return nullptr;
      }
      const char *hex = e.combos[found2].hash;
      // The manifest is written with sorted keys, so pack->entries is sorted
      // by hash; binary search instead of the linear scan (42k entries).
      int mlo = 0, mhi = pack->count - 1, idx = -1;
      while (mlo <= mhi) {
        int mid = (mlo + mhi) / 2;
        int cmp = memcmp(pack->entries[mid].hash, hex, 40);
        if (cmp == 0) {
          idx = mid;
          break;
        }
        if (cmp < 0) {
          mlo = mid + 1;
        } else {
          mhi = mid - 1;
        }
      }
      const char *file = (idx >= 0) ? pack->entries[idx].file : nullptr;
      if (!file) {
        DevMsg("[vulkan] static combo %s [%d] not in manifest\n", key,
               staticIndex);
        return nullptr;
      }
      char debug[96];
      snprintf(debug, sizeof(debug), "%s[%d]", bare, staticIndex);
      return SeVkShaderPack_CreateFromFile(device, pack, file, hex, isVertex,
                                           debug);
    }
  DevMsg("[vulkan] no static entry for shader '%s'\n", bare);
  return nullptr;
}

namespace {

// Bitmask of the vertex input locations the SPIR-V reads. Vulkan warns when a
// pipeline declares an attribute the shader does not consume, so the input
// state is built from this instead of the full layout. 0 means "could not
// reflect", and the caller then keeps the full layout.
uint32_t SpirvInputMask(const uint32_t *words, size_t count) {
  if (count < 6) {
    return 0;
  }
  const uint32_t limit = 1024;  // far above the 22 locations this layout uses
  uint32_t locOf[limit];
  bool hasLoc[limit];
  memset(hasLoc, 0, sizeof(hasLoc));
  uint32_t mask = 0;
  for (size_t i = 5; i < count;) {
    const uint32_t instr = words[i];
    const uint32_t wc = instr >> 16;
    const uint32_t op = instr & 0xffffu;
    if (wc == 0 || i + wc > count) {
      break;
    }
    if (op == 71 && wc >= 4) {  // OpDecorate
      const uint32_t id = words[i + 1];
      if (words[i + 2] == 30 && id < limit) {  // Decoration Location
        locOf[id] = words[i + 3];
        hasLoc[id] = true;
      }
    } else if (op == 59 && wc >= 4) {  // OpVariable
      const uint32_t id = words[i + 2];
      if (words[i + 3] == 1 && id < limit && hasLoc[id] && locOf[id] < 32) {
        mask |= 1u << locOf[id];
      }
    }
    i += wc;
  }
  return mask;
}

}  // namespace

SeVkShader *SeVkShaderPack_CreateFromFile(VkDevice device,
                                           SeVkShaderPack *pack,
                                           const char *file, const char *hex,
                                           bool isVertex,
                                           const char *debugName) {
  // Dedupe: snapshots resolve every frame; reuse the existing module.
  for (int i = 0; i < pack->nShaders; ++i) {
    SeVkShader *s = pack->shaders[i];
    if (s->isVertex == isVertex && memcmp(s->hash, hex, 40) == 0) {
      ++s->refs;
      return s;
    }
  }
  char path[640];
  snprintf(path, sizeof(path), "%s/%s", pack->dir, file);
  FILE *f = fopen(path, "rb");
  if (!f) {
    Warning("[vulkan] pack file missing: %s\n", path);
    return nullptr;
  }
  fseek(f, 0, SEEK_END);
  long spvSize = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (spvSize <= 0 || (spvSize % 4) != 0) {
    fclose(f);
    return nullptr;
  }
  auto *words = new uint32_t[spvSize / 4];
  size_t got = fread(words, 1, spvSize, f);
  fclose(f);
  if (got != (size_t)spvSize) {
    delete[] words;
    return nullptr;
  }
  VkShaderModuleCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sci.codeSize = spvSize;
  sci.pCode = words;
  VkShaderModule module = VK_NULL_HANDLE;
  VkResult result = vkCreateShaderModule(device, &sci, nullptr, &module);
  const uint32_t inputMask =
      isVertex ? SpirvInputMask(words, got / sizeof(uint32_t)) : 0;
  delete[] words;
  if (result != VK_SUCCESS) {
    Warning("[vulkan] vkCreateShaderModule(%s) failed\n", file);
    return nullptr;
  }
  if (pack->nShaders >= SeVkShaderPack::kMaxShaders) {
    vkDestroyShaderModule(device, module, nullptr);
    return nullptr;
  }
  auto *rec = new SeVkShader();
  rec->module = module;
  rec->isVertex = isVertex;
  rec->inputMask = inputMask;
  snprintf(rec->name, sizeof(rec->name), "%s", debugName ? debugName : "?");
  snprintf(rec->hash, sizeof(rec->hash), "%s", hex ? hex : "");
  pack->shaders[pack->nShaders++] = rec;
  Msg("[vulkan] %s shader %s <- %s (sha1 %s)\n",
      isVertex ? "vertex" : "pixel", rec->name, file, hex);
  return rec;
}

void SeVkShaderPack_Destroy(VkDevice device, SeVkShaderPack *pack,
                            SeVkShader *shader) {
  if (!shader) {
    return;
  }
  if (--shader->refs > 0) {
    return;
  }
  if (shader->module != VK_NULL_HANDLE && device != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, shader->module, nullptr);
  }
  for (int i = 0; i < pack->nShaders; ++i) {
    if (pack->shaders[i] == shader) {
      pack->shaders[i] = pack->shaders[--pack->nShaders];
      break;
    }
  }
  delete shader;
}

void SeVkShaderPack_Unload(VkDevice device, SeVkShaderPack *pack) {
  while (pack->nShaders > 0) {
    // Force release (shared refs die with the pack).
    pack->shaders[0]->refs = 1;
    SeVkShaderPack_Destroy(device, pack, pack->shaders[0]);
  }
  for (int i = 0; i < pack->nStatics; ++i) {
    delete[] pack->statics[i].combos;
    pack->statics[i].combos = nullptr;
    pack->statics[i].nCombos = 0;
  }
  pack->nStatics = 0;
  pack->count = 0;
}

}  // namespace se::vulkan
