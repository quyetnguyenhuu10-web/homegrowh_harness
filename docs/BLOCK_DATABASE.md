# BLOCK DATABASE

## 1. Public API

`@hh/database` là public boundary duy nhất.

```ts
import * as database from "@hh/database";
```

Runtime chỉ có:

```text
database.repository
database.conversation
database.contextUsage
database.algorithm
```

## 2. Repository

```ts
database.repository.list()
database.repository.add(repositoryPath)
```

## 3. Conversation

```ts
database.conversation.list(scope)
database.conversation.create(scope)
database.conversation.delete(ref)
database.conversation.getActive()
database.conversation.setActive(ref)
database.conversation.read(ref)
database.conversation.readRow(ref, rowPosition)
database.conversation.readSession(ref, sessionId)
database.conversation.append(ref, row)
database.conversation.insertBeforeSession(ref, sessionId, row)  -> Chèn 1 row vào trước row đầu tiên của sessionId
```

```ts
type ConversationScope =
  | { kind: "normal" }
  | { kind: "repository"; repositoryPath: string }

interface ConversationRef {
  scope: ConversationScope
  conversationId: string
}
```

Conversation row public dùng `rowPosition` làm vị trí row:

```ts
interface ConversationRow {
  rowPosition: number
  type: string
  role: string | null
  content: string | null
  delta: string | null
  sessionId: string | null
  requestId: string | null
  eventIndex: number | null
  createdAt: number
}
```

Invariant:

```text
rowPosition = 1, 2, 3, ... N
row kế tiếp = row hiện tại + 1
```

`insertBeforeSession()` chèn ngay trước row thường đầu tiên của Session và dịch
các row từ vị trí chèn trở đi `+1`. `rowPosition` là vị trí, không phải ID ổn định.

## 4. Context usage

```ts
database.contextUsage.get(ref)
database.contextUsage.set(ref, usage)
```

## 5. Algorithm

```ts
database.algorithm.append(row)
database.algorithm.insert(rowPosition, row)
database.algorithm.delete(rowPosition)
```

```ts
interface AlgorithmRowInput {
  block: string
  description: string
  formula: string
  typeFormula: string
}

interface AlgorithmRow extends AlgorithmRowInput {
  rowPosition: number
}
```

Algorithm cũng dùng vị trí nguyên liên tiếp `1..N`:

```text
append      -> thêm tại N + 1
insert(P)   -> chèn tại P, các row >= P dịch +1
delete(P)   -> xóa P, các row > P dịch -1
```

Hiện chưa có public API query/read cho Algorithm.

## 6. Private

Không public:

```text
SQLite handle
openHistory
historyExists
deleteHistory
listHistories
baseDir
projectsDir
filePath
NORMAL_CONVERSATION_SCOPE
raw registry JSON
raw SQLite column names
storage path helpers
model registry
custom model credential
selected model
```

Model không thuộc Database. Toàn bộ model ownership thuộc `@hh/provider`.

> Consumer chỉ biết dữ liệu và thao tác nghiệp vụ; cách Database lưu dữ liệu là private.
