const readFilesByConversation = new Map<string, Set<string>>();

function stateKey(conversationId?: string, requestId?: string): string {
  return conversationId ?? requestId ?? "default";
}

export function markFileRead(
  path: string,
  conversationId?: string,
  requestId?: string,
): void {
  const key = stateKey(conversationId, requestId);
  let files = readFilesByConversation.get(key);
  if (!files) {
    files = new Set<string>();
    readFilesByConversation.set(key, files);
  }
  files.add(path);
}

export function wasFileRead(
  path: string,
  conversationId?: string,
  requestId?: string,
): boolean {
  return (
    readFilesByConversation
      .get(stateKey(conversationId, requestId))
      ?.has(path) ?? false
  );
}

export function readFilesFor(
  conversationId?: string,
  requestId?: string,
): string[] {
  return [
    ...(readFilesByConversation.get(stateKey(conversationId, requestId)) ?? []),
  ];
}

export function mergeReadFiles(
  paths: readonly string[],
  conversationId?: string,
  requestId?: string,
): void {
  for (const path of paths) {
    markFileRead(path, conversationId, requestId);
  }
}
