import {
  CHAT_THREAD_DESKTOP_BRIDGE_KEY,
  type ChatThreadDesktopBridge,
} from "../desktop_contract";

declare global {
  interface Window {
    __homegrowhChatThreadDesktop?: ChatThreadDesktopBridge;
  }
}

export function getChatThreadDesktopBridge(): ChatThreadDesktopBridge | null {
  if (typeof window === "undefined") return null;
  return window[CHAT_THREAD_DESKTOP_BRIDGE_KEY] ?? null;
}

export function hasChatThreadDesktopBridge(): boolean {
  return getChatThreadDesktopBridge() !== null;
}
