/**
 * UDP preflight check for native (NVST) launches.
 *
 * NVST media (video/audio/input) travels over UDP. When a launch punches the
 * game server for ~30s with zero inbound packets, the cause is either "this
 * PC/network blocks UDP" or "our NVST packets go nowhere" — indistinguishable
 * from the engine log alone. This module answers it directly: a minimal STUN
 * binding exchange with a public STUN server proves whether plain UDP works
 * from this machine. It runs fire-and-forget at native-start time and reports
 * via server.log; it never blocks or fails a launch.
 */
import { randomBytes } from "node:crypto";
import { createSocket, type Socket } from "node:dgram";
import { lookup as dnsLookup } from "node:dns/promises";

export interface UdpPreflightResult {
  ok: boolean;
  server: string;
  mappedAddress?: string;
  roundTripMs?: number;
  error?: string;
}

export interface StunServer {
  host: string;
  port: number;
}

export const DEFAULT_STUN_SERVERS: StunServer[] = [
  { host: "stun.l.google.com", port: 19302 },
  { host: "stun.cloudflare.com", port: 3478 },
];

const STUN_BINDING_REQUEST = 0x0001;
const STUN_BINDING_RESPONSE = 0x0101;
const STUN_MAGIC = 0x2112a442;
const ATTR_XOR_MAPPED_ADDRESS = 0x0020;
const IPV4_FAMILY = 0x01;

export interface StunDependencies {
  resolveIpv4?: (host: string) => Promise<string>;
  createUdpSocket?: () => Socket;
}

async function defaultResolveIpv4(host: string): Promise<string> {
  return (await dnsLookup(host, { family: 4 })).address;
}

/** Minimal RFC 5389 binding request: 20-byte header, no attributes. */
export function buildStunBindingRequest(transactionId: Buffer): Buffer {
  const out = Buffer.alloc(20);
  out.writeUInt16BE(STUN_BINDING_REQUEST, 0);
  out.writeUInt16BE(0, 2);
  out.writeUInt32BE(STUN_MAGIC, 4);
  transactionId.copy(out, 8, 0, 12);
  return out;
}

/**
 * Validate a STUN binding response (type + magic + transaction echo) and
 * decode its XOR-MAPPED-ADDRESS. Returns null when the datagram is not a
 * matching response; `mappedAddress` stays undefined when the server omits
 * the attribute (still a successful round trip).
 */
export function parseStunBindingResponse(
  data: Buffer,
  transactionId: Buffer,
): { mappedAddress?: string } | null {
  if (data.length < 20) return null;
  if (data.readUInt16BE(0) !== STUN_BINDING_RESPONSE) return null;
  if (data.readUInt16BE(2) + 20 > data.length) return null;
  if (data.readUInt32BE(4) !== STUN_MAGIC) return null;
  if (!data.subarray(8, 20).equals(transactionId)) return null;
  const end = 20 + data.readUInt16BE(2);
  let mappedAddress: string | undefined;
  let offset = 20;
  while (offset + 4 <= end) {
    const type = data.readUInt16BE(offset);
    const length = data.readUInt16BE(offset + 2);
    const valueStart = offset + 4;
    const valueEnd = valueStart + length;
    if (valueEnd > end) break;
    if (
      type === ATTR_XOR_MAPPED_ADDRESS
      && length >= 8
      && data[valueStart + 1] === IPV4_FAMILY
    ) {
      const port = data.readUInt16BE(valueStart + 2) ^ (STUN_MAGIC >>> 16);
      const raw = data.readUInt32BE(valueStart + 4) ^ STUN_MAGIC;
      const a = (raw >>> 24) & 0xff;
      const b = (raw >>> 16) & 0xff;
      const c = (raw >>> 8) & 0xff;
      const d = raw & 0xff;
      mappedAddress = `${a}.${b}.${c}.${d}:${port}`;
    }
    // Attributes are padded to a 32-bit boundary.
    offset = valueEnd + ((4 - (length % 4)) % 4);
  }
  return { mappedAddress };
}

function closeQuietly(socket: Socket): void {
  try {
    socket.close();
  } catch {
    // Already closed.
  }
}

async function stunRoundTrip(
  server: StunServer,
  timeoutMs: number,
  resolveIpv4: (host: string) => Promise<string>,
  createUdpSocket: () => Socket,
): Promise<{ mappedAddress?: string; roundTripMs: number }> {
  const ip = await resolveIpv4(server.host);
  const socket = createUdpSocket();
  const transactionId = randomBytes(12);
  const request = buildStunBindingRequest(transactionId);
  const startedAt = Date.now();
  let mappedAddress: string | undefined;
  try {
    await new Promise<void>((resolve, reject) => {
      const onError = (error: Error): void => {
        socket.removeAllListeners("message");
        reject(error);
      };
      const timer = setTimeout(() => {
        socket.removeListener("message", onMessage);
        reject(new Error(`no STUN response from ${ip}:${server.port} within ${timeoutMs}ms`));
      }, timeoutMs);
      const onMessage = (data: Buffer): void => {
        const parsed = parseStunBindingResponse(data, transactionId);
        if (!parsed) return; // Not our response — keep waiting.
        mappedAddress = parsed.mappedAddress;
        clearTimeout(timer);
        socket.removeListener("message", onMessage);
        socket.removeListener("error", onError);
        resolve();
      };
      socket.once("error", onError);
      socket.on("message", onMessage);
      socket.send(request, server.port, ip, (error) => {
        if (error) {
          clearTimeout(timer);
          socket.removeListener("message", onMessage);
          reject(error);
        }
      });
    });
    return { mappedAddress, roundTripMs: Date.now() - startedAt };
  } finally {
    closeQuietly(socket);
  }
}

/**
 * Try each STUN server in order until one answers. Resolves (never rejects)
 * with the verdict; callers log it.
 */
export async function runUdpPreflight(
  servers: StunServer[] = DEFAULT_STUN_SERVERS,
  options?: { timeoutMs?: number } & StunDependencies,
): Promise<UdpPreflightResult> {
  const timeoutMs = options?.timeoutMs ?? 2500;
  const resolveIpv4 = options?.resolveIpv4 ?? defaultResolveIpv4;
  const createUdpSocket = options?.createUdpSocket ?? (() => createSocket("udp4"));
  let lastError = "no STUN servers configured";
  for (const server of servers) {
    const label = `${server.host}:${server.port}`;
    try {
      const { mappedAddress, roundTripMs } = await stunRoundTrip(
        server, timeoutMs, resolveIpv4, createUdpSocket,
      );
      return { ok: true, server: label, mappedAddress, roundTripMs };
    } catch (error) {
      lastError = (error as Error).message;
    }
  }
  return { ok: false, server: servers.map((s) => `${s.host}:${s.port}`).join(", "), error: lastError };
}

export function formatUdpPreflight(result: UdpPreflightResult): string {
  if (result.ok) {
    const parts = [`ok via ${result.server}`];
    if (result.mappedAddress) parts.push(`mapped=${result.mappedAddress}`);
    if (result.roundTripMs !== undefined) parts.push(`rtt=${result.roundTripMs}ms`);
    return parts.join(" ");
  }
  return (
    `FAILED (${result.error}) — this PC/network appears to block UDP, ` +
    `and native streaming needs UDP to reach the game server. ` +
    `Check Windows Firewall / antivirus / router settings, or retry on a mobile hotspot.`
  );
}
