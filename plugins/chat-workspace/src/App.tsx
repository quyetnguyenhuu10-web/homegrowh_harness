import { useRef, useState } from "react";

import ChatContainer, { PromptBar } from "./chat_container/renderer";
import {
  runDefaultSession,
  type SessionClientBridge,
  type SessionRegisterConfig,
} from "./session";

export interface AppProps {
  sessionClient?: SessionClientBridge;
  createSessionConfig?: (text: string) => SessionRegisterConfig;
}

export default function App({
  sessionClient,
  createSessionConfig,
}: AppProps) {
  const runningRef = useRef(false);
  const [notice, setNotice] = useState<string | null>(null);

  const send = async (text: string): Promise<void> => {
    if (!sessionClient || !createSessionConfig || runningRef.current) return;

    runningRef.current = true;
    setNotice(null);
    try {
      await runDefaultSession(sessionClient, createSessionConfig(text));
    } catch (error) {
      setNotice(error instanceof Error ? error.message : String(error));
    } finally {
      runningRef.current = false;
    }
  };

  return (
    <div
      style={{
        position: "relative",
        width: "100%",
        height: "100%",
        margin: 0,
        padding: 0,
        overflow: "hidden",
        background: "#ffffff",
      }}
    >
      <ChatContainer bottomPad={168} notice={notice}>
        <div
          style={{
            position: "absolute",
            left: "50%",
            bottom: 18,
            zIndex: 40,
            width: "min(760px, calc(100% - 32px))",
            transform: "translateX(-50%)",
            pointerEvents: "none",
          }}
        >
          <div style={{ pointerEvents: "auto" }}>
            <PromptBar demo={false} onSend={send} />
          </div>
        </div>
      </ChatContainer>
    </div>
  );
}
