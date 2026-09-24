import { toolDefinition } from "../tool_definitions.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import {
  readPositiveInteger,
  requireArguments,
  requireString,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";

export const WEBFETCH_TOOL_NAME = "webfetch";

const MAX_WEBFETCH_BYTES = 5 * 1024 * 1024;
const DEFAULT_WEBFETCH_TIMEOUT_SECONDS = 30;
const MAX_WEBFETCH_TIMEOUT_SECONDS = 120;

export const webfetchToolDefinition = toolDefinition(WEBFETCH_TOOL_NAME);

export async function executeWebfetchToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, WEBFETCH_TOOL_NAME);
  const url = requireString(args.url, "url");
  if (!url.startsWith("http://") && !url.startsWith("https://")) {
    throw new Error("URL must start with http:// or https://");
  }

  const format =
    args.format === undefined ? "markdown" : requireString(args.format, "format");
  if (format !== "markdown" && format !== "text" && format !== "html") {
    throw new Error('format must be "markdown", "text", or "html"');
  }

  const timeoutSeconds = Math.min(
    readPositiveInteger(args.timeout, DEFAULT_WEBFETCH_TIMEOUT_SECONDS),
    MAX_WEBFETCH_TIMEOUT_SECONDS,
  );
  const controller = new AbortController();
  let timedOut = false;
  const timer = setTimeout(() => {
    timedOut = true;
    controller.abort();
  }, timeoutSeconds * 1000);
  const onAbort = (): void => controller.abort();
  if (context.signal?.aborted) onAbort();
  else context.signal?.addEventListener("abort", onAbort, { once: true });

  try {
    const response = await fetch(url, {
      method: "GET",
      headers: {
        "User-Agent":
          "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/143 Safari/537.36",
        Accept:
          format === "html"
            ? "text/html;q=1.0, application/xhtml+xml;q=0.9, */*;q=0.1"
            : format === "text"
              ? "text/plain;q=1.0, text/html;q=0.8, */*;q=0.1"
              : "text/markdown;q=1.0, text/plain;q=0.8, text/html;q=0.7, */*;q=0.1",
        "Accept-Language": "en-US,en;q=0.9",
      },
      signal: controller.signal,
    });
    if (!response.ok) {
      throw new Error(
        `HTTP ${response.status}: ${response.statusText || "request failed"}`,
      );
    }

    const declaredLength = Number(response.headers.get("content-length") ?? 0);
    if (declaredLength > MAX_WEBFETCH_BYTES) {
      throw new Error("Response too large (exceeds 5MB limit)");
    }

    const buffer = await response.arrayBuffer();
    if (buffer.byteLength > MAX_WEBFETCH_BYTES) {
      throw new Error("Response too large (exceeds 5MB limit)");
    }

    const contentType = response.headers.get("content-type") ?? "";
    const mime = contentType.split(";")[0]?.trim().toLowerCase() ?? "";
    const title = `${url} (${contentType})`;

    if (mime.startsWith("image/")) {
      return resultMessage(toolCall, {
        title,
        output: "Image fetched successfully",
        metadata: { mime, bytes: buffer.byteLength },
      });
    }

    const content = new TextDecoder().decode(buffer);
    const output =
      format === "html"
        ? content
        : contentType.includes("text/html")
          ? format === "markdown"
            ? convertHtmlToMarkdown(content)
            : extractTextFromHtml(content)
          : content;

    return resultMessage(toolCall, {
      title,
      output,
      metadata: { mime, bytes: buffer.byteLength },
    });
  } catch (error) {
    if (timedOut) {
      throw new Error(`Request timed out after ${timeoutSeconds} seconds`);
    }
    if (context.signal?.aborted) throw new Error("Request aborted");
    throw error;
  } finally {
    clearTimeout(timer);
    context.signal?.removeEventListener("abort", onAbort);
  }
}

function extractTextFromHtml(html: string): string {
  return html
    .replace(/<script[\s\S]*?<\/script>/gi, "")
    .replace(/<style[\s\S]*?<\/style>/gi, "")
    .replace(/<noscript[\s\S]*?<\/noscript>/gi, "")
    .replace(/<[^>]+>/g, " ")
    .replace(/&nbsp;/gi, " ")
    .replace(/&amp;/gi, "&")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/&quot;/gi, '"')
    .replace(/&#39;/gi, "'")
    .replace(/\s+/g, " ")
    .trim();
}

function convertHtmlToMarkdown(html: string): string {
  const converted = html
    .replace(/<script[\s\S]*?<\/script>/gi, "")
    .replace(/<style[\s\S]*?<\/style>/gi, "")
    .replace(/<meta[\s\S]*?>/gi, "")
    .replace(/<link[\s\S]*?>/gi, "")
    .replace(/<h1[^>]*>([\s\S]*?)<\/h1>/gi, "\n# $1\n")
    .replace(/<h2[^>]*>([\s\S]*?)<\/h2>/gi, "\n## $1\n")
    .replace(/<h3[^>]*>([\s\S]*?)<\/h3>/gi, "\n### $1\n")
    .replace(/<h4[^>]*>([\s\S]*?)<\/h4>/gi, "\n#### $1\n")
    .replace(/<br\s*\/?>/gi, "\n")
    .replace(/<\/p>/gi, "\n\n")
    .replace(/<li[^>]*>/gi, "\n- ")
    .replace(/<\/li>/gi, "")
    .replace(/<code[^>]*>([\s\S]*?)<\/code>/gi, "`$1`")
    .replace(
      /<a\s+[^>]*href=["']([^"']+)["'][^>]*>([\s\S]*?)<\/a>/gi,
      "[$2]($1)",
    )
    .replace(/<[^>]+>/g, " ");

  return converted
    .replace(/&nbsp;/gi, " ")
    .replace(/&amp;/gi, "&")
    .replace(/&lt;/gi, "<")
    .replace(/&gt;/gi, ">")
    .replace(/&quot;/gi, '"')
    .replace(/&#39;/gi, "'")
    .replace(/[ \t]+/g, " ")
    .replace(/ *\n */g, "\n")
    .replace(/\n{3,}/g, "\n\n")
    .trim();
}
