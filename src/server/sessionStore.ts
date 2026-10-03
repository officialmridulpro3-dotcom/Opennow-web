import {
  createCipheriv,
  createDecipheriv,
  createHash,
  randomBytes,
} from "node:crypto";
import { chmodSync, readFileSync, writeFileSync } from "node:fs";
import type { IncomingMessage } from "node:http";
import {
  brotliCompressSync,
  brotliDecompressSync,
  constants as zlibConstants,
} from "node:zlib";
import type { NextFunction, Request, RequestHandler, Response } from "express";
import onHeaders from "on-headers";

import {
  appDataPath,
  ensureParentDir,
  isSingleUserDesktopRuntime,
  readAppDataFile,
  removeAppDataFile,
  writeAppDataFile,
} from "./appData";
import { WebAuthSession, type WebAuthSessionSnapshot } from "./webAuth";

const COOKIE_PREFIX = "opennow_state_";
const COOKIE_PATH = "/api";
const COOKIE_MAX_AGE_MS = 30 * 24 * 60 * 60 * 1000;
const COOKIE_CHUNK_SIZE = 3_500;
const MAX_COOKIE_CHUNKS = 4;
const COOKIE_VERSION = 1;

const configuredSecret = process.env.SESSION_SECRET?.trim();
if (configuredSecret && configuredSecret.length < 32) {
  throw new Error("SESSION_SECRET must contain at least 32 characters.");
}

const LEGACY_SESSION_SECRET_FILE = ".opennow-session-secret";

/**
 * Where the fallback secret lives.
 *
 * It used to be `.opennow-session-secret` relative to the process cwd. That
 * works for `npm run dev` in the repo root, but the installed desktop backend
 * is spawned by a launcher whose cwd is an implementation detail: any launch
 * that resolved a different cwd minted a *new* secret, every stored session
 * cookie became undecryptable, and the user was signed out again. The secret
 * now lives in the per-installation app-data directory, which is stable for the
 * lifetime of the install (an explicit OPENNOW_SESSION_SECRET_FILE still wins).
 */
function resolveSessionSecretFile(): string {
  const configured = process.env.OPENNOW_SESSION_SECRET_FILE?.trim();
  if (configured) return configured;
  return appDataPath("session-secret");
}

const SESSION_SECRET_FILE = resolveSessionSecretFile();

/**
 * Load or create a file-backed fallback secret so visitor sessions survive
 * server restarts even when SESSION_SECRET is not configured. The file is
 * created with owner-only permissions and should never be committed.
 */
function loadOrCreatePersistedSessionSecret(): string {
  try {
    const existing = readFileSync(SESSION_SECRET_FILE, "utf8").trim();
    if (existing.length >= 32) {
      return existing;
    }
  } catch {
    // Create below.
  }

  // Adopt a secret written by an older build next to the process cwd, so
  // browsers still holding cookies encrypted with it stay signed in.
  if (SESSION_SECRET_FILE !== LEGACY_SESSION_SECRET_FILE) {
    try {
      const legacy = readFileSync(LEGACY_SESSION_SECRET_FILE, "utf8").trim();
      if (legacy.length >= 32) {
        ensureParentDir(SESSION_SECRET_FILE);
        writeFileSync(SESSION_SECRET_FILE, `${legacy}\n`, { mode: 0o600 });
        chmodSync(SESSION_SECRET_FILE, 0o600);
        return legacy;
      }
    } catch {
      // No legacy secret to adopt; generate a fresh one below.
    }
  }

  const generated = randomBytes(32).toString("base64url");
  try {
    ensureParentDir(SESSION_SECRET_FILE);
    writeFileSync(SESSION_SECRET_FILE, `${generated}\n`, { flag: "wx", mode: 0o600 });
    chmodSync(SESSION_SECRET_FILE, 0o600);
    if (process.env.NODE_ENV === "production") {
      console.warn(
        `[Session] SESSION_SECRET is not set; wrote a persistent generated secret to ${SESSION_SECRET_FILE}. ` +
          "Set SESSION_SECRET explicitly in production so every instance shares the same secret.",
      );
    }
  } catch (error) {
    console.warn(
      `[Session] Could not persist the session secret to ${SESSION_SECRET_FILE}; ` +
        `visitors will be signed out on every server restart. (${String(error)})`,
    );
  }
  return generated;
}

