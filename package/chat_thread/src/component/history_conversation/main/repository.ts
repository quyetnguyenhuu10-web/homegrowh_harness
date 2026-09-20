import {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  statSync,
  writeFileSync,
} from "node:fs";
import { randomUUID } from "node:crypto";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import { deleteHistory, historyExists, openHistory } from "./store";
import { NORMAL_CONVERSATION_SCOPE } from "../scope";
import type {
  HistoryActiveConversation,
  HistoryConversationRecord,
  HistoryRepository,
  HistoryRow,
  HistorySelectedModel,
} from "../history_contract";
import type { PushRowInput } from "./types";

export interface AddRepositoryOptions {
  /** Override root projects, chủ yếu cho test/host đặc biệt. */
  projectsDir?: string;
}

interface RepositoryRegistryEntry {
  name: string;
  repositoryPath: string;
  conversations: HistoryConversationRecord[];
}

interface RepositoryRegistry {
  version: 1;
  selectedModel: HistorySelectedModel | null;
  conversations: HistoryConversationRecord[];
  repositories: RepositoryRegistryEntry[];
}

const REGISTRY_FILE = ".repositories.json";

export function defaultProjectsDir(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  for (let i = 0; i < 6; i += 1) {
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

function registryPath(projectsDir: string): string {
  return join(projectsDir, REGISTRY_FILE);
}

function loadRegistry(projectsDir: string): RepositoryRegistry {
  const file = registryPath(projectsDir);
  if (!existsSync(file)) {
    return {
      version: 1,
      selectedModel: null,
      conversations: [],
      repositories: [],
    };
  }

  const parsed = JSON.parse(readFileSync(file, "utf8")) as Partial<RepositoryRegistry>;
  const selectedModel =
    parsed.selectedModel &&
    (parsed.selectedModel.provider === "openai" ||
      parsed.selectedModel.provider === "deepseek" ||
      parsed.selectedModel.provider === "ollama" ||
      parsed.selectedModel.provider === "custom") &&
    typeof parsed.selectedModel.model === "string" &&
    parsed.selectedModel.model.trim().length > 0
      ? {
          provider: parsed.selectedModel.provider,
          model: parsed.selectedModel.model,
        }
      : null;

  return {
    version: 1,
    selectedModel,
    conversations: Array.isArray(parsed.conversations)
      ? parsed.conversations
          .filter(
            (conversation): conversation is HistoryConversationRecord =>
              typeof conversation?.id === "string" &&
              typeof conversation?.createdAt === "number",
          )
          .map((conversation) => ({
            id: conversation.id,
            createdAt: conversation.createdAt,
            user: conversation.user === true,
          }))
      : [],
    repositories: Array.isArray(parsed.repositories)
        ? parsed.repositories
            .filter(
              (entry): entry is RepositoryRegistryEntry =>
                typeof entry?.name === "string" &&
                typeof entry?.repositoryPath === "string",
            )
            .map((entry) => ({
              name: entry.name,
              repositoryPath: entry.repositoryPath,
              conversations: Array.isArray(entry.conversations)
                ? entry.conversations
                    .filter(
                      (
                        conversation,
                      ): conversation is HistoryConversationRecord =>
                        typeof conversation?.id === "string" &&
                        typeof conversation?.createdAt === "number",
                    )
                    .map((conversation) => ({
                      id: conversation.id,
                      createdAt: conversation.createdAt,
                      user: conversation.user === true,
                    }))
                : [],
            }))
        : [],
  };
}

/** Model đang chọn của toàn app, không thuộc repository hay conversation nào. */
export function getSelectedModel(
  options: AddRepositoryOptions = {},
): HistorySelectedModel | null {
  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  return loadRegistry(projectsDir).selectedModel;
}

/** Ghi model đang chọn ở top-level của .repositories.json. */
export function setSelectedModel(
  selection: HistorySelectedModel,
  options: AddRepositoryOptions = {},
): HistorySelectedModel {
  if (!selection.model.trim()) {
    throw new Error("setSelectedModel() cần model.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  mkdirSync(projectsDir, { recursive: true });

  const registry = loadRegistry(projectsDir);
  registry.selectedModel = {
    provider: selection.provider,
    model: selection.model.trim(),
  };
  saveRegistry(projectsDir, registry);
  return registry.selectedModel;
}

function normalHistoryBaseDir(projectsDir: string): string {
  return join(projectsDir, ".conversations");
}

function isNormalConversationScope(repositoryPath: string): boolean {
  return repositoryPath === NORMAL_CONVERSATION_SCOPE;
}

/** Đọc các conversation thường không thuộc repository. */
export function listConversations(
  options: AddRepositoryOptions = {},
): HistoryConversationRecord[] {
  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  return loadRegistry(projectsDir).conversations;
}

/** Đọc conversation đang active trực tiếp từ registry hiện tại. */
export function getActiveConversation(
  options: AddRepositoryOptions = {},
): HistoryActiveConversation | null {
  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  const normalConversation = registry.conversations.find(
    (conversation) => conversation.user,
  );
  if (normalConversation) {
    return {
      repositoryPath: NORMAL_CONVERSATION_SCOPE,
      conversationId: normalConversation.id,
    };
  }

  for (const repository of registry.repositories) {
    const conversation = repository.conversations.find((item) => item.user);
    if (!conversation) continue;
    return {
      repositoryPath: resolve(repository.repositoryPath),
      conversationId: conversation.id,
    };
  }

  return null;
}

function saveRegistry(projectsDir: string, registry: RepositoryRegistry): void {
  const file = registryPath(projectsDir);
  const temp = `${file}.tmp`;
  writeFileSync(temp, `${JSON.stringify(registry, null, 2)}\n`, "utf8");
  renameSync(temp, file);
}

/** Đọc danh sách repository đã đăng ký từ .repositories.json. */
export function listRepositories(
  options: AddRepositoryOptions = {},
): HistoryRepository[] {
  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  return registry.repositories.map((entry) => ({
    name: entry.name,
    repositoryPath: resolve(entry.repositoryPath),
    historyBaseDir: join(projectsDir, entry.name),
    conversations: entry.conversations,
  }));
}

function samePath(left: string, right: string): boolean {
  const a = resolve(left);
  const b = resolve(right);
  return process.platform === "win32"
    ? a.toLocaleLowerCase() === b.toLocaleLowerCase()
    : a === b;
}

/**
 * Đăng ký một repository cho conversation history.
 *
 * Thành công sẽ bảo đảm tồn tại:
 *   projects/<tên repo>/
 *
 * Các file lịch sử chat của repo phải tiếp tục dùng:
 *   openHistory(chatId, { baseDir: result.historyBaseDir })
 *
 * Gọi lại cùng path là idempotent. Hai repo khác path nhưng trùng tên folder
 * bị từ chối để không trộn DB lịch sử vào cùng một thư mục.
 */
export function addRepository(
  repositoryPath: string,
  options: AddRepositoryOptions = {},
): HistoryRepository {
  const normalizedPath = resolve(repositoryPath.trim());
  if (!repositoryPath.trim()) {
    throw new Error("addRepository() cần đường dẫn repository.");
  }

  if (!existsSync(normalizedPath) || !statSync(normalizedPath).isDirectory()) {
    throw new Error(`Repository không tồn tại hoặc không phải thư mục: ${normalizedPath}`);
  }

  const name = basename(normalizedPath);
  if (!name || name === "." || name === "..") {
    throw new Error(`Không xác định được tên repository từ: ${normalizedPath}`);
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  mkdirSync(projectsDir, { recursive: true });

  const registry = loadRegistry(projectsDir);
  const sameName = registry.repositories.find((entry) => entry.name === name);
  if (sameName && !samePath(sameName.repositoryPath, normalizedPath)) {
    throw new Error(
      `Đã có repository tên "${name}" nhưng trỏ tới path khác: ${sameName.repositoryPath}`,
    );
  }

  if (!sameName) {
    registry.repositories.push({
      name,
      repositoryPath: normalizedPath,
      conversations: [],
    });
    saveRegistry(projectsDir, registry);
  }

  const historyBaseDir = join(projectsDir, name);
  mkdirSync(historyBaseDir, { recursive: true });

  return {
    name,
    repositoryPath: normalizedPath,
    historyBaseDir,
    conversations: sameName?.conversations ?? [],
  };
}

/**
 * Tạo conversation mới thuộc đúng repository:
 * - ghi metadata vào entry repo trong .repositories.json
 * - tạo projects/<repo>/<id>.db với schema history chuẩn.
 */
export function addConversation(
  repositoryPath: string,
  options: AddRepositoryOptions = {},
): HistoryConversationRecord {
  if (isNormalConversationScope(repositoryPath)) {
    const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
    const registry = loadRegistry(projectsDir);
    const historyBaseDir = normalHistoryBaseDir(projectsDir);
    mkdirSync(historyBaseDir, { recursive: true });

    let id = randomUUID();
    while (historyExists(id, { baseDir: historyBaseDir })) {
      id = randomUUID();
    }

    const history = openHistory(id, { baseDir: historyBaseDir });
    history.close();

    const conversation: HistoryConversationRecord = {
      id,
      createdAt: Date.now(),
      user: false,
    };

    registry.conversations.push(conversation);
    try {
      saveRegistry(projectsDir, registry);
    } catch (error) {
      deleteHistory(id, { baseDir: historyBaseDir });
      registry.conversations.pop();
      throw error;
    }

    return conversation;
  }

  const normalizedPath = resolve(repositoryPath.trim());
  if (!repositoryPath.trim()) {
    throw new Error("addConversation() cần đường dẫn repository.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);
  const repository = registry.repositories.find((entry) =>
    samePath(entry.repositoryPath, normalizedPath),
  );

  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalizedPath}`);
  }

  const historyBaseDir = join(projectsDir, repository.name);
  mkdirSync(historyBaseDir, { recursive: true });

  let id = randomUUID();
  while (historyExists(id, { baseDir: historyBaseDir })) {
    id = randomUUID();
  }

  const history = openHistory(id, { baseDir: historyBaseDir });
  history.close();

  const conversation: HistoryConversationRecord = {
    id,
    createdAt: Date.now(),
    user: false,
  };

  repository.conversations.push(conversation);
  try {
    saveRegistry(projectsDir, registry);
  } catch (error) {
    deleteHistory(id, { baseDir: historyBaseDir });
    repository.conversations.pop();
    throw error;
  }

  return conversation;
}

/**
 * Đánh dấu conversation đang được user mở.
 *
 * Toàn registry chỉ có tối đa một conversation user=true.
 */
export function setActiveConversation(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
): void {
  if (!repositoryPath.trim()) {
    throw new Error("setActiveConversation() cần đường dẫn repository.");
  }
  if (!conversationId.trim()) {
    throw new Error("setActiveConversation() cần conversationId.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);
  let found = false;
  const normalTarget = isNormalConversationScope(repositoryPath);
  const normalizedPath = normalTarget ? null : resolve(repositoryPath.trim());

  for (const conversation of registry.conversations) {
    const active = normalTarget && conversation.id === conversationId;
    conversation.user = active;
    if (active) found = true;
  }

  for (const repository of registry.repositories) {
    const targetRepository =
      normalizedPath !== null && samePath(repository.repositoryPath, normalizedPath);

    for (const conversation of repository.conversations) {
      const active =
        targetRepository && conversation.id === conversationId;
      conversation.user = active;
      if (active) found = true;
    }
  }

  if (!found) {
    throw new Error(`Không tìm thấy conversation "${conversationId}".`);
  }

  saveRegistry(projectsDir, registry);
}

/**
 * Xóa conversation thuộc đúng repository:
 * - gỡ metadata khỏi entry repo trong .repositories.json
 * - xóa projects/<repo>/<id>.db và WAL/SHM/journal nếu có.
 *
 * Trả false nếu repo có tồn tại nhưng không chứa conversationId.
 */
export function deleteConversation(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
): boolean {
  if (!repositoryPath.trim()) {
    throw new Error("deleteConversation() cần đường dẫn repository.");
  }
  if (!conversationId.trim()) {
    throw new Error("deleteConversation() cần conversationId.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  if (isNormalConversationScope(repositoryPath)) {
    const conversationIndex = registry.conversations.findIndex(
      (conversation) => conversation.id === conversationId,
    );
    if (conversationIndex < 0) return false;

    const [conversation] = registry.conversations.splice(conversationIndex, 1);
    try {
      saveRegistry(projectsDir, registry);
    } catch (error) {
      registry.conversations.splice(conversationIndex, 0, conversation);
      throw error;
    }

    const historyBaseDir = normalHistoryBaseDir(projectsDir);
    try {
      deleteHistory(conversationId, { baseDir: historyBaseDir });
    } catch (error) {
      registry.conversations.splice(conversationIndex, 0, conversation);
      try {
        saveRegistry(projectsDir, registry);
      } catch {
        // Giữ lỗi xóa DB là lỗi chính.
      }
      throw error;
    }

    return true;
  }

  const normalizedPath = resolve(repositoryPath.trim());
  const repository = registry.repositories.find((entry) =>
    samePath(entry.repositoryPath, normalizedPath),
  );

  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalizedPath}`);
  }

  const conversationIndex = repository.conversations.findIndex(
    (conversation) => conversation.id === conversationId,
  );
  if (conversationIndex < 0) return false;

  const [conversation] = repository.conversations.splice(conversationIndex, 1);
  try {
    saveRegistry(projectsDir, registry);
  } catch (error) {
    repository.conversations.splice(conversationIndex, 0, conversation);
    throw error;
  }

  const historyBaseDir = join(projectsDir, repository.name);
  try {
    deleteHistory(conversationId, { baseDir: historyBaseDir });
  } catch (error) {
    repository.conversations.splice(conversationIndex, 0, conversation);
    try {
      saveRegistry(projectsDir, registry);
    } catch {
      // Giữ lỗi xóa DB là lỗi chính; caller vẫn biết thao tác chưa hoàn tất sạch.
    }
    throw error;
  }

  return true;
}

/**
 * Đọc toàn bộ rows của một conversation thuộc đúng repository, theo thứ tự cũ -> mới.
 * API này chỉ đọc; không tạo DB mới và không thay đổi metadata.
 */
export function readConversation(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
): HistoryRow[] {
  if (!repositoryPath.trim()) {
    throw new Error("readConversation() cần đường dẫn repository.");
  }
  if (!conversationId.trim()) {
    throw new Error("readConversation() cần conversationId.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  if (isNormalConversationScope(repositoryPath)) {
    if (!registry.conversations.some((conversation) => conversation.id === conversationId)) {
      throw new Error(`Conversation thường không tồn tại: ${conversationId}`);
    }

    const historyBaseDir = normalHistoryBaseDir(projectsDir);
    if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
      throw new Error(`Thiếu database của conversation "${conversationId}".`);
    }

    const history = openHistory(conversationId, {
      baseDir: historyBaseDir,
      readOnly: true,
    });
    try {
      const count = history.count();
      if (count === 0) return [];

      const newestFirst: HistoryRow[] = [];
      const pageSize = 10_000;
      for (let offset = 0; offset < count; offset += pageSize) {
        newestFirst.push(
          ...history.list(Math.min(pageSize, count - offset), offset),
        );
      }
      return newestFirst.reverse();
    } finally {
      history.close();
    }
  }

  const normalizedPath = resolve(repositoryPath.trim());
  const repository = registry.repositories.find((entry) =>
    samePath(entry.repositoryPath, normalizedPath),
  );

  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalizedPath}`);
  }
  if (!repository.conversations.some((conversation) => conversation.id === conversationId)) {
    throw new Error(
      `Conversation "${conversationId}" không thuộc repository: ${normalizedPath}`,
    );
  }

  const historyBaseDir = join(projectsDir, repository.name);
  if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
    throw new Error(
      `Thiếu database của conversation "${conversationId}": ${historyBaseDir}`,
    );
  }

  const history = openHistory(conversationId, {
    baseDir: historyBaseDir,
    readOnly: true,
  });
  try {
    const count = history.count();
    if (count === 0) return [];

    const newestFirst: HistoryRow[] = [];
    const pageSize = 10_000;
    for (let offset = 0; offset < count; offset += pageSize) {
      newestFirst.push(
        ...history.list(Math.min(pageSize, count - offset), offset),
      );
    }

    return newestFirst.reverse();
  } finally {
    history.close();
  }
}

/** Đọc đúng một DB row đã tồn tại, theo id; không scan toàn conversation. */
export function readConversationRow(
  repositoryPath: string,
  conversationId: string,
  rowId: number,
  options: AddRepositoryOptions = {},
): HistoryRow | null {
  if (!repositoryPath.trim()) {
    throw new Error("readConversationRow() cần đường dẫn repository.");
  }
  if (!conversationId.trim()) {
    throw new Error("readConversationRow() cần conversationId.");
  }
  if (!Number.isInteger(rowId) || rowId < 1) {
    throw new Error("readConversationRow() cần rowId nguyên >= 1.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  if (isNormalConversationScope(repositoryPath)) {
    if (!registry.conversations.some((conversation) => conversation.id === conversationId)) {
      throw new Error(`Conversation thường không tồn tại: ${conversationId}`);
    }
    const historyBaseDir = normalHistoryBaseDir(projectsDir);
    if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
      throw new Error(`Thiếu database của conversation "${conversationId}".`);
    }
    const history = openHistory(conversationId, {
      baseDir: historyBaseDir,
      readOnly: true,
    });
    try {
      return history.get(rowId);
    } finally {
      history.close();
    }
  }

  const normalizedPath = resolve(repositoryPath.trim());
  const repository = registry.repositories.find((entry) =>
    samePath(entry.repositoryPath, normalizedPath),
  );
  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalizedPath}`);
  }
  if (!repository.conversations.some((conversation) => conversation.id === conversationId)) {
    throw new Error(
      `Conversation "${conversationId}" không thuộc repository: ${normalizedPath}`,
    );
  }

  const historyBaseDir = join(projectsDir, repository.name);
  if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
    throw new Error(
      `Thiếu database của conversation "${conversationId}": ${historyBaseDir}`,
    );
  }
  const history = openHistory(conversationId, {
    baseDir: historyBaseDir,
    readOnly: true,
  });
  try {
    return history.get(rowId);
  } finally {
    history.close();
  }
}

