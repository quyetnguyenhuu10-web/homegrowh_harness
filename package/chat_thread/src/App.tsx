// Màn hình trắng trống (tạm thời). Style inline để bundle nhúng ra 1 file duy nhất.
import ChatContainer from "./component/chat_container/ChatContainer";
import Composer from "./component/composer/Composer";

// TODO(demo): 500 tin giả có text để test render quanh viewport,
// xóa khi nối tin nhắn thật.
const DEMO_PHRASES = [
  "Ok.",
  "Đã rõ, tiếp đi.",
  "Bạn giải thích lại đoạn code này giúp mình với.",
  "Mình vẫn chưa hiểu chỗ virtualize panel, nói kỹ hơn về cách bù scrollTop khi dịch cửa sổ RAM nhé.",
  "Tóm lại là mỗi 5 tin nhắn gom thành 1 panel con overlay tuyệt đối, các panel liền kề nhau trong panel tổng để scroll được, đúng không bạn?",
];

const DEMO_MESSAGES = Array.from({ length: 500 }, (_, i) => ({
  id: `demo-${i + 1}`,
  role: i % 2 === 0 ? ("user" as const) : ("assistant" as const),
  text: `#${i + 1} ${DEMO_PHRASES[i % DEMO_PHRASES.length]}`,
}));

export default function App() {
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
      <ChatContainer
        messages={DEMO_MESSAGES}
        messagesPerPanel={5}
        maxPanelsInRam={10}
      />
      <Composer />
    </div>
  );
}
