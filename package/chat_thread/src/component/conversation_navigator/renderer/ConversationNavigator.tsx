import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import {
  BookOpen,
  ChevronDown,
  FolderOpen,
  Plus,
  Settings,
  SquarePen,
  Trash2,
} from "lucide-react";

import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";
import {
  getChatThreadDesktopBridge,
  hasChatThreadDesktopBridge,
  NORMAL_CONVERSATION_SCOPE,
} from "../../history_conversation/renderer";

import cssText from "./style.css?inline";

export interface ConversationNavigatorConversation {
  id: string;
  title: string;
  active?: boolean;
}

export interface ConversationNavigatorProject {
  id: string;
  name: string;
  active?: boolean;
  conversations?: readonly ConversationNavigatorConversation[];
}

export interface ConversationNavigatorProps {
  /** Brand/title ở đầu panel. */
  title?: string;

  /** Các project và conversation con chỉ để render geometry. */
  projects?: readonly ConversationNavigatorProject[];

  /** Conversation không thuộc project, hiển thị trong khối "Đoạn chat". */
  conversations?: readonly ConversationNavigatorConversation[];

  activeRepositoryPath?: string | null;
  activeConversationId?: string | null;
  /** Đổi giá trị để yêu cầu navigator đọc lại registry. */
  refreshKey?: number;

  onConversationSelect?: (
    repositoryPath: string,
    conversationId: string,
  ) => void;

  onConversationCreate?: (
    repositoryPath: string,
  ) =>
    | ConversationNavigatorConversation
    | void
    | Promise<ConversationNavigatorConversation | void>;

  onConversationDeleted?: (
    repositoryPath: string,
    conversationId: string,
  ) => void;

  className?: string;
}

const STYLE_KEY = "conversation-navigator";
const PROJECT_CONVERSATION_TITLE_SPEED_PX_PER_SECOND = 36;

/**
 * Geometry-only port of md_ai LeftPanel.
 *
 * Component này chỉ sở hữu hình học/UI shell. Không có storage, routing,
 * project lifecycle, conversation selection hay action logic.
 */
