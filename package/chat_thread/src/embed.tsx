// API nhúng chat_thread vào app khác.
// Cách 1 (JS):  import { mountChatThread } from "chat-thread/embed";
//               const unmount = mountChatThread("#noi-nhung");
// Cách 2 (HTML): <script src="chat-thread.umd.js"></script>
//               <chat-thread></chat-thread>
import { createRoot, type Root } from "react-dom/client";
import App from "./App";

/** Tùy chọn nhúng (giữ chỗ để mở rộng sau, hiện chưa dùng). */
export interface ChatThreadOptions {
  [key: string]: unknown;
}

const roots = new WeakMap<HTMLElement, Root>();

function resolveTarget(target: HTMLElement | string): HTMLElement {
  const el =
    typeof target === "string" ? document.querySelector<HTMLElement>(target) : target;
  if (!el) throw new Error(`mountChatThread: không thấy container ${JSON.stringify(String(target))}.`);
  return el;
}

/** Gắn app vào 1 element bất kỳ, trả về hàm gỡ. Gọi lại cùng element sẽ mount lại sạch. */
export function mountChatThread(
  target: HTMLElement | string,
  _options: ChatThreadOptions = {},
): () => void {
  void _options;
  const el = resolveTarget(target);
  unmountChatThread(el);
  const root = createRoot(el);
  roots.set(el, root);
  root.render(<App />);
  return () => unmountChatThread(el);
}

/** Gỡ app khỏi element (an toàn khi gọi nhiều lần). */
export function unmountChatThread(target: HTMLElement | string): void {
  const el =
    typeof target === "string" ? document.querySelector<HTMLElement>(target) : target;
  if (!el) return;
  const root = roots.get(el);
  if (!root) return;
  roots.delete(el);
  // Chờ 1 tick để React 19 dọn concurrent render rồi mới unmount.
  queueMicrotask(() => root.unmount());
}

class ChatThreadElement extends HTMLElement {
  private dispose?: () => void;

  connectedCallback(): void {
    this.dispose = mountChatThread(this);
  }

  disconnectedCallback(): void {
    this.dispose?.();
    this.dispose = undefined;
  }
}

// Tự đăng ký thẻ <chat-thread> khi bundle được nạp (chỉ trên browser).
if (
  typeof window !== "undefined" &&
  typeof customElements !== "undefined" &&
  !customElements.get("chat-thread")
) {
  customElements.define("chat-thread", ChatThreadElement);
}
