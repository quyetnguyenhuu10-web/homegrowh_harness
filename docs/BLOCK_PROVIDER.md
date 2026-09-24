# BLOCK PROVIDER

## 1. Public API

`@hh/provider` là public boundary duy nhất.

```ts
import * as provider from "@hh/provider";

for await (const chunk of provider.call(providerName, model, history)) {
  // stream
}
```

API gọi model:

```ts
provider.call(providerName, model, history)
```

Trong đó:

```ts
providerName: "openai" | "deepseek" | "ollama" | "custom"
model: string
history: OpenAICompatibleMessage[]
```

## 2. Chat history

`history` luôn dùng chuẩn OpenAI-compatible Chat Completions.

```ts
[
  { role: "system", content: "..." },
  { role: "user", content: "..." },
  { role: "assistant", content: "..." },
  { role: "tool", tool_call_id: "...", content: "..." }
]
```

Giữ nguyên các field OpenAI-compatible cần thiết như:

```text
role
content
reasoning_content
tool_calls
tool_call_id
name
```

## 3. Provider

Cùng một cú pháp cho provider có sẵn:

```ts
provider.call("openai", model, history);
provider.call("deepseek", model, history);
provider.call("ollama", model, history);
```

Và custom model:

```ts
provider.call("custom", model, history);
```

Caller không truyền `apiKey`, `baseUrl` hay gọi helper để chuẩn bị provider.

## 4. Model

Provider sở hữu toàn bộ built-in model và custom model.

```ts
provider.model.list()
provider.model.getDefault()
provider.model.getSelected()
provider.model.setSelected(selection)
provider.model.getContextLimit(selection)

provider.model.addCustom(input)
provider.model.getCustom(model)
provider.model.updateCustom(input)
provider.model.deleteCustom(model)
```

`getCustom()` không trả API key.

Credential, endpoint runtime và cách route model là private. Không có public `resolveRuntime()`.

```text
built-in registry   → Provider private
custom registry     → Provider private
selected model      → Provider private storage
apiKey/baseUrl      → Provider private
model routing       → Provider private
```

> Consumer chỉ gọi public API của `@hh/provider`; không module nào khác biết model được lưu hay route như thế nào.
