import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import type { AuthSession } from "@shared/gfn";
import { cookieSessionInternals } from "./sessionStore";
import type { WebAuthSessionSnapshot } from "./webAuth";

function snapshot(userId: string): WebAuthSessionSnapshot {
  const auth: AuthSession = {
    provider: {
      idpId: "nvidia",
      code: "NVIDIA",
      displayName: "NVIDIA",
      streamingServiceUrl: "https://example.invalid/",
      priority: 0,
    },
    user: {
      userId,
      displayName: `Player ${userId}`,
      membershipTier: "FREE",
    },
    tokens: {
      accessToken: `access-${userId}`,
      refreshToken: `refresh-${userId}`,
      expiresAt: Date.now() + 60_000,
    },
  };
  return { version: 1, auth, attempts: [], activeSessionIds: [`stream-${userId}`] };
}

describe("encrypted cookie sessions", () => {
  it("isolates profiles and stream ownership across many visitors", () => {
    const encrypted = Array.from({ length: 500 }, (_, index) =>
      cookieSessionInternals.encrypt(snapshot(`user-${index}`)),
    );
    expect(new Set(encrypted).size).toBe(500);

    for (let index = 0; index < encrypted.length; index += 1) {
      const restored = cookieSessionInternals.decrypt(encrypted[index]);
      expect(restored?.auth?.user.userId).toBe(`user-${index}`);
      expect(restored?.activeSessionIds).toEqual([`stream-user-${index}`]);
    }
  });

  it("rejects a modified cookie instead of accepting forged profile state", () => {
    const encrypted = cookieSessionInternals.encrypt(snapshot("alice"));
    const replacement = encrypted.endsWith("A") ? "B" : "A";
    const tampered = `${encrypted.slice(0, -1)}${replacement}`;
    expect(cookieSessionInternals.decrypt(tampered)).toBeNull();
  });

  it("uses a fresh nonce when encrypting identical profiles", () => {
    const state = snapshot("same-user");
    expect(cookieSessionInternals.encrypt(state)).not.toBe(cookieSessionInternals.encrypt(state));
  });
});

/**
 * The desktop backend keeps an encrypted copy of the signed-in session in the
 * app-data directory, because the bundled WebView does not reliably return the
 * session cookie between launches — which is exactly why the installed app used
 * to demand a fresh NVIDIA login every single time.
 */
describe("desktop session mirror", () => {
  let dataDir = "";

  beforeEach(() => {
    dataDir = mkdtempSync(join(tmpdir(), "opennow-session-"));
  });

  afterEach(() => {
    delete process.env.OPENNOW_DATA_DIR;
    delete process.env.OPENNOW_SESSION_SECRET_FILE;
    delete process.env.OPENNOW_PARENT_PID;
    delete process.env.OPENNOW_BIND_HOST;
    vi.resetModules();
    rmSync(dataDir, { recursive: true, force: true });
  });

  async function loadStore(options: { desktop: boolean } = { desktop: true }) {
    if (options.desktop) process.env.OPENNOW_DATA_DIR = dataDir;
    process.env.OPENNOW_SESSION_SECRET_FILE = join(dataDir, "session-secret");
    vi.resetModules();
    const store = await import("./sessionStore");
    return store.cookieSessionInternals;
  }

  function cookieHeader(internals: typeof cookieSessionInternals, state: WebAuthSessionSnapshot): never {
    return {
      headers: { cookie: `opennow_state_0=${encodeURIComponent(internals.encrypt(state))}` },
      protocol: "http",
    } as never;
  }

  function cookielessRequest(): never {
    return { headers: {}, protocol: "http" } as never;
  }

  function fakeResponse(): never {
    const written: Array<[string, string]> = [];
    return {
      written,
      locals: {},
      cookie(name: string, value: string) {
        written.push([name, value]);
        return this;
      },
      clearCookie(name: string) {
        written.push([name, ""]);
        return this;
      },
    } as never;
  }

  it("restores the signed-in session when the WebView returns no cookie", async () => {
    const internals = await loadStore();
    const signedIn = internals.load(cookieHeader(internals, snapshot("desktop-user")));
    expect(signedIn.publicSession()?.user.userId).toBe("desktop-user");

    signedIn.markDirty();
    internals.persist(cookieHeader(internals, snapshot("desktop-user")), fakeResponse(), signedIn);
    expect(internals.readMirror()?.auth?.user.userId).toBe("desktop-user");

    // Next launch: empty cookie jar, same install.
    const restored = internals.load(cookielessRequest());
    expect(restored.publicSession()?.user.userId).toBe("desktop-user");
    expect(restored.ownsActiveSession("stream-desktop-user")).toBe(true);
    // Dirty on arrival so the response re-issues the cookie for this browser.
    expect(restored.isDirty()).toBe(true);
  });

  it("drops the mirror on logout so a shared machine cannot resume the session", async () => {
    const internals = await loadStore();
    const signedIn = internals.load(cookieHeader(internals, snapshot("desktop-user")));
    signedIn.markDirty();
    internals.persist(cookielessRequest(), fakeResponse(), signedIn);
    expect(internals.readMirror()).not.toBeNull();

    signedIn.logout();
    internals.persist(cookielessRequest(), fakeResponse(), signedIn);
    expect(internals.readMirror()).toBeNull();
    expect(internals.load(cookielessRequest()).publicSession()).toBeNull();
  });

  it("keeps a session that outgrows the cookie budget alive on disk", async () => {
    const internals = await loadStore();
    const oversized = snapshot("desktop-user");
    // Incompressible filler: brotli cannot shrink random bytes, so the payload
    // genuinely exceeds the 4-chunk cookie budget.
    oversized.auth!.tokens.accessToken = Array.from(
      { length: 24_000 },
      () => "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"[Math.floor(Math.random() * 62)],
    ).join("");

    const session = internals.load(cookieHeader(internals, oversized));
    session.markDirty();
    expect(() => internals.persist(cookielessRequest(), fakeResponse(), session)).not.toThrow();
    expect(internals.readMirror()?.auth?.user.userId).toBe("desktop-user");
    expect(internals.load(cookielessRequest()).publicSession()?.user.userId).toBe("desktop-user");
  });

  it("never mirrors sessions on a hosted multi-user deployment", async () => {
    process.env.OPENNOW_BIND_HOST = "0.0.0.0";
    const internals = await loadStore({ desktop: false });
    const visitor = internals.load(cookieHeader(internals, snapshot("visitor-a")));
    visitor.markDirty();
    internals.persist(cookielessRequest(), fakeResponse(), visitor);

    expect(internals.readMirror()).toBeNull();
    // A second, cookie-less visitor must not inherit the first one's session.
    expect(internals.load(cookielessRequest()).publicSession()).toBeNull();
  });
});
