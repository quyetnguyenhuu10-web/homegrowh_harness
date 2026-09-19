import {
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";

import { acquireStyleTag, releaseStyleTag } from "../style_tag";
import ViewHistory, {
  type ViewHistoryMessage,
} from "../view_history";

import cssText from "./style.css?inline";

export type PanelMessage = ViewHistoryMessage;

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
  /** Danh sách tin nhắn theo thứ tự thời gian. */
  messages?: readonly PanelMessage[];

  /** Số tin nhắn trong 1 panel con. */
  messagesPerPanel?: number;

  /** Số panel-content mục tiêu được giữ sống trong RAM. */
  maxPanelsInRam?: number;

  /** Khoảng trống cuối để composer overlay không che tin nhắn. */
  bottomPad?: number;

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

/** Sai số 1px cho scrollTop/scrollHeight có giá trị lẻ theo device scale. */
const EDGE_EPSILON = 1;

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
  messages = [],
  messagesPerPanel = 5,
  maxPanelsInRam = 10,
  bottomPad = 96,
  onBoundaryLoad,
}: ChatContainerProps) {
  useLayoutEffect(() => {
    acquireStyleTag(STYLE_KEY, cssText);

    return () => releaseStyleTag(STYLE_KEY);
  }, []);

  const perPanel = Math.max(1, Math.floor(messagesPerPanel));
  const keep = Math.max(1, Math.floor(maxPanelsInRam));
  const residentStep = Math.max(1, Math.floor(keep / 2));

  const panels = useMemo(
    () => chunk(messages, perPanel),
    [messages, perPanel],
  );

  const scrollRef = useRef<HTMLDivElement>(null);
  const previousScrollTopRef = useRef(0);
  const initializedRef = useRef(false);

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
  }, [residentStart, transitionResidentIds]);

  /**
   * Khi đổi dữ liệu/config, chỉ clamp resident window về range hợp lệ.
   */
  useLayoutEffect(() => {
    const maxStart = Math.max(0, panels.length - keep);

    setResidentStart((current) => Math.min(current, maxStart));
    pendingResidentStartRef.current = null;
    topPrependAnchorRef.current = null;
    setTransitionResidentIds(null);
    boundaryLoadInFlightRef.current = null;
    boundaryLoadRequestRef.current += 1;
    setBoundaryLoading(null);
  }, [keep, panels.length]);

  /**
   * Chat khởi tạo với resident window cuối nên đặt native viewport ở đáy một lần.
   * Sau đó paging không ghi scrollTop; browser native anchoring tự xử lý khi
   * transition prepend/remove panel thật.
   */
  useLayoutEffect(() => {
    const viewport = scrollRef.current;
    if (!viewport || initializedRef.current || panels.length === 0) return;

    initializedRef.current = true;
    viewport.scrollTop = Math.max(
      0,
      viewport.scrollHeight - viewport.clientHeight,
    );
    previousScrollTopRef.current = viewport.scrollTop;
  }, [panels.length]);

  const onScroll = () => {
    const viewport = scrollRef.current;
    if (!viewport || panels.length === 0) return;

    const nextScrollTop = viewport.scrollTop;
    const previousScrollTop = previousScrollTopRef.current;
    previousScrollTopRef.current = nextScrollTop;

    if (pendingResidentStartRef.current !== null) return;
    if (boundaryLoadInFlightRef.current !== null) return;

    const direction: ScrollDirection | null =
      nextScrollTop < previousScrollTop
        ? "up"
        : nextScrollTop > previousScrollTop
          ? "down"
          : null;

    if (direction === null) return;

    /*
     * Không có geometry lịch sử giả: scrollHeight hiện tại chỉ gồm panel đang
     * resident trong RAM. Vì vậy 0 và maxScrollTop chính là trần/sàn vật lý của
     * resident DOM, không cần wheel intent, timer hay anchor bookkeeping.
     */
    const maxScrollTop = Math.max(
      0,
      viewport.scrollHeight - viewport.clientHeight,
    );
    const hitTop = nextScrollTop <= EDGE_EPSILON;
    const hitBottom = nextScrollTop >= maxScrollTop - EDGE_EPSILON;

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

  const includesLastPanel =
    panels.length > 0 && residentIds.has(panels.length - 1);

  return (
    <div
      className="ct-chat"
      ref={scrollRef}
      onScroll={onScroll}
      data-panels={panels.length}
      data-live-from={residentStart}
      data-live-count={residentEnd - residentStart}
      data-mounted-count={residentIds.size}
      data-boundary-loading={boundaryLoading ?? "none"}
    >
      <div
        className="ct-chat__total"
        style={{
          height: "auto",
          minHeight: 0,
        }}
      >
        {renderedPanelIndices.map((panelIndex) => {
          const rows = panels[panelIndex];
          if (!rows) return null;

          return (
            <ViewHistory
              key={`panel-${panelIndex}`}
              panelIndex={panelIndex}
              messages={rows}
              isLastPanel={panelIndex === panels.length - 1}
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
  );
}
