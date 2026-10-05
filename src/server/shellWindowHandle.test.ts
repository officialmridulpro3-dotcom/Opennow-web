import { mkdtempSync, rmSync, writeFileSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it } from "vitest";

import { readShellWindowHandle } from "./shellWindowHandle";

/**
 * The breadcrumb is the IPC-free path for the app window's HWND. It must be
 * strict: a handle pointing at a recycled or foreign window would make the
 * engine reparent its video surface into an unrelated window.
 */
describe("shellWindowHandle", () => {
  let dataDir: string;
  const originalDataDir = process.env.OPENNOW_DATA_DIR;

  const write = (value: unknown): void => {
    mkdirSync(dataDir, { recursive: true });
    writeFileSync(
      join(dataDir, "window-handle.json"),
      typeof value === "string" ? value : JSON.stringify(value),
    );
  };

  beforeEach(() => {
    dataDir = mkdtempSync(join(tmpdir(), "opennow-handle-"));
    process.env.OPENNOW_DATA_DIR = dataDir;
  });

  afterEach(() => {
    if (originalDataDir === undefined) delete process.env.OPENNOW_DATA_DIR;
    else process.env.OPENNOW_DATA_DIR = originalDataDir;
    rmSync(dataDir, { recursive: true, force: true });
  });

  it("accepts a fresh handle written by a live process", () => {
    const now = Date.now();
    write({ handle: "132456", pid: process.pid, writtenAtMs: now });
    expect(readShellWindowHandle(now)).toEqual({
      handle: "132456",
      pid: process.pid,
      writtenAtMs: now,
    });
  });

  it("has no handle without a breadcrumb", () => {
    expect(readShellWindowHandle()).toBeNull();
  });

  it("rejects malformed and zero handles", () => {
    write({ handle: "0x1F2", pid: process.pid, writtenAtMs: Date.now() });
    expect(readShellWindowHandle()).toBeNull();
    write({ handle: "0", pid: process.pid, writtenAtMs: Date.now() });
    expect(readShellWindowHandle()).toBeNull();
    write("not json");
    expect(readShellWindowHandle()).toBeNull();
  });

  it("rejects a stale breadcrumb instead of trusting a recycled HWND", () => {
    const now = Date.now();
    write({ handle: "132456", pid: process.pid, writtenAtMs: now - 13 * 60 * 60 * 1000 });
    expect(readShellWindowHandle(now)).toBeNull();
  });

  it("rejects a breadcrumb whose shell process is gone", () => {
    // PID 0x7FFFFFFF is not assignable on any supported platform.
    write({ handle: "132456", pid: 0x7fffffff, writtenAtMs: Date.now() });
    expect(readShellWindowHandle()).toBeNull();
  });

  it("rejects a timestamp from the future (clock skew guard)", () => {
    const now = Date.now();
    write({ handle: "132456", pid: process.pid, writtenAtMs: now + 5 * 60 * 1000 });
    expect(readShellWindowHandle(now)).toBeNull();
  });
});
