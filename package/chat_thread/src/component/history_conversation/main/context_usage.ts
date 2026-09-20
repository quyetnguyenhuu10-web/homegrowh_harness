import {
  existsSync,
  mkdirSync,
  readFileSync,
  renameSync,
  writeFileSync,
} from "node:fs";
import { dirname, join, resolve } from "node:path";

import type { ProviderName } from "@homegrowh/provider";

import type { HistoryContextUsage, HistoryRow } from "../history_contract";
import type { ReasoningHistoryPolicy } from "../history_contract";
import { NORMAL_CONVERSATION_SCOPE } from "../scope";
import {
  defaultProjectsDir,
  listRepositories,
  readConversation,
  type AddRepositoryOptions,
} from "./repository";
import {
  DEFAULT_REASONING_HISTORY_POLICY,
  normalizeReasoningHistoryPolicy,
  selectReasoningRequestIds,
} from "./reasoning_history";

interface ContextUsageFile {
  version: 1;
  conversations: Record<string, HistoryContextUsage>;
}

export interface WriteConversationContextUsageInput {
  provider: ProviderName;
  model: string;
  API_sessions: string;
  trigger: "user" | "toolresult";
  contextLimitTokens: number;
  contextLimitCharacters: number;
  charactersPerToken: number;
  reasoningHistory?: ReasoningHistoryPolicy;
}

function emptyFile(): ContextUsageFile {
  return { version: 1, conversations: {} };
}

function contextUsageFile(
  repositoryPath: string,
  options: AddRepositoryOptions,
): string {
  const projectsDir = resolve(options.projectsDir ?? defaultProjectsDir());
  if (repositoryPath === NORMAL_CONVERSATION_SCOPE) {
    return join(projectsDir, ".chat_normal.json");
  }

  const normalized = resolve(repositoryPath);
  const repository = listRepositories({ projectsDir }).find(
    (item) => resolve(item.repositoryPath) === normalized,
  );
  if (!repository) {
    throw new Error(`Repository chưa được đăng ký: ${normalized}`);
  }
  return join(repository.historyBaseDir, ".chat_project.json");
}

function readFile(file: string): ContextUsageFile {
  if (!existsSync(file)) return emptyFile();
  const text = readFileSync(file, "utf8").trim();
  if (!text) return emptyFile();
  const parsed = JSON.parse(text) as Partial<ContextUsageFile>;
  return {
    version: 1,
    conversations:
      parsed.conversations && typeof parsed.conversations === "object"
        ? (parsed.conversations as Record<string, HistoryContextUsage>)
        : {},
  };
}

function writeFile(file: string, data: ContextUsageFile): void {
  mkdirSync(dirname(file), { recursive: true });
  const temp = `${file}.tmp`;
  writeFileSync(temp, `${JSON.stringify(data, null, 2)}\n`, "utf8");
  renameSync(temp, file);
}

export function countConversationContextCharacters(
  rows: readonly HistoryRow[],
  reasoningHistory: ReasoningHistoryPolicy = DEFAULT_REASONING_HISTORY_POLICY,
): number {
  let characters = 0;
  const deltaGroups = new Map<string, string>();
  const reasoningRequestIds = selectReasoningRequestIds(
    rows,
    normalizeReasoningHistoryPolicy(reasoningHistory),
  );

  const count = (text: string | null | undefined): void => {
    if (!text) return;
    for (const _character of text) characters += 1;
  };

  for (const row of rows) {
    if (row.type === "user") {
      count(row.content);
      continue;
    }
    if (
      row.type === "reply" ||
      row.type === "toolcall" ||
      (row.type === "reasoning" &&
        row.request_id !== null &&
        reasoningRequestIds.has(row.request_id))
    ) {
      if (
        row.delta !== null &&
        row.API_sessions !== null &&
        row.event_index !== null
      ) {
        const key = `${row.API_sessions}\u0000${row.event_index}`;
        deltaGroups.set(key, `${deltaGroups.get(key) ?? ""}${row.delta}`);
      } else {
        count(row.content ?? row.delta);
      }
      continue;
    }
    if (row.type === "toolresult" || row.type === "toolresults") {
      count(row.content);
      continue;
    }
    if (
      row.type === "message" &&
      (row.role === "user" || row.role === "assistant")
    ) {
      count(row.content);
    }
  }

  for (const text of deltaGroups.values()) count(text);
  return characters;
}

export function writeConversationContextUsage(
  repositoryPath: string,
  conversationId: string,
  input: WriteConversationContextUsageInput,
  options: AddRepositoryOptions = {},
): HistoryContextUsage {
  const rows = readConversation(repositoryPath, conversationId, options);
  const reasoningHistory = normalizeReasoningHistoryPolicy(
    input.reasoningHistory,
  );
  const characters = countConversationContextCharacters(rows, reasoningHistory);
  const estimatedTokens = Math.ceil(characters / input.charactersPerToken);
  const usage: HistoryContextUsage = {
    conversationId,
    provider: input.provider,
    model: input.model,
    characters,
    estimatedTokens,
    contextLimitTokens: input.contextLimitTokens,
    contextLimitCharacters: input.contextLimitCharacters,
    usageRatio:
      input.contextLimitCharacters > 0
        ? characters / input.contextLimitCharacters
        : 0,
    API_sessions: input.API_sessions,
    trigger: input.trigger,
    reasoningHistory,
    updatedAt: Date.now(),
  };

  const file = contextUsageFile(repositoryPath, options);
  const data = readFile(file);
  data.conversations[conversationId] = usage;
  writeFile(file, data);
  return usage;
}

export function readConversationContextUsage(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
): HistoryContextUsage | null {
  const file = contextUsageFile(repositoryPath, options);
  return readFile(file).conversations[conversationId] ?? null;
}
