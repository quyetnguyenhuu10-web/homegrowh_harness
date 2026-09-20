// API thống nhất của history_conversation nội bộ chat_thread.
export {
  openHistory,
  deleteHistory,
  historyExists,
  listHistories,
  defaultBaseDir,
} from "./store";
export type { OpenHistoryOptions } from "./store";
export {
  addConversation,
  addRepository,
  defaultProjectsDir,
  deleteConversation,
  getActiveConversation,
  getSelectedModel,
  listConversations,
  listRepositories,
  pushConversationRow,
  readConversation,
  readConversationRow,
  setActiveConversation,
  setSelectedModel,
} from "./repository";
export type { AddRepositoryOptions } from "./repository";
export type { HistoryHandle, PushRowInput } from "./types";
export {
  countConversationContextCharacters,
  readConversationContextUsage,
  writeConversationContextUsage,
} from "./context_usage";
export type { WriteConversationContextUsageInput } from "./context_usage";
export {
  DEFAULT_REASONING_HISTORY_POLICY,
  normalizeReasoningHistoryPolicy,
  selectReasoningRequestIds,
} from "./reasoning_history";
export { NORMAL_CONVERSATION_SCOPE } from "../scope";
export {
  CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
  CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
  CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_CONVERSATION_ROW_EVENT,
  CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT,
  CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
  CHAT_THREAD_DESKTOP_BRIDGE_KEY,
  CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
  CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
  CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL,
  CHAT_THREAD_LIST_REPOSITORIES_CHANNEL,
  CHAT_THREAD_READ_CONVERSATION_CHANNEL,
  CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
  CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT,
  CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
  CHAT_THREAD_REQUEST_STATE_EVENT,
  CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
} from "../desktop_contract";
export type {
  ChatThreadDesktopBridge,
  ConversationContextUsageUpdatedEvent,
  ConversationRowAppendedEvent,
  ConversationRequestSnapshot,
  ConversationRequestStateEvent,
  ProviderErrorNoticeEvent,
  SendChatRequestInput,
  SendChatRequestResult,
} from "../desktop_contract";
export type {
  HistoryActiveConversation,
  HistoryConversationRecord,
  HistoryContextUsage,
  HistoryRepository,
  HistoryRow,
  HistorySelectedModel,
  ReasoningHistoryPolicy,
} from "../history_contract";
