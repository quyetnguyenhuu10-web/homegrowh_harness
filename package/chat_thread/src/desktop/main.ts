import {
  BrowserWindow,
  dialog,
  ipcMain,
  type OpenDialogOptions,
} from "electron";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import * as database from "@hh/database";
import type {
  ActiveConversation,
  ContextUsageRecord,
  ConversationRecord,
  ConversationRef,
  ConversationScope,
  RepositoryRecord,
} from "@hh/database";
import { model as providerModel } from "@hh/provider";
import type { ProviderName } from "@hh/provider";
import * as session from "@hh/session";
import type { SessionEvent, SessionSnapshot } from "@hh/session";

import {
  CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
  CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
  CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_CONVERSATION_ROW_EVENT,
  CHAT_THREAD_COMPACTION_DEBUG_EVENT,
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
  CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
  CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
  CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
  type ConversationSessionSnapshot,
  type SendChatRequestInput,
  type SendChatRequestResult,
} from "../component/history_conversation/desktop_contract";
import type {
  HistoryActiveConversation,
  HistoryContextUsage,
  HistoryConversationRecord,
  HistoryRepository,
  HistorySelectedModel,
} from "../component/history_conversation/history_contract";
import { NORMAL_CONVERSATION_SCOPE } from "../component/history_conversation/scope";
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

export interface RegisterChatThreadDesktopOptions {
  dialogTitle?: string;
  dialogButtonLabel?: string;
}

export interface ChatThreadDesktopInstallation {
  preloadPath: string;
  dispose(): void;
}

let registered = false;

function scopeFromRepositoryPath(repositoryPath: string): ConversationScope {
  return repositoryPath === NORMAL_CONVERSATION_SCOPE
    ? { kind: "normal" }
    : { kind: "repository", repositoryPath };
}

function repositoryPathFromScope(scope: ConversationScope): string {
  return scope.kind === "normal"
    ? NORMAL_CONVERSATION_SCOPE
    : scope.repositoryPath;
}

function conversationRef(
  repositoryPath: string,
  conversationId: string,
): ConversationRef {
  return {
    scope: scopeFromRepositoryPath(repositoryPath),
    conversationId,
  };
}

function toHistoryConversation(
  record: ConversationRecord,
): HistoryConversationRecord {
  return {
    id: record.id,
    createdAt: record.createdAt,
    active: record.active,
  };
}

function toHistoryRepository(record: RepositoryRecord): HistoryRepository {
  return {
    name: record.name,
    repositoryPath: record.repositoryPath,
    conversations: database.conversation
      .list({ kind: "repository", repositoryPath: record.repositoryPath })
      .map(toHistoryConversation),
  };
}

function toHistoryActive(
  active: ActiveConversation | null,
): HistoryActiveConversation | null {
  if (!active) return null;
  return {
    repositoryPath: repositoryPathFromScope(active.scope),
    conversationId: active.conversationId,
  };
}

function toHistoryUsage(
  usage: ContextUsageRecord | null,
): HistoryContextUsage | null {
  if (!usage) return null;
  return {
    ...usage,
    provider: usage.provider as ProviderName,
    reasoningHistory: usage.reasoningHistory as HistoryContextUsage["reasoningHistory"],
  };
}

function broadcast(channel: string, payload: unknown): void {
  for (const window of BrowserWindow.getAllWindows()) {
    if (!window.isDestroyed()) window.webContents.send(channel, payload);
  }
}

function sessionSnapshotForRenderer(
  sessionSnapshot: SessionSnapshot,
): ConversationSessionSnapshot {
  return {
    sessionId: sessionSnapshot.sessionId,
    repositoryPath: repositoryPathFromScope(sessionSnapshot.target.scope),
    conversationId: sessionSnapshot.target.conversationId,
  };
}

