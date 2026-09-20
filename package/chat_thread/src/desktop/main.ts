import {
  BrowserWindow,
  dialog,
  ipcMain,
  type OpenDialogOptions,
} from "electron";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import {
  addConversation,
  addRepository,
  deleteConversation,
  getActiveConversation,
  getSelectedModel,
  listConversations,
  listRepositories,
  readConversationContextUsage,
  readConversation,
  readConversationRow,
  setSelectedModel,
  setActiveConversation,
  type AddRepositoryOptions,
  type HistoryActiveConversation,
  type HistoryRepository,
  type HistorySelectedModel,
  CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
  CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
  CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_CONVERSATION_ROW_EVENT,
  CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT,
  CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
  CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
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
  type SendChatRequestInput,
  type SendChatRequestResult,
} from "../component/history_conversation/main";
import {
  getSessionConversationRuntime,
  startSessionConversationRuntime,
  stopSessionConversationRuntime,
  type SessionConversationRuntimeOptions,
} from "../component/session_conversation/main";
import {
  addCustomModel,
  listModelRegistry,
  CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
  CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
  type AddCustomModelInput,
  type ModelRegistryItem,
  type ModelRegistrySnapshot,
} from "../component/picker_model/main";

export interface RegisterChatThreadDesktopOptions extends AddRepositoryOptions {
  dialogTitle?: string;
  dialogButtonLabel?: string;
}

export interface ChatThreadDesktopInstallation {
  /** Preload đã build sẵn của plugin để gắn vào BrowserWindow. */
  preloadPath: string;
  /** Gỡ IPC handler của plugin. */
  dispose(): void;
}

let registered = false;

function broadcast(channel: string, payload: unknown): void {
  for (const window of BrowserWindow.getAllWindows()) {
    if (!window.isDestroyed()) {
      window.webContents.send(channel, payload);
    }
  }
}

/**
 * Đăng ký phần desktop của chat_thread đúng một lần trong Electron main process.
 * Renderer không cần được truyền repositoryApi hay biết history_conversation.
 */
export function registerChatThreadDesktop(
  options: RegisterChatThreadDesktopOptions = {},
): () => void {
  if (registered) return () => {};
  registered = true;

  const runtimeOptions: SessionConversationRuntimeOptions = {
    projectsDir: options.projectsDir,
    onPersistedRow: (event) => {
      broadcast(CHAT_THREAD_CONVERSATION_ROW_EVENT, event);
    },
    onRequestState: (event) => {
      broadcast(CHAT_THREAD_REQUEST_STATE_EVENT, event);
    },
    onContextUsageUpdated: (event) => {
      broadcast(CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT, event);
    },
    onProviderError: (event) => {
      broadcast(CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT, event);
    },
    onError: (error, event) => {
      console.error("[chat_thread] session conversation failed", event, error);
    },
  };
  const startSessionRuntime = () =>
    startSessionConversationRuntime(runtimeOptions);

  void startSessionRuntime().catch((error) => {
    console.error("[chat_thread] session runtime start failed", error);
  });

  ipcMain.handle(
    CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
    (): ModelRegistrySnapshot =>
      listModelRegistry({
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
    (_event, input: AddCustomModelInput): ModelRegistryItem =>
      addCustomModel(input, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
    (): HistorySelectedModel | null =>
      getSelectedModel({
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
    (_event, selection: HistorySelectedModel): HistorySelectedModel =>
      setSelectedModel(selection, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
    async () => {
      const sessionRuntime = await startSessionRuntime();
      return sessionRuntime.listActiveRequests();
    },
  );

  ipcMain.handle(
    CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL,
    () =>
      listConversations({
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_LIST_REPOSITORIES_CHANNEL,
    (): HistoryRepository[] =>
      listRepositories({
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string) =>
      addConversation(repositoryPath, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string) =>
      deleteConversation(repositoryPath, conversationId, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_READ_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string) =>
      readConversation(repositoryPath, conversationId, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
    (_event, repositoryPath: string, conversationId: string, rowId: number) =>
      readConversationRow(repositoryPath, conversationId, rowId, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
    (_event, repositoryPath: string, conversationId: string) =>
      readConversationContextUsage(repositoryPath, conversationId, {
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
    (): HistoryActiveConversation | null =>
      getActiveConversation({
        projectsDir: options.projectsDir,
      }),
  );

  ipcMain.handle(
    CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string): void => {
      setActiveConversation(repositoryPath, conversationId, {
        projectsDir: options.projectsDir,
      });
    },
  );

  ipcMain.handle(
    CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
    async (event): Promise<HistoryRepository | null> => {
      const browserWindow = BrowserWindow.fromWebContents(event.sender);
      const dialogOptions: OpenDialogOptions = {
        title: options.dialogTitle ?? "Add repository",
        buttonLabel: options.dialogButtonLabel ?? "Add repository",
        properties: ["openDirectory"],
      };

      const result = browserWindow
        ? await dialog.showOpenDialog(browserWindow, dialogOptions)
        : await dialog.showOpenDialog(dialogOptions);

      if (result.canceled || result.filePaths.length === 0) return null;

      return addRepository(result.filePaths[0], {
        projectsDir: options.projectsDir,
      });
    },
  );

  ipcMain.handle(
    CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
    async (_event, input: SendChatRequestInput): Promise<SendChatRequestResult> => {
      const sessionRuntime = await startSessionRuntime();
      return {
        requestId: await sessionRuntime.sendChatRequest(input),
      };
    },
  );

  ipcMain.handle(
    CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
    async (_event, requestId: string): Promise<void> => {
      const sessionRuntime = await startSessionRuntime();
      sessionRuntime.cancelChatRequest(requestId);
    },
  );

  return () => {
    if (!registered) return;
    registered = false;
    ipcMain.removeHandler(CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_READ_CONVERSATION_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_DELETE_CONVERSATION_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_ADD_CONVERSATION_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_LIST_REPOSITORIES_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_ADD_REPOSITORY_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL);
    ipcMain.removeHandler(CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL);
    void stopSessionConversationRuntime().catch((error) => {
      console.error("[chat_thread] session runtime stop failed", error);
    });
  };
}

export {
  getSessionConversationRuntime,
  startSessionConversationRuntime,
  stopSessionConversationRuntime,
} from "../component/session_conversation/main";

export function getChatThreadPreloadPath(): string {
  return join(dirname(fileURLToPath(import.meta.url)), "preload.cjs");
}

/**
 * Entry desktop cấp plugin: host gọi một lần ở main process để nhận toàn bộ
 * phần desktop cần thiết của chat_thread.
 */
export function installChatThreadDesktop(
  options: RegisterChatThreadDesktopOptions = {},
): ChatThreadDesktopInstallation {
  return {
    preloadPath: getChatThreadPreloadPath(),
    dispose: registerChatThreadDesktop(options),
  };
}