export default function ConversationNavigator({
  title = "Chat",
  projects = [],
  conversations = [],
  activeRepositoryPath = null,
  activeConversationId = null,
  refreshKey = 0,
  onConversationSelect,
  onConversationCreate,
  onConversationDeleted,
  className,
}: ConversationNavigatorProps) {
  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);
    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  const rootClassName = className
    ? `ct-conversation-nav ${className}`
    : "ct-conversation-nav";

  const [addedRepositories, setAddedRepositories] = useState<
    readonly ConversationNavigatorProject[]
  >([]);
  const [addedConversations, setAddedConversations] = useState<
    readonly ConversationNavigatorConversation[]
  >([]);
  const [isAddingRepository, setIsAddingRepository] = useState(false);
  const [projectsCollapsed, setProjectsCollapsed] = useState(false);
  const [conversationsCollapsed, setConversationsCollapsed] = useState(false);
  const [addingConversationFor, setAddingConversationFor] = useState<string | null>(
    null,
  );
  const [deletingConversationKey, setDeletingConversationKey] = useState<
    string | null
  >(null);
  const repositoryBridgeAvailable = hasChatThreadDesktopBridge();

  useEffect(() => {
    const repositoryBridge = getChatThreadDesktopBridge();
    if (!repositoryBridge) return;

    let cancelled = false;
    void Promise.all([
      repositoryBridge.listRepositories(),
      repositoryBridge.listConversations(),
    ])
      .then(([repositories, normalConversations]) => {
        if (cancelled) return;
        setAddedRepositories(
          repositories.map((repository) => ({
            id: repository.repositoryPath,
            name: repository.name,
            conversations: repository.conversations.map((conversation) => ({
              id: conversation.id,
              title: conversation.id,
            })),
          })),
        );
        setAddedConversations(
          normalConversations.map((conversation) => ({
            id: conversation.id,
            title: conversation.id,
          })),
        );
      })
      .catch((error) => {
        console.error("[ConversationNavigator] listRepositories failed", error);
      });

    return () => {
      cancelled = true;
    };
  }, [refreshKey]);

  const renderedProjects = useMemo(() => {
    if (addedRepositories.length === 0) return projects;

    const seen = new Set(projects.map((project) => project.id));
    return [
      ...projects,
      ...addedRepositories.filter((project) => !seen.has(project.id)),
    ];
  }, [addedRepositories, projects]);

  const renderedConversations = useMemo(() => {
    if (addedConversations.length === 0) return conversations;

    const seen = new Set(conversations.map((conversation) => conversation.id));
    return [
      ...conversations,
      ...addedConversations.filter((conversation) => !seen.has(conversation.id)),
    ];
  }, [addedConversations, conversations]);

  const addRepository = async (): Promise<void> => {
    if (isAddingRepository) return;

    const repositoryBridge = getChatThreadDesktopBridge();
    if (!repositoryBridge) return;

    setIsAddingRepository(true);
    try {
      const repository = await repositoryBridge.addRepository();
      if (!repository) return;
      setAddedRepositories((current) => {
        if (current.some((project) => project.id === repository.repositoryPath)) {
          return current;
        }

        return [
          ...current,
          {
            id: repository.repositoryPath,
            name: repository.name,
            conversations: repository.conversations.map((conversation) => ({
              id: conversation.id,
              title: conversation.id,
            })),
          },
        ];
      });
    } catch (error) {
      console.error("[ConversationNavigator] addRepository failed", error);
    } finally {
      setIsAddingRepository(false);
    }
  };

  const addConversation = async (
    project: ConversationNavigatorProject,
  ): Promise<void> => {
    if (addingConversationFor !== null) return;

    if (onConversationCreate) {
      setAddingConversationFor(project.id);
      try {
        const conversation = await onConversationCreate(project.id);
        if (conversation) {
          setAddedRepositories((current) =>
            current.map((repository) =>
              repository.id === project.id
                ? {
                    ...repository,
                    conversations: [
                      ...(repository.conversations ?? []).filter(
                        (item) => item.id !== conversation.id,
                      ),
                      conversation,
                    ],
                  }
                : repository,
            ),
          );
        }
      } catch (error) {
        console.error(
          "[ConversationNavigator] onConversationCreate failed",
          error,
        );
      } finally {
        setAddingConversationFor(null);
      }
      return;
    }

    const repositoryBridge = getChatThreadDesktopBridge();
    if (!repositoryBridge) return;

    setAddingConversationFor(project.id);
    try {
      const conversation = await repositoryBridge.addConversation(project.id);
      setAddedRepositories((current) =>
        current.map((repository) =>
          repository.id === project.id
            ? {
                ...repository,
                conversations: [
                  ...(repository.conversations ?? []),
                  {
                    id: conversation.id,
                    title: conversation.id,
                  },
                ],
              }
            : repository,
        ),
      );
    } catch (error) {
      console.error("[ConversationNavigator] addConversation failed", error);
    } finally {
      setAddingConversationFor(null);
    }
  };

  const deleteConversation = async (
    project: ConversationNavigatorProject,
    conversation: ConversationNavigatorConversation,
  ): Promise<void> => {
    if (deletingConversationKey !== null) return;

    const repositoryBridge = getChatThreadDesktopBridge();
    if (!repositoryBridge) return;

    const key = `${project.id}\u0000${conversation.id}`;
    setDeletingConversationKey(key);
    try {
      const deleted = await repositoryBridge.deleteConversation(
        project.id,
        conversation.id,
      );
      if (!deleted) return;

      setAddedRepositories((current) =>
        current.map((repository) =>
          repository.id === project.id
            ? {
                ...repository,
                conversations: (repository.conversations ?? []).filter(
                  (item) => item.id !== conversation.id,
                ),
              }
            : repository,
        ),
      );
      onConversationDeleted?.(project.id, conversation.id);
    } catch (error) {
      console.error("[ConversationNavigator] deleteConversation failed", error);
    } finally {
      setDeletingConversationKey(null);
    }
  };

  const deleteNormalConversation = async (
    conversation: ConversationNavigatorConversation,
  ): Promise<void> => {
    if (deletingConversationKey !== null) return;

    const repositoryBridge = getChatThreadDesktopBridge();
    if (!repositoryBridge) return;

    const key = `${NORMAL_CONVERSATION_SCOPE}\u0000${conversation.id}`;
    setDeletingConversationKey(key);
    try {
      const deleted = await repositoryBridge.deleteConversation(
        NORMAL_CONVERSATION_SCOPE,
        conversation.id,
      );
      if (!deleted) return;

      setAddedConversations((current) =>
        current.filter((item) => item.id !== conversation.id),
      );
      onConversationDeleted?.(NORMAL_CONVERSATION_SCOPE, conversation.id);
    } catch (error) {
      console.error(
        "[ConversationNavigator] delete normal conversation failed",
        error,
      );
    } finally {
      setDeletingConversationKey(null);
    }
  };

  return (
    <aside className={rootClassName} aria-label="Điều hướng hội thoại">
      <header className="ct-conversation-nav__header">
        <strong className="ct-conversation-nav__title">{title}</strong>
      </header>

      <div className="ct-conversation-nav__scroll-region">
        <nav className="ct-conversation-nav__nav" aria-label="Dự án">
          <section className="ct-conversation-nav__projects-section">
            <div className="ct-conversation-nav__projects-heading">
              <button
                className={
                  projectsCollapsed
                    ? "ct-conversation-nav__section-label-wrap is-collapsed"
                    : "ct-conversation-nav__section-label-wrap"
                }
                type="button"
                aria-expanded={!projectsCollapsed}
                onClick={() => setProjectsCollapsed((current) => !current)}
              >
                <span className="ct-conversation-nav__section-label">Dự án</span>
                {renderedProjects.length > 0 && (
                  <ChevronDown
                    className="ct-conversation-nav__section-chevron"
                    aria-hidden="true"
                    strokeWidth={1.9}
                  />
                )}
              </button>

              <button
                className="ct-conversation-nav__project-add"
                type="button"
                disabled={!repositoryBridgeAvailable || isAddingRepository}
                aria-label="Thêm project"
                title="Thêm repository"
                onClick={() => void addRepository()}
              >
                <Plus aria-hidden="true" strokeWidth={1.9} />
              </button>
            </div>

            {!projectsCollapsed && (
              <div className="ct-conversation-nav__project-list">
                {renderedProjects.map((project) => (
                  <section
                    key={project.id}
                    className={
                      project.active
                        ? "ct-conversation-nav__project-group is-active"
                        : "ct-conversation-nav__project-group"
                    }
                  >
                  <div className="ct-conversation-nav__project-row">
                    <span className="ct-conversation-nav__project-name-wrap">
                      <FolderOpen
                        className="ct-conversation-nav__project-icon"
                        aria-hidden="true"
                        strokeWidth={1.8}
                      />
                      <span className="ct-conversation-nav__project-name">
                        {project.name}
                      </span>
                    </span>

                    <span className="ct-conversation-nav__project-actions">
                      <button
                        className="ct-conversation-nav__project-action-slot"
                        type="button"
                        disabled={
                          !repositoryBridgeAvailable ||
                          addingConversationFor !== null
                        }
                        aria-label={`Thêm conversation cho ${project.name}`}
                        title="Thêm conversation"
                        onClick={() => void addConversation(project)}
                      >
                        <SquarePen aria-hidden="true" strokeWidth={1.8} />
                      </button>
                    </span>
                  </div>

                  <div className="ct-conversation-nav__project-conversations">
                    {(project.conversations ?? []).map((conversation) => (
                      <ConversationRow
                        key={conversation.id}
                        conversation={conversation}
                        nested
                        active={
                          activeRepositoryPath === project.id &&
                          activeConversationId === conversation.id
                        }
                        onSelect={
                          onConversationSelect
                            ? () => onConversationSelect(project.id, conversation.id)
                            : undefined
                        }
                        deleting={
                          deletingConversationKey ===
                          `${project.id}\u0000${conversation.id}`
                        }
                        onDelete={() => void deleteConversation(project, conversation)}
                      />
                    ))}
                  </div>
                  </section>
                ))}
              </div>
            )}
          </section>

          <section className="ct-conversation-nav__projects-section ct-conversation-nav__chats-section">
            <div className="ct-conversation-nav__projects-heading">
              <button
                className={
                  conversationsCollapsed
                    ? "ct-conversation-nav__section-label-wrap is-collapsed"
                    : "ct-conversation-nav__section-label-wrap"
                }
                type="button"
                aria-expanded={!conversationsCollapsed}
                onClick={() =>
                  setConversationsCollapsed((current) => !current)
                }
              >
                <span className="ct-conversation-nav__section-label">
                  Đoạn chat
                </span>
                {renderedConversations.length > 0 && (
                  <ChevronDown
                    className="ct-conversation-nav__section-chevron"
                    aria-hidden="true"
                    strokeWidth={1.9}
                  />
                )}
              </button>
            </div>

            {!conversationsCollapsed && renderedConversations.length > 0 && (
              <div className="ct-conversation-nav__conversation-list">
                {renderedConversations.map((conversation) => (
                  <ConversationRow
                    key={conversation.id}
                    conversation={conversation}
                    nested={false}
                    active={
                      activeRepositoryPath === NORMAL_CONVERSATION_SCOPE &&
                      activeConversationId === conversation.id
                    }
                    onSelect={
                      onConversationSelect
                        ? () =>
                            onConversationSelect(
                              NORMAL_CONVERSATION_SCOPE,
                              conversation.id,
                            )
                        : undefined
                    }
                    deleting={
                      deletingConversationKey ===
                      `${NORMAL_CONVERSATION_SCOPE}\u0000${conversation.id}`
                    }
                    onDelete={() => void deleteNormalConversation(conversation)}
                  />
                ))}
              </div>
            )}
          </section>
        </nav>
      </div>

      <footer className="ct-conversation-nav__footer">
        <div className="ct-conversation-nav__footer-row">
          <BookOpen
            className="ct-conversation-nav__footer-icon"
            aria-hidden="true"
            strokeWidth={1.8}
          />
          <span>Thư viện</span>
        </div>
        <div className="ct-conversation-nav__footer-row">
          <Settings
            className="ct-conversation-nav__footer-icon"
            aria-hidden="true"
            strokeWidth={1.8}
          />
          <span>Cài đặt</span>
        </div>
      </footer>
    </aside>
  );
}

