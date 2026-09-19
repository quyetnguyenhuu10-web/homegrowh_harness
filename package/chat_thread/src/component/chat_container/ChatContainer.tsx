import {
  useEffect,
  useLayoutEffect,
  useMemo,
  useRef,
  useState,
} from "react";

import { acquireStyleTag, releaseStyleTag } from "../style_tag";

import cssText from "./style.css?inline";

/** Tin nhắn khung (text demo để test render, sau nối nội dung thật). */
export interface PanelMessage {
  id: string;
  role: "user" | "assistant";
  text?: string;
}

export interface ChatContainerProps {
  /** Danh sách tin nhắn theo thứ tự thời gian. */
  messages?: PanelMessage[];

  /** Số tin nhắn trong 1 panel con. */
  messagesPerPanel?: number;

  /** Số panel-content mục tiêu được giữ sống trong RAM. */
  maxPanelsInRam?: number;

  /** Khoảng trống cuối để composer overlay không che tin nhắn. */
  bottomPad?: number;
}

const STYLE_KEY = "chat-container";

const ROW_H = 64;
const ROW_GAP = 32;
const PANEL_PAD = 12;

/** Chỉ đổi resident page khi viewport thật sự chạm biên page đang sống. */
const EDGE_THRESHOLD = 8;

type ScrollDirection = "up" | "down";

function chunk<T>(arr: T[], size: number): T[][] {
  const out: T[][] = [];

  for (let i = 0; i < arr.length; i += size) {
    out.push(arr.slice(i, i + size));
  }

  return out;
}

function getPanelHeight(rowCount: number): number {
  if (rowCount <= 0) return 0;

  return (
    rowCount * ROW_H +
    Math.max(0, rowCount - 1) * ROW_GAP
  );
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
}: ChatContainerProps) {
  useEffect(() => {
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
  const edgeLockRef = useRef<ScrollDirection | null>(null);

  const residentIds = useMemo(
    () =>
      transitionResidentIds ??
      createRangeIds(residentStart, residentEnd),
    [residentEnd, residentStart, transitionResidentIds],
  );

  const beginPageTransition = (nextStart: number): void => {
    if (nextStart === residentStart) return;
    if (pendingResidentStartRef.current !== null) return;

    const nextEnd = Math.min(panels.length, nextStart + keep);
    const union = createRangeIds(residentStart, residentEnd);

    for (let index = nextStart; index < nextEnd; index += 1) {
      union.add(index);
    }

    pendingResidentStartRef.current = nextStart;
    setTransitionResidentIds(union);
  };

  /**
   * Sau khi union page cũ + page mới đã commit thật vào DOM,
   * chuyển ownership sang page mới rồi bỏ content page cũ.
   */
  useLayoutEffect(() => {
    if (transitionResidentIds === null) return;

    const nextStart = pendingResidentStartRef.current;
    if (nextStart === null) return;

    pendingResidentStartRef.current = null;
    setResidentStart(nextStart);
    setTransitionResidentIds(null);
  }, [transitionResidentIds]);

  /**
   * Khi đổi dữ liệu/config, chỉ clamp resident window về range hợp lệ.
   */
  useLayoutEffect(() => {
    const maxStart = Math.max(0, panels.length - keep);

    setResidentStart((current) => Math.min(current, maxStart));
    pendingResidentStartRef.current = null;
    setTransitionResidentIds(null);
    edgeLockRef.current = null;
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
    if (pendingResidentStartRef.current !== null) return;

    const nextScrollTop = viewport.scrollTop;
    const previousScrollTop = previousScrollTopRef.current;
    previousScrollTopRef.current = nextScrollTop;

    const direction: ScrollDirection | null =
      nextScrollTop < previousScrollTop
        ? "up"
        : nextScrollTop > previousScrollTop
          ? "down"
          : null;

    if (direction === null) return;

    const distanceFromTop = nextScrollTop;
    const distanceFromBottom =
      viewport.scrollHeight - nextScrollTop - viewport.clientHeight;

    const nearTop = distanceFromTop <= EDGE_THRESHOLD;
    const nearBottom = distanceFromBottom <= EDGE_THRESHOLD;

    const lockedDirection = edgeLockRef.current;
    if (lockedDirection !== null) {
      const leftLockedEdge =
        lockedDirection === "up" ? !nearTop : !nearBottom;

      if (leftLockedEdge) {
        edgeLockRef.current = null;
      } else {
        return;
      }
    }

    if (direction === "up" && nearTop && residentStart > 0) {
        edgeLockRef.current = "up";
        beginPageTransition(Math.max(0, residentStart - residentStep));
      return;
    }

    if (direction === "down" && nearBottom && residentEnd < panels.length) {
      const maxStart = Math.max(0, panels.length - keep);
      edgeLockRef.current = "down";
      beginPageTransition(
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
    >
      <div
        className="ct-chat__total"
        style={{
          height: "auto",
          minHeight: 0,
        }}
      >
        {renderedPanelIndices.map((panelIndex, localIndex) => {
          const rows = panels[panelIndex];
          if (!rows) return null;

          const shellHeight = getPanelHeight(rows.length);
          const isLastRenderedPanel =
            localIndex === renderedPanelIndices.length - 1;

          return (
            <section
              key={`panel-${panelIndex}`}
              className="ct-chat__panel"
              data-panel-index={panelIndex}
              data-panel-mounted="true"
              style={{
                position: "relative",
                display: "flex",
                flexDirection: "column",
                width: "100%",
                height: shellHeight,
                minHeight: shellHeight,
                boxSizing: "border-box",
                padding: `0 ${PANEL_PAD}px`,
                gap: ROW_GAP,
                marginBottom: !isLastRenderedPanel ? ROW_GAP : 0,
                overflow: "hidden",
              }}
            >
              {rows.map((m) => (
                <div
                  key={m.id}
                  className={`ct-chat__msg ct-chat__msg--${m.role}`}
                  data-msg-id={m.id}
                  style={{
                    height: ROW_H,
                    minHeight: ROW_H,
                    flexShrink: 0,
                  }}
                >
                  <div
                    className="ct-chat__bubble"
                    style={{
                      width: m.role === "user" ? "55%" : "70%",
                      height: "100%",
                      boxSizing: "border-box",
                      padding: "10px 14px",
                      overflow: "hidden",
                      fontSize: 13,
                      lineHeight: "20px",
                      color: "#111827",
                      whiteSpace: "nowrap",
                      textOverflow: "ellipsis",
                    }}
                  >
                    {m.text ?? ""}
                  </div>
                </div>
              ))}
            </section>
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
