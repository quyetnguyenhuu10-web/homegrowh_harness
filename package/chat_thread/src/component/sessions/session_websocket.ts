import {
  openSession,
  type ConversationSession,
} from "@homegrowh/session-conversation";

import type {
  SessionWebSocketController,
  SessionWebSocketFactory,
  SessionWebSocketListener,
  SessionWebSocketOptions,
  SessionWebSocketSendData,
  SessionWebSocketSnapshot,
  SessionWebSocketState,
} from "./types";

const WS_CONNECTING = 0;
const WS_OPEN = 1;
const WS_CLOSED = 3;

function defaultWebSocketFactory(
  url: string | URL,
  protocols?: string | string[],
): WebSocket {
  return protocols === undefined
    ? new WebSocket(url)
    : new WebSocket(url, protocols);
}

/**
 * Creates the lifecycle shell for one browser WebSocket-backed conversation.
 *
 * Current responsibility:
 * - create/own the ConversationSession from session_conversation;
 * - create/own one WebSocket transport at a time;
 * - expose native transport events and observable state;
 * - make explicit session.close()/controller.close() the final shutdown owner.
 *
 * Deliberately absent until the end-to-end contract exists:
 * - authentication;
 * - JSON/message envelope schema;
 * - user/assistant/tool routing;
 * - persistence/history hydration;
 * - retry/backoff policy;
 * - heartbeat/ack/sequence protocol.
 */
export function createSessionWebSocket(
  options: SessionWebSocketOptions,
): SessionWebSocketController {
  const createWebSocket: SessionWebSocketFactory =
    options.createWebSocket ?? defaultWebSocketFactory;

  const listeners = new Set<SessionWebSocketListener>();

  let conversationSession: ConversationSession | null = null;
  let transport: WebSocket | null = null;
  let transportGeneration = 0;
  let state: SessionWebSocketState = "idle";
  let closeReason: string | undefined;
  let lastError: unknown;
  let explicitShutdown = false;
  let pendingSocketCloseCode = 1000;
  let removeSessionCloseListener: (() => void) | null = null;

  const snapshot = (): SessionWebSocketSnapshot => ({
    state,
    sessionId: conversationSession?.id ?? null,
    sessionClosed: conversationSession?.closed ?? false,
    socketReadyState: transport?.readyState ?? null,
    ...(closeReason === undefined ? {} : { closeReason }),
    ...(lastError === undefined ? {} : { error: lastError }),
  });

  const publish = (): void => {
    const next = snapshot();

    try {
      options.onStateChange?.(next);
    } catch {
      // UI observers must not break session/socket lifecycle.
    }

    for (const listener of [...listeners]) {
      try {
        listener(next);
      } catch {
        // Same isolation rule as session_conversation close listeners.
      }
    }
  };

  const setState = (next: SessionWebSocketState): void => {
    state = next;
    publish();
  };

  const detachTransport = (expected: WebSocket): void => {
    if (transport === expected) {
      transport = null;
    }
  };

  const closeTransport = (socketCode = 1000): void => {
    const socket = transport;
    if (!socket) {
      if (explicitShutdown) setState("closed");
      return;
    }

    if (
      socket.readyState === WS_CONNECTING ||
      socket.readyState === WS_OPEN
    ) {
      try {
        socket.close(socketCode);
      } catch (error) {
        lastError = error;
        detachTransport(socket);
        setState(explicitShutdown ? "closed" : "error");
      }
      return;
    }

    if (socket.readyState === WS_CLOSED) {
      detachTransport(socket);
      setState(explicitShutdown ? "closed" : "disconnected");
    }
  };

  const bindConversationSession = (): ConversationSession => {
    if (conversationSession && !conversationSession.closed) {
      return conversationSession;
    }

    conversationSession = openSession(options.session);
    closeReason = undefined;

    removeSessionCloseListener?.();
    removeSessionCloseListener = conversationSession.onClose((info) => {
      explicitShutdown = true;
      closeReason = info.reason;

      if (transport) {
        setState("closing");
        closeTransport(pendingSocketCloseCode);
      } else {
        setState("closed");
      }
    });

    return conversationSession;
  };

  const openTransport = (): void => {
    if (
      transport &&
      (transport.readyState === WS_CONNECTING ||
        transport.readyState === WS_OPEN)
    ) {
      return;
    }

    lastError = undefined;
    const generation = ++transportGeneration;
    let socket: WebSocket;

    try {
      socket = createWebSocket(options.url, options.protocols);
    } catch (error) {
      lastError = error;
      setState("error");
      throw error;
    }

    transport = socket;
    setState("connecting");

    socket.addEventListener("open", (event) => {
      if (generation !== transportGeneration || transport !== socket) return;
      setState("open");
      options.onSocketOpen?.(event);
    });

    socket.addEventListener("message", (event) => {
      if (generation !== transportGeneration || transport !== socket) return;
      options.onSocketMessage?.(event);
    });

    socket.addEventListener("error", (event) => {
      if (generation !== transportGeneration || transport !== socket) return;
      lastError = event;
      setState("error");
      options.onSocketError?.(event);
    });

    socket.addEventListener("close", (event) => {
      if (generation !== transportGeneration || transport !== socket) return;

      detachTransport(socket);
      options.onSocketClose?.(event);

      if (explicitShutdown || conversationSession?.closed) {
        setState("closed");
        return;
      }

      // Network/remote close does NOT close ConversationSession. A later open()
      // may attach a fresh transport to the same still-live conversation.
      setState("disconnected");
    });
  };

  const controller: SessionWebSocketController = {
    get session() {
      return conversationSession;
    },

    get socket() {
      return transport;
    },

    get state() {
      return state;
    },

    open(): ConversationSession {
      if (explicitShutdown) {
        throw new Error(
          "Session WebSocket controller has been closed. Create a new controller for a new session.",
        );
      }

      const session = bindConversationSession();
      openTransport();
      return session;
    },

    send(data: SessionWebSocketSendData): boolean {
      if (!transport || transport.readyState !== WS_OPEN) {
        return false;
      }

      transport.send(data);
      return true;
    },

    close(reason?: string, socketCode = 1000): void {
      if (explicitShutdown && state === "closed") return;

      explicitShutdown = true;
      closeReason = reason;
      pendingSocketCloseCode = socketCode;

      if (conversationSession && !conversationSession.closed) {
        // onClose listener owns the transport shutdown path.
        conversationSession.close(reason);
        return;
      }

      if (transport) {
        setState("closing");
        closeTransport(socketCode);
        return;
      }

      setState("closed");
    },

    snapshot,

    subscribe(listener: SessionWebSocketListener): () => void {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },

    [Symbol.dispose](): void {
      controller.close("dispose");
    },
  };

  return controller;
}
