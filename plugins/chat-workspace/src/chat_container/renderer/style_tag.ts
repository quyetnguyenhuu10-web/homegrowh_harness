// Gắn 1 thẻ <style data-chat-thread="key"> duy nhất cho mỗi key,
// đếm ref để nhiều component cùng dùng 1 thẻ và tự gỡ khi hết.
const tags = new Map<string, { el: HTMLStyleElement; refs: number }>();

export function acquireStyleTag(key: string, cssText: string): void {
  if (typeof document === "undefined") return;
  const found = tags.get(key);
  if (found) {
    found.refs += 1;
    return;
  }
  const el = document.createElement("style");
  el.setAttribute("data-chat-thread", key);
  el.textContent = cssText;
  document.head.appendChild(el);
  tags.set(key, { el, refs: 1 });
}

export function releaseStyleTag(key: string): void {
  if (typeof document === "undefined") return;
  const found = tags.get(key);
  if (!found) return;
  found.refs = Math.max(0, found.refs - 1);
  if (found.refs === 0) {
    found.el.remove();
    tags.delete(key);
  }
}