function ConversationRow({
  conversation,
  nested,
  active,
  onSelect,
  deleting = false,
  onDelete,
}: {
  conversation: ConversationNavigatorConversation;
  nested: boolean;
  active?: boolean;
  onSelect?: () => void;
  deleting?: boolean;
  onDelete?: () => void;
}) {
  const titleViewportRef = useRef<HTMLSpanElement | null>(null);

  useLayoutEffect(() => {
    const viewport = titleViewportRef.current;
    if (!viewport) return;

    const text = viewport.querySelector<HTMLElement>(
      ".ct-conversation-nav__conversation-title-text",
    );
    if (!text) return;

    const sync = (): void => {
      const overflowWidth = Math.max(
        0,
        Math.ceil(text.scrollWidth - viewport.clientWidth),
      );
      const overflowing = overflowWidth > 0;
      viewport.classList.toggle("is-overflowing", overflowing);

      if (!overflowing) {
        viewport.style.removeProperty("--ct-conversation-title-shift");
        viewport.style.removeProperty("--ct-conversation-title-duration");
        return;
      }

      const durationMs = Math.max(
        1,
        Math.round(
          (overflowWidth / PROJECT_CONVERSATION_TITLE_SPEED_PX_PER_SECOND) * 1000,
        ),
      );
      viewport.style.setProperty(
        "--ct-conversation-title-shift",
        `${-overflowWidth}px`,
      );
      viewport.style.setProperty(
        "--ct-conversation-title-duration",
        `${durationMs}ms`,
      );
    };

    sync();
    const observer = new ResizeObserver(sync);
    observer.observe(viewport);
    observer.observe(text);

    return () => observer.disconnect();
  }, [conversation.title]);

  const className = [
    "ct-conversation-nav__conversation",
    nested ? "ct-conversation-nav__conversation--nested" : "",
    (active ?? conversation.active) ? "is-active" : "",
    onSelect ? "is-selectable" : "",
  ]
    .filter(Boolean)
    .join(" ");

  return (
    <div
      className={className}
      role={onSelect ? "button" : undefined}
      tabIndex={onSelect ? 0 : undefined}
      onClick={onSelect}
      onKeyDown={
        onSelect
          ? (event) => {
              if (event.key !== "Enter" && event.key !== " ") return;
              event.preventDefault();
              onSelect();
            }
          : undefined
      }
    >
      <span
        ref={titleViewportRef}
        className="ct-conversation-nav__conversation-title-viewport"
      >
        <span className="ct-conversation-nav__conversation-title-text">
          {conversation.title}
        </span>
      </span>
      {onDelete && (
        <button
          className="ct-conversation-nav__conversation-delete"
          type="button"
          disabled={deleting}
          aria-label={`Xóa conversation ${conversation.title}`}
          title="Xóa conversation"
          onClick={(event) => {
            event.stopPropagation();
            onDelete();
          }}
        >
          <Trash2 aria-hidden="true" strokeWidth={1.8} />
        </button>
      )}
    </div>
  );
}
