// API SQLite thống nhất cho lịch sử hội thoại.
// Quy ước file: mỗi id = 1 file `<id>.db` trong thư mục output
// (mặc định src/output/, đổi được qua opts.baseDir).
import { DatabaseSync } from "node:sqlite";
import { existsSync, mkdirSync, readdirSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import type { HistoryHandle, HistoryRow, PushRowInput } from "./types.js";

/**
 * Thư mục chứa .db mặc định: src/output/ của package (thư mục có sẵn).
 * Tìm package root bằng cách đi ngược lên tới package.json để chạy đúng
 * cả khi code nằm ở src/ lẫn dist/ sau build.
 */
export function defaultBaseDir(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  for (let i = 0; i < 6; i++) {
    if (existsSync(join(dir, "package.json"))) {
      return join(dir, "src", "output");
    }
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  return resolve(dirname(fileURLToPath(import.meta.url)), "output");
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
  error_message TEXT,
  content TEXT NOT NULL DEFAULT '',
  source_image TEXT,
  source_path TEXT,
  created_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_rows_type ON rows(type);
CREATE INDEX IF NOT EXISTS idx_rows_created ON rows(created_at);
`;

function toRow(raw: Record<string, unknown>): HistoryRow {
  return {
    id: raw.id as number,
    type: raw.type as string,
    role: (raw.role as string | null) ?? null,
    error_message: (raw.error_message as string | null) ?? null,
    content: (raw.content as string) ?? "",
    source_image: (raw.source_image as string | null) ?? null,
    source_path: (raw.source_path as string | null) ?? null,
    created_at: raw.created_at as number,
  };
}

export interface OpenHistoryOptions {
  /** Thư mục chứa .db. Mặc định src/output/. */
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
  }

  const insertStmt = db.prepare(
    "INSERT INTO rows (type, role, error_message, content, source_image, source_path, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
  );
  const listStmt = db.prepare(
    "SELECT id, type, role, error_message, content, source_image, source_path, created_at FROM rows ORDER BY id DESC LIMIT ? OFFSET ?",
  );
  const getStmt = db.prepare(
    "SELECT id, type, role, error_message, content, source_image, source_path, created_at FROM rows WHERE id = ?",
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
      if (!row.type || typeof row.content !== "string") {
        throw new Error("push() cần { type: string, content: string }.");
      }
      const createdAt = Date.now();
      const info = insertStmt.run(
        row.type,
        row.role ?? null,
        row.error_message ?? null,
        row.content,
        row.source_image ?? null,
        row.source_path ?? null,
        createdAt,
      );
      const rowId = Number(info.lastInsertRowid);
      return {
        id: rowId,
        type: row.type,
        role: row.role ?? null,
        error_message: row.error_message ?? null,
        content: row.content,
        source_image: row.source_image ?? null,
        source_path: row.source_path ?? null,
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