/**
 * Điểm ghi row duy nhất cho một conversation thuộc repository.
 * Validate ownership trước, sau đó SQLite cấp id + created_at và trả row đã persist.
 */
export function pushConversationRow(
  repositoryPath: string,
  conversationId: string,
  row: PushRowInput,
  options: AddRepositoryOptions = {},
): HistoryRow {
  if (!repositoryPath.trim()) {
    throw new Error("pushConversationRow() cần đường dẫn repository.");
  }
  if (!conversationId.trim()) {
    throw new Error("pushConversationRow() cần conversationId.");
  }

  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  const registry = loadRegistry(projectsDir);

  if (isNormalConversationScope(repositoryPath)) {
    if (!registry.conversations.some((conversation) => conversation.id === conversationId)) {
      throw new Error(`Conversation thường không tồn tại: ${conversationId}`);
    }

    const historyBaseDir = normalHistoryBaseDir(projectsDir);
    if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
      throw new Error(`Thiếu database của conversation "${conversationId}".`);
    }

    const history = openHistory(conversationId, { baseDir: historyBaseDir });
    try {
      return history.push(row);
    } finally {
      history.close();
    }
  }

  const normalizedPath = resolve(repositoryPath.trim());
  const repository = registry.repositories.find((entry) =>
    samePath(entry.repositoryPath, normalizedPath),
  );

  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalizedPath}`);
  }
  if (!repository.conversations.some((conversation) => conversation.id === conversationId)) {
    throw new Error(
      `Conversation "${conversationId}" không thuộc repository: ${normalizedPath}`,
    );
  }

  const historyBaseDir = join(projectsDir, repository.name);
  if (!historyExists(conversationId, { baseDir: historyBaseDir })) {
    throw new Error(
      `Thiếu database của conversation "${conversationId}": ${historyBaseDir}`,
    );
  }

  const history = openHistory(conversationId, { baseDir: historyBaseDir });
  try {
    return history.push(row);
  } finally {
    history.close();
  }
}
