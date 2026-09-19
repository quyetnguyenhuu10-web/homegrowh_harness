import type {
  ConversationSession,
  SessionOptions,
} from "@homegrowh/session-conversation";

/**
 * Transport state only. The ConversationSession has its own `closed` flag.
 * A disconnected/error socket does not implicitly close the conversation.
 */
export type SessionWebSocketState =
  | "idle"
  | "connecting"
  | "open"
  | "disconnected"
  | "error"
  | "closing"
  | "closed";

export type SessionWebSocketSendData = Parameters<WebSocket["send"]>[0];

export interface SessionWebSocketSnapshot {
  state: SessionWebSocketState;
  sessionId: string | null;
  sessionClosed: boolean;
  socketReadyState: number | null;
  closeReason?: string;
  error?: unknown;
}

export type SessionWebSocketListener = (
  snapshot: SessionWebSocketSnapshot,
) => void;

export type SessionWebSocketFactory = (
  url: string | URL,
  protocols?: string | string[],
) => WebSocket;

/**
 * WebSocket protocol is intentionally NOT defined here yet.
 * The callbacks expose native browser events so the later end-to-end layer can
 * decide framing, codecs, authentication and message routing without replacing
 * this lifecycle owner.
 */
export interface SessionWebSocketOptions {
  url: string | URL;
  protocols?: string | string[];

  /** Options forwarded to @homegrowh/session-conversation openSession(). */
  session?: SessionOptions;

  /** Test/host injection point. Defaults to the browser WebSocket constructor. */
  createWebSocket?: SessionWebSocketFactory;

  onStateChange?: SessionWebSocketListener;
  onSocketOpen?: (event: Event) => void;
  onSocketMessage?: (event: MessageEvent) => void;
  onSocketError?: (event: Event) => void;
  onSocketClose?: (event: CloseEvent) => void;
}

export interface SessionWebSocketController {
  readonly session: ConversationSession | null;
  readonly socket: WebSocket | null;
  readonly state: SessionWebSocketState;

  /**
   * Opens the ConversationSession when needed and opens/reopens its transport.
   * Calling again while CONNECTING/OPEN is idempotent.
   */
  open(): ConversationSession;

  /** Raw transport send. No JSON/protocol assumptions are made here. */
  send(data: SessionWebSocketSendData): boolean;

  /**
   * Explicitly owns final shutdown: closes the ConversationSession and transport.
   * A remote/network socket close alone does not close the ConversationSession.
   */
  close(reason?: string, socketCode?: number): void;

  snapshot(): SessionWebSocketSnapshot;
  subscribe(listener: SessionWebSocketListener): () => void;

  [Symbol.dispose](): void;
}
