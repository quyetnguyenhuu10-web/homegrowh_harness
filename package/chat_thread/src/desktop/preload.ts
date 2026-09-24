import { contextBridge, ipcRenderer } from "electron";

import {
  CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
  CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
  CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_DESKTOP_BRIDGE_KEY,
  CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
  CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
  CHAT_THREAD_CONVERSATION_ROW_EVENT,
  CHAT_THREAD_COMPACTION_DEBUG_EVENT,
  CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT,
  CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
  CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL,
  CHAT_THREAD_LIST_REPOSITORIES_CHANNEL,
  CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT,
  CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
  CHAT_THREAD_READ_CONVERSATION_CHANNEL,
  CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
  CHAT_THREAD_REQUEST_STATE_EVENT,
  CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
  CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
  type ConversationSessionSnapshot,
  type ConversationSessionStateEvent,
  type ConversationContextUsageUpdatedEvent,
  type CompactionDebugEvent,
  type ConversationRowAppendedEvent,
  type ProviderErrorNoticeEvent,
  type ChatThreadDesktopBridge,
  type SendChatRequestInput,
  type SendChatRequestResult,
} from "../component/history_conversation/desktop_contract";
import type {
  HistoryActiveConversation,
  HistoryConversationRecord,
  HistoryContextUsage,
  HistoryRepository,
  HistoryRow,
  HistorySelectedModel,
} from "../component/history_conversation/history_contract";
import {
  CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
  CHAT_THREAD_DELETE_CUSTOM_MODEL_CHANNEL,
  CHAT_THREAD_GET_CUSTOM_MODEL_CHANNEL,
  CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
  CHAT_THREAD_UPDATE_CUSTOM_MODEL_CHANNEL,
  type AddCustomModelInput,
  type CustomModelEditableConfig,
  type DeleteCustomModelInput,
  type GetCustomModelInput,
  type ModelRegistryItem,
  type ModelRegistrySnapshot,
  type UpdateCustomModelInput,
} from "../component/picker_model/model_registry_contract";

