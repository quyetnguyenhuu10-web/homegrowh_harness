// API SQLite thống nhất cho lịch sử hội thoại nội bộ chat_thread.
// Mặc định các DB nằm dưới projects/. Khi gắn với repo, caller dùng
// historyBaseDir trả về từ addRepository() để DB nằm trong projects/<repo>/.
import { DatabaseSync } from "node:sqlite";
import { existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { HistoryRow } from "../history_contract";
import type { HistoryHandle, PushRowInput } from "./types";

/**
 * Thư mục chứa .db mặc định:
 * src/data/history_conversation/projects/ của chat_thread.
 * Tìm chat_thread root bằng package.json để không đổi vị trí lưu chỉ vì
 * module đang chạy từ source hay build output.
 */
export function defaultBaseDir(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  for (let i = 0; i < 6; i++) {
    if (existsSync(join(dir, "package.json"))) {
      return join(
        dir,
        "src",
        "data",
        "history_conversation",
        "projects",
      );
    }
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return resolve(
    dirname(fileURLToPath(import.meta.url)),
    "../../../data/history_conversation/projects",
  );
}

/** Id chỉ cho phép chữ/số/gạch để thành tên file an toàn. */
function checkId(id: string): void {
  if (!id || !/^[A-Za-z0-9_-]{1,128}$/.test(id)) {
    throw new Error(
      `Id lịch sử không hợp lệ: ${JSON.stringify(id)} (chỉ dùng A-Za-z0-9 _ -)`,
    );
  }
}

function dbFile(baseDir: string, id: string): string {
  return join(resolve(baseDir), `${id}.db`);
}

const SCHEMA = `
CREATE TABLE IF NOT EXISTS rows (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  type TEXT NOT NULL,
  role TEXT,
  content TEXT,
  delta TEXT,
  API_sessions TEXT,
  request_id TEXT,
  event_index INTEGER,
  created_at INTEGER NOT NULL
);
`;

const ROW_COLUMNS = [
  "id",
  "type",
  "role",
  "content",
  "delta",
  "API_sessions",
  "request_id",
  "event_index",
  "created_at",
] as const;

function tableColumns(db: DatabaseSync): Set<string> {
  const rows = db.prepare("PRAGMA table_info(rows)").all() as Array<
    Record<string, unknown>
  >;
  return new Set(
    rows
      .map((row) => row.name)
      .filter((name): name is string => typeof name === "string"),
  );
}

function ensureCurrentSchema(db: DatabaseSync): void {
  const columns = tableColumns(db);
  const exactSchema =
    columns.size === ROW_COLUMNS.length &&
    ROW_COLUMNS.every((column) => columns.has(column));
  if (exactSchema) return;

  const contentExpr = columns.has("content") ? "content" : "NULL";
  const deltaExpr = columns.has("delta") ? "delta" : "NULL";
  const apiSessionsExpr = columns.has("API_sessions")
    ? "API_sessions"
    : "NULL";
  const requestIdExpr = columns.has("request_id") ? "request_id" : "NULL";
  const eventIndexExpr = columns.has("event_index") ? "event_index" : "NULL";

  db.exec("BEGIN IMMEDIATE;");
  try {
    db.exec(`
      DROP TABLE IF EXISTS rows_next;
      CREATE TABLE rows_next (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        type TEXT NOT NULL,
        role TEXT,
        content TEXT,
        delta TEXT,
        API_sessions TEXT,
        request_id TEXT,
        event_index INTEGER,
        created_at INTEGER NOT NULL
      );
      INSERT INTO rows_next (
        id,
        type,
        role,
        content,
        delta,
        API_sessions,
        request_id,
        event_index,
        created_at
      )
      SELECT
        id,
        type,
        role,
        ${contentExpr},
        ${deltaExpr},
        ${apiSessionsExpr},
        ${requestIdExpr},
        ${eventIndexExpr},
        created_at
      FROM rows
      ORDER BY id ASC;
      DROP TABLE rows;
      ALTER TABLE rows_next RENAME TO rows;
      CREATE INDEX IF NOT EXISTS idx_rows_type ON rows(type);
      CREATE INDEX IF NOT EXISTS idx_rows_created ON rows(created_at);
      CREATE INDEX IF NOT EXISTS idx_rows_api_sessions ON rows(API_sessions);
      CREATE INDEX IF NOT EXISTS idx_rows_request_id ON rows(request_id, id);
      CREATE INDEX IF NOT EXISTS idx_rows_api_session_event
        ON rows(API_sessions, event_index, id);
    `);
    db.exec("COMMIT;");
  } catch (error) {
    db.exec("ROLLBACK;");
    throw error;
  }
}

function ensureIndexes(db: DatabaseSync): void {
  db.exec(`
    CREATE INDEX IF NOT EXISTS idx_rows_type ON rows(type);
    CREATE INDEX IF NOT EXISTS idx_rows_created ON rows(created_at);
    CREATE INDEX IF NOT EXISTS idx_rows_api_sessions ON rows(API_sessions);
    CREATE INDEX IF NOT EXISTS idx_rows_request_id ON rows(request_id, id);
    CREATE INDEX IF NOT EXISTS idx_rows_api_session_event
      ON rows(API_sessions, event_index, id);
  `);
}

function toRow(raw: Record<string, unknown>): HistoryRow {
  return {
    id: raw.id as number,
    type: raw.type as string,
    role: (raw.role as string | null) ?? null,
    content: (raw.content as string | null) ?? null,
    delta: (raw.delta as string | null) ?? null,
    API_sessions: (raw.API_sessions as string | null) ?? null,
    request_id: (raw.request_id as string | null) ?? null,
    event_index:
      typeof raw.event_index === "number" ? raw.event_index : null,
    created_at: raw.created_at as number,
  };
}

export interface OpenHistoryOptions {
  /** Thư mục chứa .db. Mặc định data/history_conversation/projects/. */
  baseDir?: string;
  /** Chế độ SQLite open (mặc định đọc/ghi + tự tạo file). */
  readOnly?: boolean;
}

/**
 * Tạo/mở id: mở (hoặc tạo mới) file `<id>.db` và trả tay cầm.
 * Gọi nhiều lần cùng id trả về các tay cầm độc lập cùng trỏ 1 file.
 */
export function openHistory(
  id: string,
  opts: OpenHistoryOptions = {},
): HistoryHandle {
  checkId(id);
  const baseDir = resolve(opts.baseDir ?? defaultBaseDir());
  mkdirSync(baseDir, { recursive: true });
  const file = dbFile(baseDir, id);

  const db = new DatabaseSync(file, opts.readOnly ? { readOnly: true } : {});
  if (!opts.readOnly) {
    db.exec("PRAGMA journal_mode = WAL;");
    db.exec(SCHEMA);
    ensureCurrentSchema(db);
    ensureIndexes(db);
  }

  const columns = tableColumns(db);
  const contentSelect = columns.has("content") ? "content" : "NULL AS content";
  const deltaSelect = columns.has("delta") ? "delta" : "NULL AS delta";
  const apiSessionsSelect = columns.has("API_sessions")
    ? "API_sessions"
    : "NULL AS API_sessions";
  const requestIdSelect = columns.has("request_id")
    ? "request_id"
    : "NULL AS request_id";
  const eventIndexSelect = columns.has("event_index")
    ? "event_index"
    : "NULL AS event_index";

  const insertStmt = opts.readOnly
    ? null
    : db.prepare(
        "INSERT INTO rows (type, role, content, delta, API_sessions, request_id, event_index, created_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
      );
  const listStmt = db.prepare(
    "SELECT id, type, role, " +
      contentSelect +
      ", " +
      deltaSelect +
      ", " +
      apiSessionsSelect +
      ", " +
      requestIdSelect +
      ", " +
      eventIndexSelect +
      ", created_at FROM rows ORDER BY id DESC LIMIT ? OFFSET ?",
  );
  const getStmt = db.prepare(
    "SELECT id, type, role, " +
      contentSelect +
      ", " +
      deltaSelect +
      ", " +
      apiSessionsSelect +
      ", " +
      requestIdSelect +
      ", " +
      eventIndexSelect +
      ", created_at FROM rows WHERE id = ?",
  );
  const deleteStmt = db.prepare("DELETE FROM rows WHERE id = ?");
  const countStmt = db.prepare("SELECT COUNT(*) AS n FROM rows");

  let isClosed = false;
  function mustOpen(): void {
    if (isClosed) throw new Error(`Lịch sử "${id}" đã close().`);
  }

  return {
    id,
    file,
    db,

    push(row: PushRowInput): HistoryRow {
      mustOpen();
      if (!insertStmt) {
        throw new Error(`Lịch sử "${id}" đang mở read-only.`);
      }
      if (!row.type) {
        throw new Error("push() cần type không rỗng.");
      }
      if (!row.API_sessions.trim()) {
        throw new Error("push() cần API_sessions không rỗng.");
      }
      if (row.request_id !== undefined && row.request_id !== null && !row.request_id.trim()) {
        throw new Error("request_id nếu có phải không rỗng.");
      }
      if (
        row.delta !== undefined &&
        row.delta !== null &&
        (!Number.isInteger(row.event_index) || (row.event_index ?? 0) < 1)
      ) {
        throw new Error("Row có delta cần event_index nguyên >= 1.");
      }
      const createdAt = Date.now();
      const info = insertStmt.run(
        row.type,
        row.role ?? null,
        row.content ?? null,
        row.delta ?? null,
        row.API_sessions,
        row.request_id ?? null,
        row.event_index ?? null,
        createdAt,
      );
      const rowId = Number(info.lastInsertRowid);
      return {
        id: rowId,
        type: row.type,
        role: row.role ?? null,
        content: row.content ?? null,
        delta: row.delta ?? null,
        API_sessions: row.API_sessions,
        request_id: row.request_id ?? null,
        event_index: row.event_index ?? null,
        created_at: createdAt,
      };
    },

    list(limit = 100, offset = 0): HistoryRow[] {
      mustOpen();
      const safeLimit = Math.max(1, Math.min(10000, Math.floor(limit)));
      const safeOffset = Math.max(0, Math.floor(offset));
      return (listStmt.all(safeLimit, safeOffset) as Array<Record<string, unknown>>).map(toRow);
    },

    get(rowId: number): HistoryRow | null {
      mustOpen();
      const raw = getStmt.get(rowId) as Record<string, unknown> | undefined;
      return raw === undefined ? null : toRow(raw);
    },

    deleteRow(rowId: number): boolean {
      mustOpen();
      const info = deleteStmt.run(rowId);
      return Number(info.changes) > 0;
    },

    count(): number {
      mustOpen();
      const raw = countStmt.get() as Record<string, unknown> | undefined;
      return Number(raw?.n ?? 0);
    },

    clear(): void {
      mustOpen();
      db.exec("DELETE FROM rows;");
    },

    close(): void {
      if (isClosed) return;
      isClosed = true;
      db.close();
    },
  };
}

/** File .db của id có tồn tại không. */
export function historyExists(
  id: string,
  opts: OpenHistoryOptions = {},
): boolean {
  checkId(id);
  return existsSync(dbFile(opts.baseDir ?? defaultBaseDir(), id));
}

/**
 * Xóa id: đóng không cần thiết (mỗi tay cầm tự quản lý),
 * xóa file `<id>.db` kèm các file phụ WAL/SHM/journal.
 * Trả true nếu file từng tồn tại.
 */
export function deleteHistory(
  id: string,
  opts: OpenHistoryOptions = {},
): boolean {
  checkId(id);
  const file = dbFile(opts.baseDir ?? defaultBaseDir(), id);
  let existed = false;
  for (const suffix of ["", "-wal", "-shm", "-journal"]) {
    const target = suffix === "" ? file : `${file}${suffix}`;
    if (existsSync(target)) {
      existed = true;
      rmSync(target, { force: true });
    }
  }
  return existed;
}

/** Liệt kê mọi id đang có (tên file .db trong thư mục). */
export function listHistories(opts: OpenHistoryOptions = {}): string[] {
  const baseDir = resolve(opts.baseDir ?? defaultBaseDir());
  if (!existsSync(baseDir)) return [];
  return readdirSync(baseDir)
    .filter((name) => name.endsWith(".db"))
    .map((name) => name.slice(0, -".db".length))
    .sort();
}
