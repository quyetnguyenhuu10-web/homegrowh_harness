import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import type { ChatMessage } from "@homegrowh/provider";

function chatThreadRoot(): string {
  let dir = dirname(fileURLToPath(import.meta.url));
  for (let index = 0; index < 8; index += 1) {
    if (existsSync(join(dir, "package.json"))) return dir;
    const parent = dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  throw new Error("Không tìm thấy root package chat_thread.");
}

export function systemPromptDirectory(): string {
  return join(
    chatThreadRoot(),
    "src",
    "component",
    "context",
    "main",
    "prompt_system",
  );
}

/**
 * Danh sách system prompt được bật chủ động.
 *
 * prompt_system/ chỉ là kho file .md. File nằm trong thư mục nhưng không có tên
 * ở đây sẽ KHÔNG được đưa vào context model.
 *
 * Thứ tự trong mảng chính là thứ tự system message gửi cho model.
 */
export const ACTIVE_SYSTEM_PROMPT_FILES: readonly string[] = [
  "WORKSPACE.md",
];

/** Chỉ load các file được khai báo explicit trong ACTIVE_SYSTEM_PROMPT_FILES. */
export interface SystemPromptVariables {
  activeRepositoryPath: string;
}

function renderSystemPrompt(
  content: string,
  variables: SystemPromptVariables,
): string {
  return content.replaceAll(
    "{{ACTIVE_REPOSITORY_PATH}}",
    variables.activeRepositoryPath,
  );
}

export function loadSystemPromptMessages(
  variables: SystemPromptVariables,
): ChatMessage[] {
  const dir = systemPromptDirectory();
  if (!existsSync(dir)) return [];

  return ACTIVE_SYSTEM_PROMPT_FILES.map((name) => {
    if (!name.toLowerCase().endsWith(".md")) {
      throw new Error(`System prompt phải là file .md: ${name}`);
    }

    const file = join(dir, name);
    if (!existsSync(file)) {
      throw new Error(`Không tìm thấy system prompt đã khai báo: ${file}`);
    }

    return renderSystemPrompt(readFileSync(file, "utf8"), variables);
  })
    .filter((content) => content.trim().length > 0)
    .map((content) => ({ role: "system" as const, content }));
}
