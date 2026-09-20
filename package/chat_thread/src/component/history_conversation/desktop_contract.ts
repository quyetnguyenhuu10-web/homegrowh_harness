import type { ProviderName } from "@homegrowh/provider";

import type {
  HistoryActiveConversation,
  HistoryConversationRecord,
  HistoryRepository,
  HistoryRow,
  HistorySelectedModel,
  ReasoningHistoryPolicy,
} from "./history_contract";
import type {
  AddCustomModelInput,
  ModelRegistryItem,
  ModelRegistrySnapshot,
} from "../picker_model/model_registry_contract";

export const CHAT_THREAD_ADD_REPOSITORY_CHANNEL =
  "homegrowh-chat-thread:history:add-repository";
export const CHAT_THREAD_LIST_REPOSITORIES_CHANNEL =
  "homegrowh-chat-thread:history:list-repositories";
export const CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL =
  "homegrowh-chat-thread:history:list-conversations";
export const CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL =
  "homegrowh-chat-thread:history:get-selected-model";
export const CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL =
  "homegrowh-chat-thread:history:set-selected-model";
export const CHAT_THREAD_ADD_CONVERSATION_CHANNEL =
  "homegrowh-chat-thread:history:add-conversation";
export const CHAT_THREAD_DELETE_CONVERSATION_CHANNEL =
  "homegrowh-chat-thread:history:delete-conversation";
export const CHAT_THREAD_READ_CONVERSATION_CHANNEL =
  "homegrowh-chat-thread:history:read-conversation";
export const CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL =
  "homegrowh-chat-thread:history:read-conversation-row";
export const CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL =
  "homegrowh-chat-thread:history:set-active-conversation";
export const CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL =
  "homegrowh-chat-thread:history:get-active-conversation";
export const CHAT_THREAD_CONVERSATION_ROW_EVENT =
  "homegrowh-chat-thread:conversation:row";
export const CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL =
  "homegrowh-chat-thread:provider:chat";
export const CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL =
  "homegrowh-chat-thread:provider:cancel";
export const CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL =
  "homegrowh-chat-thread:provider:list-active-requests";
export const CHAT_THREAD_REQUEST_STATE_EVENT =
  "homegrowh-chat-thread:provider:request-state";
export const CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT =
  "homegrowh-chat-thread:provider:error-notice";
export const CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT =
  "homegrowh-chat-thread:context:usage-updated";
export const CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL =
  "homegrowh-chat-thread:context:read-usage";
export const CHAT_THREAD_DESKTOP_BRIDGE_KEY = "__homegrowhChatThreadDesktop";

export interface ConversationRowAppendedEvent {
  repositoryPath: string;
  conversationId: string;
  requestId?: string;
  rowId: number;
  API_sessions: string;
}

export interface SendChatRequestInput {
  provider: ProviderName;
  model: string;
  prompt: string;
  stream?: boolean;
  reasoningHistory?: ReasoningHistoryPolicy;
}

export interface SendChatRequestResult {
  requestId: string;
}

export interface ConversationRequestSnapshot {
  requestId: string;
  repositoryPath: string;
  conversationId: string;
  reasoning: string;
  answer: string;
}

export type ConversationRequestStateEvent =
  | {
      active: true;
      request: ConversationRequestSnapshot;
    }
  | {
      active: false;
      requestId: string;
      repositoryPath: string;
      conversationId: string;
    };

export interface ProviderErrorNoticeEvent {
  repositoryPath: string;
  conversationId: string;
  requestId: string;
  message: string;
}

export interface ConversationContextUsageUpdatedEvent {
  repositoryPath: string;
  conversationId: string;
  API_sessions: string;
}

export interface ChatThreadDesktopBridge {
  listModelRegistry(): Promise<ModelRegistrySnapshot>;
  addCustomModel(input: AddCustomModelInput): Promise<ModelRegistryItem>;
  getSelectedModel(): Promise<HistorySelectedModel | null>;
  setSelectedModel(selection: HistorySelectedModel): Promise<HistorySelectedModel>;
  listConversations(): Promise<HistoryConversationRecord[]>;
  listRepositories(): Promise<HistoryRepository[]>;
  addRepository(): Promise<HistoryRepository | null>;
  addConversation(repositoryPath: string): Promise<HistoryConversationRecord>;
  deleteConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<boolean>;
  readConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<HistoryRow[]>;
  readConversationRow(
    repositoryPath: string,
    conversationId: string,
    rowId: number,
  ): Promise<HistoryRow | null>;
  readConversationContextUsage(
    repositoryPath: string,
    conversationId: string,
  ): Promise<import("./history_contract").HistoryContextUsage | null>;
  setActiveConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<void>;
  getActiveConversation(): Promise<HistoryActiveConversation | null>;
  sendChatRequest(input: SendChatRequestInput): Promise<SendChatRequestResult>;
  cancelChatRequest(requestId: string): Promise<void>;
  listActiveChatRequests(): Promise<ConversationRequestSnapshot[]>;
  onChatRequestState(
    listener: (event: ConversationRequestStateEvent) => void,
  ): () => void;
  onProviderErrorNotice(
    listener: (event: ProviderErrorNoticeEvent) => void,
  ): () => void;
  onConversationContextUsageUpdated(
    listener: (event: ConversationContextUsageUpdatedEvent) => void,
  ): () => void;
  onConversationRow(
    listener: (event: ConversationRowAppendedEvent) => void,
  ): () => void;
}
