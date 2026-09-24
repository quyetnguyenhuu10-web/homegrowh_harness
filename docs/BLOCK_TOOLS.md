# BLOCK TOOLS

## 1. Public API

`@hh/tools` là public boundary duy nhất.

Tương đương C++:

```cpp
#include <fsystem>
fsystem::read(...);
```

TypeScript:

```ts
import * as tools from "@hh/tools";

await tools.read(toolCall, context);
```

Không import vào các file nội bộ như `registry`, `_shared`, `read_file`...

## 2. Cú pháp gọi

Tên public API giữ đúng tên tool hiện tại:

```ts
import * as tools from "@hh/tools";

await tools.read(toolCall, context);
await tools.write(toolCall, context);
await tools.edit_file(toolCall, context);
await tools.glob(toolCall, context);
await tools.grep(toolCall, context);
await tools.webfetch(toolCall, context);
await tools.todowrite(toolCall, context);
```

Mỗi tool chỉ có **một hàm public để thực thi**. Mọi helper, registry và implementation phía sau là private.

Root `@hh/tools` chỉ export 7 hàm public trên.

Schema của tool nằm trên chính hàm:

```ts
tools.read.definition
```

Rule:

> C++: `fsystem::read()`  
> TypeScript: `tools.read()`

> Consumer chỉ biết `@hh/tools`; không biết implementation nằm ở đâu.

Layout:

```text
tools/
├─ src/
├─ tests/
└─ filesystems/   # native C++, để ngoài src
```
