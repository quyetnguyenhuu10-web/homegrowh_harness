import type { ProviderName } from "@homegrowh/provider";

export interface HistoryRow {
  id: number;
  type: string;
  role: string | null;
  content: string | null;
  delta: string | null;
  API_sessions: string | null;
  request_id: string | null;
  event_index: number | null;
  created_at: number;
}

export interface HistoryConversationRecord {
  id: string;
  createdAt: number;
  user: boolean;
}

export interface HistoryRepository {
  name: string;
  repositoryPath: string;
  historyBaseDir: string;
  conversations: HistoryConversationRecord[];
}

export interface HistorySelectedModel {
  provider: ProviderName;
  model: string;
}

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
  API_sessions: string;
  trigger: "user" | "toolresult";
  reasoningHistory: ReasoningHistoryPolicy;
  updatedAt: number;
}