const sessionSecret = configuredSecret || loadOrCreatePersistedSessionSecret();
const encryptionKey = createHash("sha256").update(sessionSecret).digest();

function parseCookies(value: string | undefined): Record<string, string> {
  return Object.fromEntries((value ?? "").split(";").flatMap((item) => {
    const index = item.indexOf("=");
    if (index < 1) return [];
    const name = item.slice(0, index).trim();
    const rawValue = item.slice(index + 1).trim();
    try {
      return [[name, decodeURIComponent(rawValue)]];
    } catch {
      return [];
    }
  }));
}

function encryptedCookieValue(snapshot: WebAuthSessionSnapshot): string {
  const compressed = brotliCompressSync(Buffer.from(JSON.stringify(snapshot), "utf8"), {
    params: {
      [zlibConstants.BROTLI_PARAM_QUALITY]: 5,
      [zlibConstants.BROTLI_PARAM_SIZE_HINT]: 4_096,
    },
  });
  const nonce = randomBytes(12);
  const cipher = createCipheriv("aes-256-gcm", encryptionKey, nonce);
  cipher.setAAD(Buffer.from(`OpenNOW-Web:${COOKIE_VERSION}`));
  const encrypted = Buffer.concat([cipher.update(compressed), cipher.final()]);
  const authenticationTag = cipher.getAuthTag();
  return Buffer.concat([Buffer.from([COOKIE_VERSION]), nonce, authenticationTag, encrypted]).toString("base64url");
}

function decryptCookieValue(value: string): WebAuthSessionSnapshot | null {
  try {
    const payload = Buffer.from(value, "base64url");
    if (payload.length < 30 || payload[0] !== COOKIE_VERSION) return null;
    const nonce = payload.subarray(1, 13);
    const authenticationTag = payload.subarray(13, 29);
    const encrypted = payload.subarray(29);
    const decipher = createDecipheriv("aes-256-gcm", encryptionKey, nonce);
    decipher.setAAD(Buffer.from(`OpenNOW-Web:${COOKIE_VERSION}`));
    decipher.setAuthTag(authenticationTag);
    const compressed = Buffer.concat([decipher.update(encrypted), decipher.final()]);
    const parsed = JSON.parse(brotliDecompressSync(compressed).toString("utf8")) as WebAuthSessionSnapshot;
    if (parsed.version !== 1 || !Array.isArray(parsed.attempts) || !Array.isArray(parsed.activeSessionIds)) return null;
    return parsed;
  } catch {
    return null;
  }
}

function joinedCookieValue(request: Pick<IncomingMessage, "headers">): string | null {
  const cookies = parseCookies(request.headers.cookie);
  const chunks: string[] = [];
  for (let index = 0; index < MAX_COOKIE_CHUNKS; index += 1) {
    const chunk = cookies[`${COOKIE_PREFIX}${index}`];
    if (!chunk) break;
    chunks.push(chunk);
  }
  return chunks.length > 0 ? chunks.join("") : null;
}

const SESSION_MIRROR_FILE = "session-mirror.bin";

/**
 * Encrypted copy of the signed-in session, kept in the app-data directory.
 *
 * Written only for the single-user desktop backend. A hosted deployment serves
 * many visitors from one process, where a shared on-disk session would leak one
 * visitor's tokens into the next request — so the mirror stays off there and
 * cookies remain the only store.
 */
function readSessionMirror(): WebAuthSessionSnapshot | null {
  if (!isSingleUserDesktopRuntime()) return null;
  const encrypted = readAppDataFile(SESSION_MIRROR_FILE)?.trim();
  if (!encrypted) return null;
  const snapshot = decryptCookieValue(encrypted);
  // A logged-out mirror is not a session; treat it as absent.
  return snapshot?.auth ? snapshot : null;
}

function writeSessionMirror(session: WebAuthSession): void {
  if (!isSingleUserDesktopRuntime()) return;
  try {
    const snapshot = session.toSnapshot();
    if (!snapshot.auth) {
      removeAppDataFile(SESSION_MIRROR_FILE);
      return;
    }
    writeAppDataFile(SESSION_MIRROR_FILE, encryptedCookieValue(snapshot));
  } catch (error) {
    console.warn(`[Session] Could not mirror the desktop session to disk: ${String(error)}`);
  }
}

