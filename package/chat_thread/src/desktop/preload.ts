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
  type ConversationRequestSnapshot,
  type ConversationRequestStateEvent,
  type ConversationContextUsageUpdatedEvent,
  type ConversationRowAppendedEvent,
  type ProviderErrorNoticeEvent,
  type ChatThreadDesktopBridge,
  type SendChatRequestInput,
  type SendChatRequestResult,
  type HistoryActiveConversation,
  type HistoryConversationRecord,
  type HistoryContextUsage,
  type HistoryRepository,
  type HistoryRow,
  type HistorySelectedModel,
} from "../component/history_conversation/main";
import {
  CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
  CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
  type AddCustomModelInput,
  type ModelRegistryItem,
  type ModelRegistrySnapshot,
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
    rowId: number,
  ): Promise<HistoryRow | null> {
    return ipcRenderer.invoke(
      CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
      repositoryPath,
      conversationId,
      rowId,
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

  cancelChatRequest(requestId: string): Promise<void> {
    return ipcRenderer.invoke(
      CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
      requestId,
    ) as Promise<void>;
  },

  listActiveChatRequests(): Promise<ConversationRequestSnapshot[]> {
    return ipcRenderer.invoke(
      CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
    ) as Promise<ConversationRequestSnapshot[]>;
  },

  onChatRequestState(
    listener: (event: ConversationRequestStateEvent) => void,
  ): () => void {
    const handler = (
      _event: Electron.IpcRendererEvent,
      payload: ConversationRequestStateEvent,
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