const bridge: ChatThreadDesktopBridge = {
  listModelRegistry(): Promise<ModelRegistrySnapshot> {
    return ipcRenderer.invoke(
      CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
    ) as Promise<ModelRegistrySnapshot>;
  },

  addCustomModel(input: AddCustomModelInput): Promise<ModelRegistryItem> {
    return ipcRenderer.invoke(
      CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
      input,
    ) as Promise<ModelRegistryItem>;
  },

  deleteCustomModel(input: DeleteCustomModelInput): Promise<boolean> {
    return ipcRenderer.invoke(
      CHAT_THREAD_DELETE_CUSTOM_MODEL_CHANNEL,
      input,
    ) as Promise<boolean>;
  },

  getCustomModel(input: GetCustomModelInput): Promise<CustomModelEditableConfig> {
    return ipcRenderer.invoke(
      CHAT_THREAD_GET_CUSTOM_MODEL_CHANNEL,
      input,
    ) as Promise<CustomModelEditableConfig>;
  },

  updateCustomModel(input: UpdateCustomModelInput): Promise<ModelRegistryItem> {
    return ipcRenderer.invoke(
      CHAT_THREAD_UPDATE_CUSTOM_MODEL_CHANNEL,
      input,
    ) as Promise<ModelRegistryItem>;
  },

  getSelectedModel(): Promise<HistorySelectedModel | null> {
    return ipcRenderer.invoke(
      CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
    ) as Promise<HistorySelectedModel | null>;
  },

  setSelectedModel(
    selection: HistorySelectedModel,
  ): Promise<HistorySelectedModel> {
    return ipcRenderer.invoke(
      CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
      selection,
    ) as Promise<HistorySelectedModel>;
  },

  listConversations(): Promise<HistoryConversationRecord[]> {
    return ipcRenderer.invoke(CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL) as Promise<
      HistoryConversationRecord[]
    >;
  },

  listRepositories(): Promise<HistoryRepository[]> {
    return ipcRenderer.invoke(CHAT_THREAD_LIST_REPOSITORIES_CHANNEL) as Promise<
      HistoryRepository[]
    >;
  },

  addRepository(): Promise<HistoryRepository | null> {
    return ipcRenderer.invoke(CHAT_THREAD_ADD_REPOSITORY_CHANNEL) as Promise<
      HistoryRepository | null
    >;
  },

  addConversation(repositoryPath: string): Promise<HistoryConversationRecord> {
    return ipcRenderer.invoke(
      CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
      repositoryPath,
    ) as Promise<HistoryConversationRecord>;
  },

  deleteConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<boolean> {
    return ipcRenderer.invoke(
      CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
      repositoryPath,
      conversationId,
    ) as Promise<boolean>;
  },

  readConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<HistoryRow[]> {
    return ipcRenderer.invoke(
      CHAT_THREAD_READ_CONVERSATION_CHANNEL,
      repositoryPath,
      conversationId,
    ) as Promise<HistoryRow[]>;
  },

  readConversationRow(
    repositoryPath: string,
    conversationId: string,
    rowPosition: number,
  ): Promise<HistoryRow | null> {
    return ipcRenderer.invoke(
      CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
      repositoryPath,
      conversationId,
      rowPosition,
    ) as Promise<HistoryRow | null>;
  },

  readConversationContextUsage(
    repositoryPath: string,
    conversationId: string,
  ): Promise<HistoryContextUsage | null> {
    return ipcRenderer.invoke(
      CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
      repositoryPath,
      conversationId,
    ) as Promise<HistoryContextUsage | null>;
  },

  setActiveConversation(
    repositoryPath: string,
    conversationId: string,
  ): Promise<void> {
    return ipcRenderer.invoke(
      CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
      repositoryPath,
      conversationId,
    ) as Promise<void>;
  },

  getActiveConversation(): Promise<HistoryActiveConversation | null> {
    return ipcRenderer.invoke(
      CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
    ) as Promise<HistoryActiveConversation | null>;
  },

  sendChatRequest(input: SendChatRequestInput): Promise<SendChatRequestResult> {
    return ipcRenderer.invoke(
      CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
      input,
    ) as Promise<SendChatRequestResult>;
  },

  cancelChatSession(sessionId: string): Promise<void> {
    return ipcRenderer.invoke(
      CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
      sessionId,
    ) as Promise<void>;
  },

  listActiveChatSessions(): Promise<ConversationSessionSnapshot[]> {
    return ipcRenderer.invoke(
      CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
    ) as Promise<ConversationSessionSnapshot[]>;
  },

  onChatSessionState(
    listener: (event: ConversationSessionStateEvent) => void,
  ): () => void {
    const handler = (
      _event: Electron.IpcRendererEvent,
      payload: ConversationSessionStateEvent,
    ) => {
      listener(payload);
    };
    ipcRenderer.on(CHAT_THREAD_REQUEST_STATE_EVENT, handler);
    return () =>
      ipcRenderer.removeListener(CHAT_THREAD_REQUEST_STATE_EVENT, handler);
  },

  onProviderErrorNotice(
    listener: (event: ProviderErrorNoticeEvent) => void,
  ): () => void {
    const handler = (
      _event: Electron.IpcRendererEvent,
      payload: ProviderErrorNoticeEvent,
    ) => {
      listener(payload);
    };
    ipcRenderer.on(CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT, handler);
    return () =>
      ipcRenderer.removeListener(CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT, handler);
  },

  onConversationContextUsageUpdated(
    listener: (event: ConversationContextUsageUpdatedEvent) => void,
  ): () => void {
    const handler = (
      _event: Electron.IpcRendererEvent,
      payload: ConversationContextUsageUpdatedEvent,
    ) => {
      listener(payload);
    };
    ipcRenderer.on(CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT, handler);
    return () =>
      ipcRenderer.removeListener(CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT, handler);
  },

  onCompactionDebug(
    listener: (event: CompactionDebugEvent) => void,
  ): () => void {
    const handler = (
      _event: Electron.IpcRendererEvent,
      payload: CompactionDebugEvent,
    ) => {
      listener(payload);
    };
    ipcRenderer.on(CHAT_THREAD_COMPACTION_DEBUG_EVENT, handler);
    return () => ipcRenderer.removeListener(CHAT_THREAD_COMPACTION_DEBUG_EVENT, handler);
  },

  onConversationRow(
    listener: (event: ConversationRowAppendedEvent) => void,
  ): () => void {
    const handler = (_event: Electron.IpcRendererEvent, payload: ConversationRowAppendedEvent) => {
      listener(payload);
    };
    ipcRenderer.on(CHAT_THREAD_CONVERSATION_ROW_EVENT, handler);
    return () => ipcRenderer.removeListener(CHAT_THREAD_CONVERSATION_ROW_EVENT, handler);
  },
};

contextBridge.exposeInMainWorld(CHAT_THREAD_DESKTOP_BRIDGE_KEY, bridge);
