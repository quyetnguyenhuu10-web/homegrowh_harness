# BLOCK COMPACTION

## 1. Public API

`@hh/compaction` là public boundary duy nhất.

```ts
import * as compaction from "@hh/compaction";

const result = await compaction.compact(
  providerName,
  model,
  history,
  tokenCount,
  { signal }, // tùy chọn: hủy request compaction theo vòng đời Session
);
```

Một API duy nhất:

```ts
compaction.compact(providerName, model, history, tokenCount, options?)
```

## 2. Tham số

```ts
providerName: "openai" | "deepseek" | "ollama" | "custom"
model: string
history: OpenAICompatibleMessage[]
tokenCount: number
options?: { signal?: AbortSignal }
```

`tokenCount` là số token hiện tại. Compaction tự quyết định có cần compact hay không.

Không truyền:

```text
contextLimitTokens
threshold
DB row
conversationId
requestId
repositoryPath
apiKey
baseUrl
prompt compact
```

## 3. Kết quả

```ts
interface CompactionResult {
  compacted: boolean
  history: OpenAICompatibleMessage[]
}
```

```text
compacted = false
    → trả history hiện tại

compacted = true
    → Compaction tự gọi @hh/provider
    → compact history
    → trả history mới
```

## 4. Boundary

Compaction tự sở hữu:

```text
threshold
prompt
format history
quyết định compact
gọi Provider
gom stream
tạo history mới
```

> Consumer chỉ biết `compaction.compact(...)`.