function loadCookieSession(request: Pick<IncomingMessage, "headers">): WebAuthSession {
  const encrypted = joinedCookieValue(request);
  const snapshot = encrypted ? decryptCookieValue(encrypted) : null;
  if (snapshot) return WebAuthSession.fromSnapshot(snapshot);

  // Desktop fallback: the bundled WebView does not always hand the session
  // cookie back (profile resets after an update, cookie eviction, or a payload
  // that outgrew the cookie budget). The single-user backend keeps an encrypted
  // mirror of the signed-in session in the app-data directory, so a missing
  // cookie no longer means "sign in again".
  const mirrored = readSessionMirror();
  if (!mirrored) return new WebAuthSession();
  const restored = WebAuthSession.fromSnapshot(mirrored);
  // Dirty on arrival: the response re-issues the cookie for this browser so the
  // mirror stays a fallback rather than the primary path.
  restored.markDirty();
  return restored;
}

/**
 * Whether the session cookies may carry the Secure flag. Secure cookies are
 * silently dropped by browsers on plain-HTTP origins — the desktop backend
 * always serves plain HTTP on loopback (with NODE_ENV=production), so tying
 * the flag to NODE_ENV alone signed desktop users out on every launch. The
 * payload stays AES-256-GCM encrypted either way; Secure is only hardening.
 */
function resolveCookieSecure(request: Request): boolean {
  const override = process.env.OPENNOW_COOKIE_SECURE?.trim().toLowerCase();
  if (override === "0" || override === "false" || override === "no") return false;
  if (override === "1" || override === "true" || override === "yes") return true;
  return process.env.NODE_ENV === "production" && request.protocol === "https";
}

function persistCookieSession(request: Request, response: Response, session: WebAuthSession): void {
  if (!session.isDirty()) return;
  const encrypted = encryptedCookieValue(session.toSnapshot());
  const chunks = encrypted.match(new RegExp(`.{1,${COOKIE_CHUNK_SIZE}}`, "g")) ?? [];
  if (chunks.length > MAX_COOKIE_CHUNKS) {
    // Overflow used to throw, which turned the response into a 500 *and* left
    // the browser without a session cookie — a guaranteed sign-out on the next
    // launch. Keep the session on disk instead so the desktop mirror can
    // restore it, and only give up on the cookie.
    console.warn(
      `[Session] Encrypted session needs ${chunks.length} cookies; the budget is ${MAX_COOKIE_CHUNKS}. ` +
        (isSingleUserDesktopRuntime()
          ? "Persisting to the desktop session mirror instead."
          : "The visitor will be signed out on their next request."),
    );
    writeSessionMirror(session);
    session.markPersisted();
    return;
  }

  const options = {
    httpOnly: true,
    sameSite: "strict" as const,
    secure: resolveCookieSecure(request),
    maxAge: COOKIE_MAX_AGE_MS,
    path: COOKIE_PATH,
  };
  for (let index = 0; index < MAX_COOKIE_CHUNKS; index += 1) {
    const name = `${COOKIE_PREFIX}${index}`;
    const chunk = chunks[index];
    if (chunk) response.cookie(name, chunk, options);
    else response.clearCookie(name, options);
  }
  writeSessionMirror(session);
  session.markPersisted();
}

export const cookieSessionMiddleware: RequestHandler = (
  request: Request,
  response: Response,
  next: NextFunction,
): void => {
  const session = loadCookieSession(request);
  response.locals.openNowSession = session;
  onHeaders(response, () => {
    persistCookieSession(request, response, session);
  });
  next();
};

export function getSession(_request: Request, response?: Response): WebAuthSession {
  const session = response?.locals.openNowSession as WebAuthSession | undefined;
  if (!session) throw new Error("Cookie session middleware is not initialized.");
  return session;
}

export function getExistingSession(request: Pick<IncomingMessage, "headers">): WebAuthSession | null {
  const encrypted = joinedCookieValue(request);
  const snapshot = encrypted ? decryptCookieValue(encrypted) : readSessionMirror();
  return snapshot ? WebAuthSession.fromSnapshot(snapshot) : null;
}

export const cookieSessionInternals = {
  encrypt: encryptedCookieValue,
  decrypt: decryptCookieValue,
  load: loadCookieSession,
  persist: persistCookieSession,
  readMirror: readSessionMirror,
  writeMirror: writeSessionMirror,
  mirrorFileName: SESSION_MIRROR_FILE,
  secretFilePath: SESSION_SECRET_FILE,
};