function forwardSessionEvent(event: SessionEvent): void {
  if (event.type === "row") {
    broadcast(CHAT_THREAD_CONVERSATION_ROW_EVENT, {
      repositoryPath: repositoryPathFromScope(event.target.scope),
      conversationId: event.target.conversationId,
      ...(event.sessionId ? { sessionId: event.sessionId } : {}),
      ...(event.requestId ? { requestId: event.requestId } : {}),
      rowPosition: event.rowPosition,
      row: event.row,
    });
    return;
  }

  if (event.type === "session") {
    broadcast(
      CHAT_THREAD_REQUEST_STATE_EVENT,
      event.active
        ? {
            active: true,
            session: sessionSnapshotForRenderer(event.session),
          }
        : {
            active: false,
            sessionId: event.sessionId,
            repositoryPath: repositoryPathFromScope(event.target.scope),
            conversationId: event.target.conversationId,
          },
    );
    return;
  }

  if (event.type === "contextUsage") {
    broadcast(CHAT_THREAD_CONTEXT_USAGE_UPDATED_EVENT, {
      repositoryPath: repositoryPathFromScope(event.target.scope),
      conversationId: event.target.conversationId,
      sessionId: event.sessionId,
      requestId: event.requestId,
    });
    return;
  }

  if (event.type === "compactionDebug") {
    broadcast(CHAT_THREAD_COMPACTION_DEBUG_EVENT, {
      repositoryPath: repositoryPathFromScope(event.target.scope),
      conversationId: event.target.conversationId,
      sessionId: event.sessionId,
      requestId: event.requestId,
      phase: event.phase,
      ...(event.delta !== undefined ? { delta: event.delta } : {}),
    });
    return;
  }

  broadcast(CHAT_THREAD_PROVIDER_ERROR_NOTICE_EVENT, {
    repositoryPath: repositoryPathFromScope(event.target.scope),
    conversationId: event.target.conversationId,
    sessionId: event.sessionId,
    message: event.message,
  });
}

