import {
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import type { ReactNode } from "react";

import ConversationNavigator, {
  type ConversationNavigatorProps,
} from "../../conversation_navigator/renderer";
import { acquireStyleTag, releaseStyleTag } from "../../style_tag/renderer";
import ViewHistory, {
  type HistoryEntry,
} from "../../view_history/renderer";

import cssText from "./style.css?inline";

export type PanelEntry = HistoryEntry;

export type ChatContainerBoundary = "top" | "bottom";
export type ChatContainerScrollDirection = "up" | "down";

export interface ChatContainerBoundaryLoadEvent {
  /** Biên viewport vừa chạm. */
  edge: ChatContainerBoundary;

  /** Hướng scroll tương ứng với biên. */
  direction: ChatContainerScrollDirection;

  /** Range resident hiện tại, end là exclusive. */
  residentStart: number;
  residentEnd: number;

  /** Tổng số panel logic hiện có. */
  totalPanels: number;

  /** Resident start mà container sẽ chuyển tới nếu callback cho phép load. */
  nextResidentStart: number;
}

export type ChatContainerBoundaryLoadCallback = (
  event: ChatContainerBoundaryLoadEvent,
) => void | boolean | Promise<void | boolean>;

export interface ChatContainerProps {
  /** Danh sách logical entry theo thứ tự thời gian. */
  entries?: readonly PanelEntry[];

  /** Số logical entry trong 1 panel con. */
  entriesPerPanel?: number;

  /** Số panel-content mục tiêu được giữ sống trong RAM. */
  maxPanelsInRam?: number;

  /** Khoảng trống cuối để composer overlay không che tin nhắn. */
  bottomPad?: number;

  /** Chỉ ẩn/hiện reasoning ở lớp render, không làm thay đổi entries nguồn. */
  showThinking?: boolean;

  /**
   * Left panel hội thoại do ChatContainer sở hữu.
   * Không truyền thì ChatContainer chỉ render vùng chat như trước.
   */
  conversationNavigator?: ConversationNavigatorProps | false;

  /** Overlay/content thuộc vùng chat chính, ví dụ Composer. */
  children?: ReactNode;

  /** Thông báo tạm thời cố định ở đầu vùng chat, không thuộc history entries. */
  notice?: string | null;

  /**
   * Hook minh bạch trước mỗi lần resident page load khi chạm biên.
   *
   * - Có thể sync hoặc async.
   * - Promise chưa xong thì container khóa load biên để tránh spam/jank.
   * - Trả `false` để hủy resident transition lần đó.
   * - Không truyền callback thì container load page nội bộ như trước.
   */
  onBoundaryLoad?: ChatContainerBoundaryLoadCallback;
}

const STYLE_KEY = "chat-container";

type ScrollDirection = ChatContainerScrollDirection;

function chunk<T>(arr: readonly T[], size: number): T[][] {
  const out: T[][] = [];

  for (let i = 0; i < arr.length; i += size) {
    out.push(arr.slice(i, i + size));
  }

  return out;
}

function createRangeIds(start: number, end: number): Set<number> {
  const ids = new Set<number>();

  for (let index = start; index < end; index += 1) {
    ids.add(index);
  }

  return ids;
}

export default function ChatContainer({
  entries = [],
  entriesPerPanel = 5,
  maxPanelsInRam = 10,
  bottomPad = 144,
  showThinking = true,
  conversationNavigator,
  children,
  notice = null,
  onBoundaryLoad,
}: ChatContainerProps) {
  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);

    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  const perPanel = Math.max(1, Math.floor(entriesPerPanel));
  const keep = Math.max(1, Math.floor(maxPanelsInRam));
  const residentStep = Math.max(1, Math.floor(keep / 2));

  const panels = useMemo(
    () => chunk(entries, perPanel),
    [entries, perPanel],
  );

  const scrollRef = useRef<HTMLDivElement>(null);
  const previousScrollTopRef = useRef(0);
  const initializedRef = useRef(false);
  const previousPanelCountRef = useRef(panels.length);
  const previousKeepRef = useRef(keep);
  const autoScrollRef = useRef(false);
  const tailFollowTargetRef = useRef<number | null>(null);
  const userScrollInputRef = useRef(false);
  const pointerScrollInputRef = useRef(false);
  const [autoScroll, setAutoScroll] = useState(false);

  const updateAutoScroll = (next: boolean): void => {
    autoScrollRef.current = next;
    setAutoScroll((current) => (current === next ? current : next));
  };

  const isAtBottom = (viewport: HTMLDivElement): boolean => {
    return (
      Math.ceil(viewport.scrollTop + viewport.clientHeight) >=
      viewport.scrollHeight
    );
  };

  const beginTransientUserScrollInput = (): void => {
    userScrollInputRef.current = true;
  };

  const isKeyboardScrollInput = (event: KeyboardEvent): boolean => {
    if (
      event.key !== "ArrowUp" &&
      event.key !== "ArrowDown" &&
      event.key !== "PageUp" &&
      event.key !== "PageDown" &&
      event.key !== "Home" &&
      event.key !== "End" &&
      event.key !== " "
    ) {
      return false;
    }

    const target = event.target;
    if (!(target instanceof HTMLElement)) return true;
    return !(
      target.isContentEditable ||
      target instanceof HTMLInputElement ||
      target instanceof HTMLTextAreaElement ||
      target instanceof HTMLSelectElement
    );
  };

  const beginPointerUserScrollInput = (
    event: React.PointerEvent<HTMLDivElement>,
  ): void => {
    const viewport = scrollRef.current;
    if (!viewport) return;

    if (event.pointerType === "touch" || event.pointerType === "pen") {
      pointerScrollInputRef.current = true;
      userScrollInputRef.current = true;
      return;
    }

    if (event.pointerType !== "mouse" || event.button !== 0) return;

    const scrollbarWidth = viewport.offsetWidth - viewport.clientWidth;
    const rect = viewport.getBoundingClientRect();
    const direction = getComputedStyle(viewport).direction;
    const onScrollbar =
      scrollbarWidth > 0
        ? direction === "rtl"
          ? event.clientX < rect.left + scrollbarWidth
          : event.clientX >= rect.right - scrollbarWidth
        : event.target === viewport;

    if (!onScrollbar) return;

    pointerScrollInputRef.current = true;
    userScrollInputRef.current = true;
  };

  useEffect(() => {
    const viewport = scrollRef.current;

    const onKeyDown = (event: KeyboardEvent): void => {
      if (isKeyboardScrollInput(event)) beginTransientUserScrollInput();
    };

    const onScrollEnd = (): void => {
      if (!pointerScrollInputRef.current) {
        userScrollInputRef.current = false;
      }
    };

    const endPointerUserScrollInput = (): void => {
      pointerScrollInputRef.current = false;
      userScrollInputRef.current = false;
    };

    window.addEventListener("keydown", onKeyDown, true);
    window.addEventListener("pointerup", endPointerUserScrollInput);
    window.addEventListener("pointercancel", endPointerUserScrollInput);
    viewport?.addEventListener("scrollend", onScrollEnd);

    return () => {
      window.removeEventListener("keydown", onKeyDown, true);
      window.removeEventListener("pointerup", endPointerUserScrollInput);
      window.removeEventListener("pointercancel", endPointerUserScrollInput);
      viewport?.removeEventListener("scrollend", onScrollEnd);
    };
  }, []);

  /**
   * Resident window là toàn bộ scroll space đang sống.
   * Panel ngoài window không có shell, spacer hay geometry giả trong DOM.
   */
  const [residentStart, setResidentStart] = useState(() =>
    Math.max(0, panels.length - keep),
  );

  const residentEnd = Math.min(
    panels.length,
    residentStart + keep,
  );

  /**
   * Trong đúng một React commit chuyển page, giữ cả page cũ lẫn page mới.
   * Đây là mount thật của React nên transient peak có thể vượt keep.
   * Commit kế tiếp mới loại page cũ.
   */
  const [transitionResidentIds, setTransitionResidentIds] =
    useState<Set<number> | null>(null);

  const pendingResidentStartRef = useRef<number | null>(null);
  const topPrependAnchorRef = useRef<{
    panelIndex: number;
    top: number;
  } | null>(null);
  const boundaryLoadInFlightRef = useRef<ScrollDirection | null>(null);
  const boundaryLoadRequestRef = useRef(0);
  const [boundaryLoading, setBoundaryLoading] =
    useState<ChatContainerBoundary | null>(null);

  const residentIds = useMemo(
    () =>
      transitionResidentIds ??
      createRangeIds(residentStart, residentEnd),
    [residentEnd, residentStart, transitionResidentIds],
  );
  const includesLastPanel =
    panels.length > 0 && residentIds.has(panels.length - 1);

  const beginPageTransition = (nextStart: number): void => {
    if (nextStart === residentStart) return;
    if (pendingResidentStartRef.current !== null) return;

    if (nextStart < residentStart) {
      const viewport = scrollRef.current;
      const anchor = viewport?.querySelector<HTMLElement>(
        `[data-panel-index="${residentStart}"]`,
      );

      if (anchor) {
        topPrependAnchorRef.current = {
          panelIndex: residentStart,
          top: anchor.getBoundingClientRect().top,
        };
      }
    }

    const nextEnd = Math.min(panels.length, nextStart + keep);
    const union = createRangeIds(residentStart, residentEnd);

    for (let index = nextStart; index < nextEnd; index += 1) {
      union.add(index);
    }

    pendingResidentStartRef.current = nextStart;
    setTransitionResidentIds(union);
  };

  const beginTailFollow = (nextStart: number): void => {
    tailFollowTargetRef.current = nextStart;

    if (
      nextStart === residentStart &&
      transitionResidentIds === null &&
      pendingResidentStartRef.current === null
    ) {
      const viewport = scrollRef.current;
      if (viewport) {
        viewport.scrollTop = viewport.scrollHeight;
        previousScrollTopRef.current = viewport.scrollTop;
      }
      tailFollowTargetRef.current = null;
      return;
    }
    if (pendingResidentStartRef.current !== null) return;
    if (transitionResidentIds !== null) return;

    beginPageTransition(nextStart);
  };

  /**
   * Khi prepend panel cũ ở trần resident DOM, giữ nguyên vị trí thật của panel
   * đầu resident hiện tại. Nếu browser đã native-anchor đúng thì delta = 0;
   * nếu không, bù đúng phần DOM vừa được chèn phía trên.
   */
  useLayoutEffect(() => {
    if (transitionResidentIds === null) return;

    const viewport = scrollRef.current;
    const savedAnchor = topPrependAnchorRef.current;
    if (!viewport || !savedAnchor) return;

    const anchor = viewport.querySelector<HTMLElement>(
      `[data-panel-index="${savedAnchor.panelIndex}"]`,
    );
    if (!anchor) {
      topPrependAnchorRef.current = null;
      return;
    }

    const delta = anchor.getBoundingClientRect().top - savedAnchor.top;
    if (Math.abs(delta) > 0.5) {
      viewport.scrollTop += delta;
      previousScrollTopRef.current = viewport.scrollTop;
    }

    topPrependAnchorRef.current = null;
  }, [transitionResidentIds]);

  const requestBoundaryLoad = (
    direction: ScrollDirection,
    nextResidentStart: number,
  ): void => {
    if (nextResidentStart === residentStart) return;
    if (pendingResidentStartRef.current !== null) return;
    if (boundaryLoadInFlightRef.current !== null) return;

    const edge: ChatContainerBoundary =
      direction === "up" ? "top" : "bottom";

    if (!onBoundaryLoad) {
      beginPageTransition(nextResidentStart);
      return;
    }

    const requestId = ++boundaryLoadRequestRef.current;
    boundaryLoadInFlightRef.current = direction;
    setBoundaryLoading(edge);

    const event: ChatContainerBoundaryLoadEvent = {
      edge,
      direction,
      residentStart,
      residentEnd,
      totalPanels: panels.length,
      nextResidentStart,
    };

    void Promise.resolve()
      .then(() => onBoundaryLoad(event))
      .then((result) => {
        if (boundaryLoadRequestRef.current !== requestId) return;
        if (result === false) return;

        beginPageTransition(nextResidentStart);
      })
      .catch((error: unknown) => {
        console.error("[ChatContainer] onBoundaryLoad failed", error);
      })
      .finally(() => {
        if (boundaryLoadRequestRef.current !== requestId) return;

        boundaryLoadInFlightRef.current = null;
        setBoundaryLoading(null);
      });
  };

  /**
   * Sau khi union page cũ + page mới đã commit thật vào DOM,
   * chuyển ownership sang page mới rồi bỏ content page cũ.
   */
  useEffect(() => {
    if (transitionResidentIds === null) return;

    const nextStart = pendingResidentStartRef.current;
    if (nextStart === null) return;

    setResidentStart(nextStart);
    setTransitionResidentIds(null);
  }, [transitionResidentIds]);

  /**
   * Chỉ mở khóa paging sau khi page mới đã commit xong và page cũ đã rời DOM.
   * Vì vậy scroll event do chính DOM transition sinh ra không thể chain-load.
   */
  useLayoutEffect(() => {
    if (transitionResidentIds !== null) return;
    if (pendingResidentStartRef.current !== residentStart) return;

    pendingResidentStartRef.current = null;

    const tailTarget = tailFollowTargetRef.current;
    if (tailTarget === null) return;

    if (tailTarget !== residentStart) {
      beginPageTransition(tailTarget);
      return;
    }

    const viewport = scrollRef.current;
    if (viewport) {
      viewport.scrollTop = viewport.scrollHeight;
      previousScrollTopRef.current = viewport.scrollTop;
    }
    tailFollowTargetRef.current = null;
  }, [residentStart, transitionResidentIds]);

  /**
   * Append không được hủy paging đang chạy.
   *
   * Nếu đang follow đáy thì resident window cũng follow tail mới. Nếu người dùng
   * đã rời đáy thì giữ nguyên resident window; chỉ shrink/config change mới
   * được phép clamp/reset transition state.
   */
  useLayoutEffect(() => {
    const previousPanelCount = previousPanelCountRef.current;
    const previousKeep = previousKeepRef.current;
    previousPanelCountRef.current = panels.length;
    previousKeepRef.current = keep;

    if (panels.length === 0) {
      initializedRef.current = false;
      tailFollowTargetRef.current = null;
      updateAutoScroll(false);
    } else if (previousPanelCount === 0) {
      initializedRef.current = false;
      setResidentStart(Math.max(0, panels.length - keep));
    }

    const maxStart = Math.max(0, panels.length - keep);
    const grew = panels.length > previousPanelCount;
    const shrank = panels.length < previousPanelCount;
    const keepChanged = keep !== previousKeep;

    if (grew && autoScrollRef.current) {
      beginTailFollow(maxStart);
      return;
    }

    if (shrank || keepChanged) {
      tailFollowTargetRef.current = null;
      setResidentStart((current) => Math.min(current, maxStart));
      pendingResidentStartRef.current = null;
      topPrependAnchorRef.current = null;
      setTransitionResidentIds(null);
      boundaryLoadInFlightRef.current = null;
      boundaryLoadRequestRef.current += 1;
      setBoundaryLoading(null);
      return;
    }

    setResidentStart((current) => Math.min(current, maxStart));
  }, [keep, panels.length]);

  /**
   * Chat khởi tạo với resident window cuối nên đặt native viewport ở đáy một lần.
   * Sau đó paging không ghi scrollTop; browser native anchoring tự xử lý khi
   * transition prepend/remove panel thật.
   */
  useLayoutEffect(() => {
    const viewport = scrollRef.current;
    if (!viewport || initializedRef.current || panels.length === 0) return;

    const expectedResidentStart = Math.max(0, panels.length - keep);
    if (residentStart !== expectedResidentStart) return;

    initializedRef.current = true;
    viewport.scrollTop = Math.max(
      0,
      viewport.scrollHeight - viewport.clientHeight,
    );
    previousScrollTopRef.current = viewport.scrollTop;
    updateAutoScroll(true);
  }, [keep, panels.length, residentStart]);

  /**
   * Content growth never changes the follow flag. Only an actual scroll event
   * can do that. This prevents streamed content from pushing the sentinel out
   * for one frame and accidentally disabling autoscroll.
   */
  useLayoutEffect(() => {
    if (!autoScroll) return;
    if (tailFollowTargetRef.current !== null) return;
    if (pendingResidentStartRef.current !== null) return;
    if (transitionResidentIds !== null) return;
    const viewport = scrollRef.current;
    if (!viewport || !includesLastPanel) return;

    viewport.scrollTop = viewport.scrollHeight;
    previousScrollTopRef.current = viewport.scrollTop;
  }, [autoScroll, bottomPad, entries, includesLastPanel, showThinking]);

  const onScroll = () => {
    const viewport = scrollRef.current;
    if (!viewport || panels.length === 0) return;

    const nextScrollTop = viewport.scrollTop;
    const previousScrollTop = previousScrollTopRef.current;
    previousScrollTopRef.current = nextScrollTop;

    const userScrollInput = userScrollInputRef.current;
    if (userScrollInput) {
      const atConversationBottom = includesLastPanel && isAtBottom(viewport);
      updateAutoScroll(atConversationBottom);

      if (!atConversationBottom && tailFollowTargetRef.current !== null) {
        tailFollowTargetRef.current = null;
      }
    }

    if (pendingResidentStartRef.current !== null) return;
    if (tailFollowTargetRef.current !== null) return;
    if (transitionResidentIds !== null) return;
    if (boundaryLoadInFlightRef.current !== null) return;

    if (!userScrollInput) return;

    const direction: ScrollDirection | null =
      nextScrollTop < previousScrollTop
        ? "up"
        : nextScrollTop > previousScrollTop
          ? "down"
          : null;

    if (direction === null) return;

    const hitTop = nextScrollTop === 0;
    const hitBottom = isAtBottom(viewport);

    if (direction === "up" && hitTop && residentStart > 0) {
      requestBoundaryLoad(
        "up",
        Math.max(0, residentStart - residentStep),
      );
      return;
    }

    if (direction === "down" && hitBottom && residentEnd < panels.length) {
      const maxStart = Math.max(0, panels.length - keep);
      requestBoundaryLoad(
        "down",
        Math.min(maxStart, residentStart + residentStep),
      );
    }
  };

  const renderedPanelIndices = useMemo(
    () => [...residentIds].sort((left, right) => left - right),
    [residentIds],
  );

  return (
    <div className="ct-chat-shell">
      {conversationNavigator !== undefined && conversationNavigator !== false && (
        <div className="ct-chat-shell__navigator">
          <ConversationNavigator {...conversationNavigator} />
        </div>
      )}

      <div
        className="ct-chat-shell__main"
        data-has-navigator={
          conversationNavigator !== undefined && conversationNavigator !== false
            ? "true"
            : "false"
        }
      >
        {notice && (
          <div className="ct-chat__notice" role="status">
            {notice}
          </div>
        )}

        <div
          className="ct-chat"
          ref={scrollRef}
          onScroll={onScroll}
          onWheelCapture={beginTransientUserScrollInput}
          onPointerDownCapture={beginPointerUserScrollInput}
          data-panels={panels.length}
          data-live-from={residentStart}
          data-live-count={residentEnd - residentStart}
          data-mounted-count={residentIds.size}
          data-boundary-loading={boundaryLoading ?? "none"}
          data-auto-scroll={autoScroll ? "true" : "false"}
        >
          <div
            className="ct-chat__total"
            style={{
              height: "auto",
              minHeight: 0,
            }}
          >
            {renderedPanelIndices.map((panelIndex) => {
              const panelEntries = panels[panelIndex];
              if (!panelEntries) return null;

              return (
                <ViewHistory
                  key={`panel-${panelIndex}`}
                  panelIndex={panelIndex}
                  entries={panelEntries}
                  isLastPanel={panelIndex === panels.length - 1}
                  showThinking={showThinking}
                />
              );
            })}

            {includesLastPanel && bottomPad > 0 && (
              <div
                aria-hidden="true"
                style={{
                  height: bottomPad,
                  flexShrink: 0,
                }}
              />
            )}

          </div>
        </div>

        {children}
      </div>
    </div>
  );
}
