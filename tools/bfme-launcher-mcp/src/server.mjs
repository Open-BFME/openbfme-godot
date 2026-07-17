#!/usr/bin/env node
import readline from "node:readline";
import {
  configureLauncher,
  getLauncherStatus,
  launchBfme2,
  terminateBfmeTree,
} from "./core.mjs";

const tools = [
  {
    name: "get_launcher_status",
    description: "Inspect the fixed BFME All In One Launcher installation, compatibility settings, and path-verified launcher/game processes.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: true, openWorldHint: false },
  },
  {
    name: "configure_launcher",
    description: "Atomically change only the BFME2 affinity-1 and windowed-mode compatibility flags in the fixed launcher settings file.",
    inputSchema: {
      type: "object",
      properties: {
        launch_with_affinity_1: { type: "boolean" },
        windowed_bfme2: { type: "boolean" },
      },
      additionalProperties: false,
      minProperties: 1,
    },
    annotations: { readOnlyHint: false, destructiveHint: false, openWorldHint: false },
  },
  {
    name: "launch_bfme2",
    description: "Request launch of only the fixed BFME All In One Launcher. Returns immediately and explicitly reports the Windows UAC consent boundary.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: false, destructiveHint: false, openWorldHint: true },
  },
  {
    name: "terminate_bfme_tree",
    description: "Stop only path-verified BFME launcher, wrapper, and game processes; if elevation is needed, request UAC and revalidate identity after approval.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: false, destructiveHint: true, openWorldHint: false },
  },
];

function textResult(value, isError = false) {
  return {
    content: [{ type: "text", text: JSON.stringify(value, null, 2) }],
    structuredContent: value,
    ...(isError ? { isError: true } : {}),
  };
}

const instructions = "Operate only the fixed local BFME All In One Launcher and its path-verified process family. Never treat a launch or elevation request as completed: poll get_launcher_status after the user handles Windows UAC. Do not use this server to approve oracle evidence.";
const protocolVersion = "2025-06-18";

async function callTool(params) {
  const args = params.arguments ?? {};
  try {
    switch (params.name) {
      case "get_launcher_status":
        if (Object.keys(args).length) throw new Error("get_launcher_status accepts no arguments.");
        return textResult(await getLauncherStatus());
      case "configure_launcher":
        return textResult(await configureLauncher(args));
      case "launch_bfme2":
        if (Object.keys(args).length) throw new Error("launch_bfme2 accepts no arguments.");
        return textResult(await launchBfme2());
      case "terminate_bfme_tree":
        if (Object.keys(args).length) throw new Error("terminate_bfme_tree accepts no arguments.");
        return textResult(await terminateBfmeTree());
      default:
        return textResult({ error: `Unknown tool: ${params.name}` }, true);
    }
  } catch (error) {
    return textResult({ error: error instanceof Error ? error.message : String(error) }, true);
  }
}

function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

async function dispatch(message) {
  if (!Object.hasOwn(message, "id")) return;
  try {
    let result;
    switch (message.method) {
      case "initialize":
        result = {
          protocolVersion,
          capabilities: { tools: {} },
          serverInfo: { name: "openbfme-launcher", version: "0.1.0" },
          instructions,
        };
        break;
      case "ping":
        result = {};
        break;
      case "tools/list":
        result = { tools };
        break;
      case "tools/call":
        result = await callTool(message.params ?? {});
        break;
      default:
        write({ jsonrpc: "2.0", id: message.id, error: { code: -32601, message: `Method not found: ${message.method}` } });
        return;
    }
    write({ jsonrpc: "2.0", id: message.id, result });
  } catch (error) {
    write({ jsonrpc: "2.0", id: message.id, error: { code: -32603, message: error instanceof Error ? error.message : String(error) } });
  }
}

const lines = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
for await (const line of lines) {
  if (!line.trim()) continue;
  try {
    await dispatch(JSON.parse(line.replace(/^\uFEFF/, "")));
  } catch (error) {
    write({ jsonrpc: "2.0", id: null, error: { code: -32700, message: error instanceof Error ? error.message : String(error) } });
  }
}
