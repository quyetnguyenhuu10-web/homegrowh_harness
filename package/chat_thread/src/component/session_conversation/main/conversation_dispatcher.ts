import { randomUUID } from "node:crypto";

import {
  pushConversationRow,
  type AddRepositoryOptions,
  type PushRowInput,
} from "../../history_conversation/main";
import type { SessionModelEvent } from "./model_events";
import type {
  ConversationRequestSnapshot,
  ConversationRequestStateEvent,
  ConversationRowAppendedEvent,
} from "../../history_conversation/main";

export interface ConversationRequestContext {
  repositoryPath: string;
  conversationId: string;
}

export interface ConversationDispatcherOptions extends AddRepositoryOptions {
  onPersistedRow?: (event: ConversationRowAppendedEvent) => void;
  onRequestState?: (event: ConversationRequestStateEvent) => void;
  onError?: (error: unknown, event: SessionModelEvent) => void;
}

export interface ConversationDispatcher {
  /** Sinh requestId mới và bind vào đúng repo/conversation. */
  createRequest(context: ConversationRequestContext): string;
  /** Bind requestId có sẵn, dùng khi request được sinh ở tầng khác. */
  registerRequest(requestId: string, context: ConversationRequestContext): void;
  /** Nhận event của một model turn và route DB/UI theo đúng conversation. */
  handleModelEvent(event: SessionModelEvent): Promise<void>;
  /** Persist một row do app tạo, vẫn đi qua queue + callback DB-first chung. */
  persistRequestRow(requestId: string, row: PushRowInput): Promise<void>;
  /** Hủy context/request đang giữ trong RAM. */
  releaseRequest(requestId: string): void;
  /** Snapshot toàn bộ request đang sống; dùng để rehydrate renderer sau reload. */
  listActiveRequests(): ConversationRequestSnapshot[];
  dispose(): void;
}

function queueKey(context: ConversationRequestContext): string {
  return `${context.repositoryPath}\u0000${context.conversationId}`;
}

export function createConversationDispatcher(
  options: ConversationDispatcherOptions = {},
): ConversationDispatcher {
  const requestContexts = new Map<string, ConversationRequestContext>();
  const streamBuffers = new Map<string, string>();
  const reasoningBuffers = new Map<string, string>();
  const conversationTails = new Map<string, Promise<void>>();

  const enqueue = (
    context: ConversationRequestContext,
    work: () => void | Promise<void>,
  ): Promise<void> => {
    const key = queueKey(context);
    const previous = conversationTails.get(key) ?? Promise.resolve();
    const next = previous.catch(() => {}).then(work);
    conversationTails.set(key, next);

    void next.finally(() => {
      if (conversationTails.get(key) === next) {
        conversationTails.delete(key);
      }
    });

    return next;
  };

  const contextFor = (requestId: string): ConversationRequestContext => {
    const context = requestContexts.get(requestId);
    if (!context) {
      throw new Error(`Không có conversation context cho requestId "${requestId}".`);
    }
    return context;
  };

  const persist = (
    requestId: string,
    context: ConversationRequestContext,
    row: Parameters<typeof pushConversationRow>[2],
  ): void => {
    const persisted = pushConversationRow(
      context.repositoryPath,
      context.conversationId,
      {
        ...row,
        request_id: requestId,
      },
      { projectsDir: options.projectsDir },
    );

    options.onPersistedRow?.({
      repositoryPath: context.repositoryPath,
      conversationId: context.conversationId,
      requestId,
      rowId: persisted.id,
      API_sessions: persisted.API_sessions ?? row.API_sessions,
    });
  };

  const releaseRequest = (requestId: string): void => {
    const context = requestContexts.get(requestId);
    requestContexts.delete(requestId);
    streamBuffers.delete(requestId);
    reasoningBuffers.delete(requestId);
    if (context) {
      options.onRequestState?.({
        active: false,
        requestId,
        repositoryPath: context.repositoryPath,
        conversationId: context.conversationId,
      });
    }
  };

  const snapshotFor = (
    requestId: string,
    context: ConversationRequestContext,
  ): ConversationRequestSnapshot => ({
    requestId,
    repositoryPath: context.repositoryPath,
    conversationId: context.conversationId,
    reasoning: reasoningBuffers.get(requestId) ?? "",
    answer: streamBuffers.get(requestId) ?? "",
  });

  const dispatcher: ConversationDispatcher = {
    createRequest(context): string {
      const requestId = randomUUID();
      requestContexts.set(requestId, { ...context });
      options.onRequestState?.({
        active: true,
        request: snapshotFor(requestId, context),
      });
      return requestId;
    },

    registerRequest(requestId, context): void {
      const id = requestId.trim();
      if (!id) throw new Error("registerRequest() cần requestId.");
      requestContexts.set(id, { ...context });
      options.onRequestState?.({
        active: true,
        request: snapshotFor(id, context),
      });
    },

    async handleModelEvent(event): Promise<void> {
      try {
        const context = contextFor(event.requestId);

        if (event.type === "assistant.delta") {
          await enqueue(context, () => {
            persist(event.requestId, context, {
              type: "reply",
              role: "assistant",
              delta: event.delta,
              API_sessions: event.apiSession,
              event_index: event.eventIndex,
            });
          });
          const next = `${streamBuffers.get(event.requestId) ?? ""}${event.delta}`;
          streamBuffers.set(event.requestId, next);
          return;
        }

        if (event.type === "assistant.reasoning.delta") {
          await enqueue(context, () => {
            persist(event.requestId, context, {
              type: "reasoning",
              role: "assistant",
              delta: event.delta,
              API_sessions: event.apiSession,
              event_index: event.eventIndex,
            });
          });
          const next = `${reasoningBuffers.get(event.requestId) ?? ""}${event.delta}`;
          reasoningBuffers.set(event.requestId, next);
          return;
        }

        if (event.type === "assistant.tool_call.delta") {
          await enqueue(context, () => {
            persist(event.requestId, context, {
              type: "toolcall",
              role: "assistant",
              delta: event.delta,
              API_sessions: event.apiSession,
              event_index: event.eventIndex,
            });
          });
          return;
        }

        if (event.type === "assistant.tool_calls") {
          streamBuffers.set(event.requestId, "");
          reasoningBuffers.set(event.requestId, "");
          return;
        }

        if (event.type === "assistant.done") {
          releaseRequest(event.requestId);
          return;
        }

        if (event.type === "assistant.cancelled") {
          releaseRequest(event.requestId);
          return;
        }

      } catch (error) {
        options.onError?.(error, event);
        throw error;
      }
    },

    persistRequestRow(requestId, row): Promise<void> {
      const context = contextFor(requestId);
      return enqueue(context, () => {
        persist(requestId, context, row);
      });
    },

    releaseRequest,

    listActiveRequests(): ConversationRequestSnapshot[] {
      return Array.from(requestContexts, ([requestId, context]) =>
        snapshotFor(requestId, context),
      );
    },

    dispose(): void {
      requestContexts.clear();
      streamBuffers.clear();
      reasoningBuffers.clear();
      conversationTails.clear();
    },
  };

  return dispatcher;
}
