// Test CLI: stream token ra terminal qua API thống nhất.
// Chạy: npm run test:cli -- --provider deepseek --prompt "Xin chào"
//   --provider  openai | deepseek (bắt buộc)
//   --prompt    câu hỏi (bắt buộc, hoặc PIPE qua stdin)
//   --model, --api-key, --base-url, --temperature, --max-tokens (tùy chọn)
import { chatStream, type ProviderName } from "../src/register.js";

function helpText(): string {
  return [
    "Dùng: node ./dist/tests/cli.js --provider <openai|deepseek|ollama> --prompt \"...\" [tùy chọn]",
    "",
    "Tùy chọn:",
    "  --model <tên>        ghi đè model mặc định",
    "  --api-key <key>      ghi đè key trong .env",
    "  --base-url <url>     ghi đè endpoint (hữu ích khi test mock local)",
    "  --temperature <số>   nhiệt độ sampling",
    "  --max-tokens <số>    giới hạn token trả về",
    "  --help               in hướng dẫn này",
    "",
    "Ví dụ:",
    '  npm run test:cli -- --provider deepseek --prompt "2 + 2 bằng mấy?"',
  ].join("\n");
}

function parseArgs(argv: string[]): Record<string, string | true> {
  const out: Record<string, string | true> = {};
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith("--")) continue;
    const key = arg.slice(2);
    const next = argv[i + 1];
    if (next === undefined || next.startsWith("--")) {
      out[key] = true;
    } else {
      out[key] = next;
      i++;
    }
  }
  return out;
}

function readStdin(): Promise<string> {
  return new Promise((resolve) => {
    if (process.stdin.isTTY) {
      resolve("");
      return;
    }
    let data = "";
    process.stdin.setEncoding("utf8");
    process.stdin.on("data", (chunk) => (data += chunk));
    process.stdin.on("end", () => resolve(data.trim()));
  });
}

function fail(message: string): never {
  process.stderr.write(`Lỗi: ${message}\n\n${helpText()}\n`);
  process.exit(1);
}

// Default CHỈ nằm ở test: test luôn gọi API kèm model rõ ràng.
const TEST_DEFAULT_MODEL: Record<ProviderName, string> = {
  openai: "gpt-4o-mini",
  deepseek: "deepseek-chat",
  ollama: "qwen3:4b",
};

async function main(): Promise<void> {
  const args = parseArgs(process.argv.slice(2));

  if (args.help) {
    process.stdout.write(`${helpText()}\n`);
    return;
  }

  const provider = args.provider;
  if (provider !== "openai" && provider !== "deepseek" && provider !== "ollama") {
    fail("--provider phải là openai, deepseek hoặc ollama.");
  }

  let prompt = typeof args.prompt === "string" ? args.prompt : "";
  if (!prompt) prompt = await readStdin();
  if (!prompt) fail("thiếu --prompt (hoặc PIPE nội dung qua stdin).");

  const num = (key: string): number | undefined => {
    const raw = args[key];
    if (typeof raw !== "string") return undefined;
    const value = Number(raw);
    if (!Number.isFinite(value)) fail(`--${key} phải là số.`);
    return value;
  };

  const started = Date.now();
  let events = 0;
  let chars = 0;
  let usage: { totalTokens?: number } = {};

  try {
    for await (const ev of chatStream({
      provider: provider as ProviderName,
      messages: [{ role: "user", content: prompt }],
      model:
        typeof args.model === "string" && args.model
          ? args.model
          : TEST_DEFAULT_MODEL[provider as ProviderName],
      ...(typeof args["api-key"] === "string" ? { apiKey: args["api-key"] } : {}),
      ...(typeof args["base-url"] === "string" ? { baseUrl: args["base-url"] } : {}),
      ...(num("temperature") !== undefined ? { temperature: num("temperature") } : {}),
      ...(num("max-tokens") !== undefined ? { maxTokens: num("max-tokens") } : {}),
    })) {
      events++;
      if (ev.done) break;
      // Token thô ra terminal ngay khi về, không chờ hết response.
      process.stdout.write(ev.delta);
      chars += ev.delta.length;
      if (ev.usage?.totalTokens) usage = ev.usage;
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err);
    process.stderr.write(`\nAPI lỗi: ${message}\n`);
    process.exit(1);
  }

  const elapsed = ((Date.now() - started) / 1000).toFixed(1);
  process.stderr.write(
    `\n— xong: ${events} chunk, ${chars} ký tự, ${elapsed}s` +
      (usage.totalTokens !== undefined ? `, ${usage.totalTokens} tokens` : "") +
      ` [${provider}]\n`,
  );
}

await main();