export function registerChatThreadDesktop(
  options: RegisterChatThreadDesktopOptions = {},
): () => void {
  if (registered) return () => {};
  registered = true;
  const unsubscribeSession = session.subscribe(forwardSessionEvent);

  ipcMain.handle(
    CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
    (): ModelRegistrySnapshot => providerModel.list(),
  );
  ipcMain.handle(
    CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
    (_event, input: AddCustomModelInput): ModelRegistryItem =>
      providerModel.addCustom({
        apiKey: input.apiKey,
        endpoint: input.endpoint,
        model: input.model,
        contextWindowTokens: input.maxContextWindowTokens,
      }),
  );
  ipcMain.handle(
    CHAT_THREAD_DELETE_CUSTOM_MODEL_CHANNEL,
    (_event, input: DeleteCustomModelInput): boolean =>
      providerModel.deleteCustom(input.model),
  );
  ipcMain.handle(
    CHAT_THREAD_GET_CUSTOM_MODEL_CHANNEL,
    (_event, input: GetCustomModelInput): CustomModelEditableConfig =>
      providerModel.getCustom(input.model),
  );
  ipcMain.handle(
    CHAT_THREAD_UPDATE_CUSTOM_MODEL_CHANNEL,
    (_event, input: UpdateCustomModelInput): ModelRegistryItem =>
      providerModel.updateCustom({
        originalModel: input.originalModel,
        apiKey: input.apiKey,
        endpoint: input.endpoint,
        model: input.model,
        contextWindowTokens: input.maxContextWindowTokens,
      }),
  );
  ipcMain.handle(
    CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
    (): HistorySelectedModel | null => providerModel.getSelected(),
  );
  ipcMain.handle(
    CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
    (_event, selection: HistorySelectedModel): HistorySelectedModel =>
      providerModel.setSelected(selection),
  );

  ipcMain.handle(
    CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
    () => session.active().map(sessionSnapshotForRenderer),
  );
  ipcMain.handle(
    CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL,
    (): HistoryConversationRecord[] =>
      database.conversation.list({ kind: "normal" }).map(toHistoryConversation),
  );
  ipcMain.handle(
    CHAT_THREAD_LIST_REPOSITORIES_CHANNEL,
    (): HistoryRepository[] => database.repository.list().map(toHistoryRepository),
  );
  ipcMain.handle(
    CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string): HistoryConversationRecord =>
      toHistoryConversation(
        database.conversation.create(scopeFromRepositoryPath(repositoryPath)),
      ),
  );
  ipcMain.handle(
    CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string): boolean =>
      database.conversation.delete(
        conversationRef(repositoryPath, conversationId),
      ),
  );
  ipcMain.handle(
    CHAT_THREAD_READ_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string) =>
      database.conversation.read(conversationRef(repositoryPath, conversationId)),
  );
  ipcMain.handle(
    CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
    (_event, repositoryPath: string, conversationId: string, rowPosition: number) =>
      database.conversation.readRow(
        conversationRef(repositoryPath, conversationId),
        rowPosition,
      ),
  );
  ipcMain.handle(
    CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
    (_event, repositoryPath: string, conversationId: string) =>
      toHistoryUsage(
        database.contextUsage.get(
          conversationRef(repositoryPath, conversationId),
        ),
      ),
  );
  ipcMain.handle(
    CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
    (): HistoryActiveConversation | null =>
      toHistoryActive(database.conversation.getActive()),
  );
  ipcMain.handle(
    CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
    (_event, repositoryPath: string, conversationId: string): void => {
      database.conversation.setActive(
        conversationRef(repositoryPath, conversationId),
      );
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
      return toHistoryRepository(database.repository.add(result.filePaths[0]));
    },
  );

  ipcMain.handle(
    CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
    async (_event, input: SendChatRequestInput): Promise<SendChatRequestResult> => ({
      sessionId: await session.send(
        conversationRef(input.repositoryPath, input.conversationId),
        input.prompt,
        { reasoningHistory: input.reasoningHistory },
      ),
    }),
  );
  ipcMain.handle(
    CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
    (_event, sessionId: string): void => session.cancel(sessionId),
  );

  return () => {
    if (!registered) return;
    registered = false;
    for (const channel of [
      CHAT_THREAD_LIST_MODEL_REGISTRY_CHANNEL,
      CHAT_THREAD_ADD_CUSTOM_MODEL_CHANNEL,
      CHAT_THREAD_DELETE_CUSTOM_MODEL_CHANNEL,
      CHAT_THREAD_GET_CUSTOM_MODEL_CHANNEL,
      CHAT_THREAD_UPDATE_CUSTOM_MODEL_CHANNEL,
      CHAT_THREAD_READ_CONVERSATION_CHANNEL,
      CHAT_THREAD_READ_CONVERSATION_ROW_CHANNEL,
      CHAT_THREAD_READ_CONTEXT_USAGE_CHANNEL,
      CHAT_THREAD_GET_ACTIVE_CONVERSATION_CHANNEL,
      CHAT_THREAD_SET_ACTIVE_CONVERSATION_CHANNEL,
      CHAT_THREAD_GET_SELECTED_MODEL_CHANNEL,
      CHAT_THREAD_SET_SELECTED_MODEL_CHANNEL,
      CHAT_THREAD_DELETE_CONVERSATION_CHANNEL,
      CHAT_THREAD_ADD_CONVERSATION_CHANNEL,
      CHAT_THREAD_LIST_ACTIVE_REQUESTS_CHANNEL,
      CHAT_THREAD_LIST_CONVERSATIONS_CHANNEL,
      CHAT_THREAD_LIST_REPOSITORIES_CHANNEL,
      CHAT_THREAD_ADD_REPOSITORY_CHANNEL,
      CHAT_THREAD_SEND_CHAT_REQUEST_CHANNEL,
      CHAT_THREAD_CANCEL_CHAT_REQUEST_CHANNEL,
    ]) {
      ipcMain.removeHandler(channel);
    }
    unsubscribeSession();
    for (const activeSession of session.active()) session.cancel(activeSession.sessionId);
  };
}

export function getChatThreadPreloadPath(): string {
  return join(dirname(fileURLToPath(import.meta.url)), "preload.cjs");
}

export function installChatThreadDesktop(
  options: RegisterChatThreadDesktopOptions = {},
): ChatThreadDesktopInstallation {
  return {
    preloadPath: getChatThreadPreloadPath(),
    dispose: registerChatThreadDesktop(options),
  };
}
