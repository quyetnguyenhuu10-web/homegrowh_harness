import type { ConversationRow } from "@hh/database";
import type { ModelSelection, ProviderName } from "@hh/provider";

export type HistoryRow = ConversationRow;

export interface HistoryConversationRecord {
  id: string;
  createdAt: number;
  active: boolean;
}

export interface HistoryRepository {
  name: string;
  repositoryPath: string;
  conversations: HistoryConversationRecord[];
}

export type HistorySelectedModel = ModelSelection & { provider: ProviderName };

export interface HistoryActiveConversation {
  repositoryPath: string;
  conversationId: string;
}

export type ReasoningHistoryPolicy =
  | { mode: "recent"; requestCount: number }
  | { mode: "all" };

export interface HistoryContextUsage {
  conversationId: string;
  provider: ProviderName;
  model: string;
  characters: number;
  estimatedTokens: number;
  contextLimitTokens: number;
  contextLimitCharacters: number;
  usageRatio: number;
  sessionId: string | null;
  requestId: string;
  trigger: "user" | "toolresult";
  reasoningHistory: ReasoningHistoryPolicy;
  updatedAt: number;
}
