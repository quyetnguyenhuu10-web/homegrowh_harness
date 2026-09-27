import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import {
  FolderOpen,
  MessageSquare,
  Plus,
  Search,
  SquareArrowOutUpRight,
  SquarePen,
} from "lucide-react";

import {
  getChatThreadDesktopBridge,
  hasChatThreadDesktopBridge,
  type HistoryRepository,
} from "../../history_conversation/renderer";
import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";

import cssText from "./style.css?inline";

export interface EmptyConversationStateProps {
  title?: string;
  onConversationSelect?: (
    repositoryPath: string,
    conversationId: string,
  ) => void;
  onConversationCreate?: (repositoryPath: string) => void | Promise<void>;
  onRepositoryAdded?: () => void;
}

const STYLE_KEY = "empty-conversation-state";

export default function EmptyConversationState({
  title = "Homegrowh Harness",
  onConversationSelect,
  onConversationCreate,
  onRepositoryAdded,
}: EmptyConversationStateProps) {
  const [open, setOpen] = useState(false);
  const [repositories, setRepositories] = useState<readonly HistoryRepository[]>(
    [],
  );
  const [loading, setLoading] = useState(false);
  const [addingRepository, setAddingRepository] = useState(false);
  const [repositorySearch, setRepositorySearch] = useState("");
  const [creatingRepositoryPath, setCreatingRepositoryPath] = useState<
    string | null
  >(null);
  const rootRef = useRef<HTMLDivElement | null>(null);
  const bridgeAvailable = hasChatThreadDesktopBridge();
  const filteredRepositories = useMemo(() => {
    const query = repositorySearch.trim().toLocaleLowerCase();
    if (!query) return repositories;

    return repositories.filter((repository) =>
      repository.name.toLocaleLowerCase().includes(query),
    );
  }, [repositories, repositorySearch]);

  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  useEffect(() => {
    if (!open) return;

    const bridge = getChatThreadDesktopBridge();
    if (!bridge) return;

    let cancelled = false;
    setLoading(true);
    void bridge
      .listRepositories()
      .then((next) => {
        if (!cancelled) setRepositories(next);
      })
      .catch((error) => {
        console.error("[EmptyConversationState] listRepositories failed", error);
      })
      .finally(() => {
        if (!cancelled) setLoading(false);
      });

    const onPointerDown = (event: PointerEvent): void => {
      const root = rootRef.current;
      if (!root?.contains(event.target as Node)) setOpen(false);
    };
    const onKeyDown = (event: KeyboardEvent): void => {
      if (event.key === "Escape") setOpen(false);
    };

    document.addEventListener("pointerdown", onPointerDown);
    document.addEventListener("keydown", onKeyDown);
    return () => {
      cancelled = true;
      document.removeEventListener("pointerdown", onPointerDown);
      document.removeEventListener("keydown", onKeyDown);
    };
  }, [open]);

  const createConversation = async (repositoryPath: string): Promise<void> => {
    if (!onConversationCreate || creatingRepositoryPath !== null) return;
    setCreatingRepositoryPath(repositoryPath);
    try {
      await onConversationCreate(repositoryPath);
      setOpen(false);
    } catch (error) {
      console.error("[EmptyConversationState] create conversation failed", error);
    } finally {
      setCreatingRepositoryPath(null);
    }
  };

  const addRepository = async (): Promise<void> => {
    if (addingRepository) return;

    const bridge = getChatThreadDesktopBridge();
    if (!bridge) return;

    setAddingRepository(true);
    try {
      const repository = await bridge.addRepository();
      if (!repository) return;

      setRepositories((current) => {
        const withoutDuplicate = current.filter(
          (item) => item.repositoryPath !== repository.repositoryPath,
        );
        return [...withoutDuplicate, repository];
      });
      onRepositoryAdded?.();
    } catch (error) {
      console.error("[EmptyConversationState] addRepository failed", error);
    } finally {
      setAddingRepository(false);
    }
  };

  return (
    <div className="ct-empty-conversation" ref={rootRef}>
      <h1 className="ct-empty-conversation__title">{title}</h1>

      <button
        className="ct-empty-conversation__project-trigger"
        type="button"
        disabled={!bridgeAvailable}
        aria-haspopup="dialog"
        aria-expanded={open}
        onClick={() => setOpen((current) => !current)}
      >
        <span>Bắt đầu với dự án của bạn</span>
        <SquareArrowOutUpRight aria-hidden="true" strokeWidth={1.8} />
      </button>

      {open && (
        <div
          className="ct-empty-conversation__popup"
          role="dialog"
          aria-label="Chọn dự án hoặc hội thoại"
        >
          {loading ? (
            <div className="ct-empty-conversation__status">Đang tải…</div>
          ) : repositories.length === 0 ? (
            <div className="ct-empty-conversation__empty-repositories">
              <div className="ct-empty-conversation__empty-repositories-content">
                <button
                  className="ct-empty-conversation__add-repository"
                  type="button"
                  disabled={addingRepository}
                  aria-label="Thêm repository"
                  title="Thêm repository"
                  onClick={() => void addRepository()}
                >
                  <Plus aria-hidden="true" strokeWidth={1.8} />
                </button>
                <span className="ct-empty-conversation__add-repository-label">
                  Thêm repository
                </span>
              </div>
            </div>
          ) : (
            <div className="ct-empty-conversation__repositories">
              <div className="ct-empty-conversation__popup-toolbar">
                <button
                  className="ct-empty-conversation__search-button"
                  type="button"
                  aria-label="Tìm repository"
                  title="Tìm repository"
                  onClick={(event) => {
                    const toolbar = event.currentTarget.parentElement;
                    toolbar
                      ?.querySelector<HTMLInputElement>(
                        ".ct-empty-conversation__search-input",
                      )
                      ?.focus();
                  }}
                >
                  <Search aria-hidden="true" strokeWidth={1.8} />
                </button>

                <input
                  className="ct-empty-conversation__search-input"
                  type="search"
                  value={repositorySearch}
                  autoComplete="off"
                  aria-label="Tìm repository"
                  placeholder="Tìm repository…"
                  onChange={(event) => setRepositorySearch(event.target.value)}
                />

                <button
                  className="ct-empty-conversation__add-repository ct-empty-conversation__add-repository--corner"
                  type="button"
                  disabled={addingRepository}
                  aria-label="Thêm repository"
                  title="Thêm repository"
                  onClick={() => void addRepository()}
                >
                  <Plus aria-hidden="true" strokeWidth={1.8} />
                </button>
              </div>

              <div className="ct-empty-conversation__repository-list">
                {filteredRepositories.map((repository) => (
                  <section
                    className="ct-empty-conversation__repository"
                    key={repository.repositoryPath}
                  >
                    <div className="ct-empty-conversation__repository-row">
                      <span className="ct-empty-conversation__repository-name">
                        <FolderOpen aria-hidden="true" strokeWidth={1.8} />
                        <span>{repository.name}</span>
                      </span>
                      <button
                        className="ct-empty-conversation__new-chat"
                        type="button"
                        disabled={
                          !onConversationCreate ||
                          creatingRepositoryPath !== null
                        }
                        aria-label={`Tạo conversation mới cho ${repository.name}`}
                        title="Tạo conversation mới"
                        onClick={() =>
                          void createConversation(repository.repositoryPath)
                        }
                      >
                        <SquarePen aria-hidden="true" strokeWidth={1.8} />
                      </button>
                    </div>

                    {repository.conversations.length > 0 && (
                      <div className="ct-empty-conversation__conversation-list">
                        {repository.conversations.map((conversation) => (
                          <button
                            className="ct-empty-conversation__conversation"
                            type="button"
                            key={conversation.id}
                            onClick={() => {
                              setOpen(false);
                              onConversationSelect?.(
                                repository.repositoryPath,
                                conversation.id,
                              );
                            }}
                          >
                            <MessageSquare
                              aria-hidden="true"
                              strokeWidth={1.7}
                            />
                            <span>{conversation.id}</span>
                          </button>
                        ))}
                      </div>
                    )}
                  </section>
                ))}
                {filteredRepositories.length === 0 && (
                  <div className="ct-empty-conversation__status">
                    Không tìm thấy repository.
                  </div>
                )}
              </div>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
