import { createSocket, type AddressInfo, type RemoteInfo } from "node:dgram";
import { describe, expect, it } from "vitest";

import {
  buildStunBindingRequest,
  formatUdpPreflight,
  parseStunBindingResponse,
  runUdpPreflight,
} from "./udpPreflight";

const STUN_MAGIC = 0x2112a442;

/** Hand-built RFC 5389 binding response advertising 203.0.113.7:54321. */
function buildFakeStunResponse(transactionId: Buffer): Buffer {
  const out = Buffer.alloc(20 + 4 + 8);
  out.writeUInt16BE(0x0101, 0);
  out.writeUInt16BE(12, 2);
  out.writeUInt32BE(STUN_MAGIC, 4);
  transactionId.copy(out, 8, 0, 12);
  out.writeUInt16BE(0x0020, 20); // XOR-MAPPED-ADDRESS
  out.writeUInt16BE(8, 22);
  out[24] = 0x00;
  out[25] = 0x01; // IPv4
  out.writeUInt16BE(54321 ^ (STUN_MAGIC >>> 16), 26);
  out.writeUInt32BE((0xcb007107 ^ STUN_MAGIC) >>> 0, 28); // 203.0.113.7
  return out;
}

async function startFakeStunServer(): Promise<{ port: number; close: () => Promise<void> }> {
  const server = createSocket("udp4");
  server.on("message", (message: Buffer, rinfo: RemoteInfo) => {
    if (message.length < 20 || message.readUInt16BE(0) !== 0x0001) return;
    server.send(buildFakeStunResponse(message.subarray(8, 20)), rinfo.port, rinfo.address);
  });
  await new Promise<void>((resolve) => server.bind(0, "127.0.0.1", () => resolve()));
  const port = (server.address() as AddressInfo).port;
  return { port, close: () => new Promise((resolve) => server.close(() => resolve())) };
}

/** Returns a localhost UDP port that is (almost certainly) closed. */
async function closedLocalPort(): Promise<number> {
  const socket = createSocket("udp4");
  await new Promise<void>((resolve) => socket.bind(0, "127.0.0.1", () => resolve()));
  const port = (socket.address() as AddressInfo).port;
  await new Promise<void>((resolve) => socket.close(() => resolve()));
  return port;
}

describe("buildStunBindingRequest", () => {
  it("builds a 20-byte header-only request echoing the transaction id", () => {
    const tid = Buffer.from("0123456789abcdef01234567", "hex");
    const request = buildStunBindingRequest(tid);
    expect(request.length).toBe(20);
    expect(request.readUInt16BE(0)).toBe(0x0001);
    expect(request.readUInt16BE(2)).toBe(0);
    expect(request.readUInt32BE(4)).toBe(STUN_MAGIC);
    expect(request.subarray(8, 20).equals(tid)).toBe(true);
  });
});

describe("parseStunBindingResponse", () => {
  const tid = Buffer.from("0123456789abcdef01234567", "hex");

  it("decodes the XOR-mapped address", () => {
    expect(parseStunBindingResponse(buildFakeStunResponse(tid), tid)).toEqual({
      mappedAddress: "203.0.113.7:54321",
    });
  });

  it("rejects truncated, mistyped, and foreign-transaction datagrams", () => {
    const good = buildFakeStunResponse(tid);
    expect(parseStunBindingResponse(good.subarray(0, 10), tid)).toBeNull();
    const wrongType = Buffer.from(good);
    wrongType.writeUInt16BE(0x0001, 0);
    expect(parseStunBindingResponse(wrongType, tid)).toBeNull();
    const wrongTid = Buffer.from(good);
    wrongTid[8] ^= 0xff;
    expect(parseStunBindingResponse(wrongTid, tid)).toBeNull();
  });
});

describe("runUdpPreflight", () => {
  it("succeeds against a local STUN server and reports the mapped address", async () => {
    const fake = await startFakeStunServer();
    try {
      const result = await runUdpPreflight([{ host: "stun.test", port: fake.port }], {
        timeoutMs: 2000,
        resolveIpv4: async () => "127.0.0.1",
      });
      expect(result.ok).toBe(true);
      expect(result.server).toBe(`stun.test:${fake.port}`);
      expect(result.mappedAddress).toBe("203.0.113.7:54321");
      expect(result.roundTripMs).toBeGreaterThanOrEqual(0);
    } finally {
      await fake.close();
    }
  });

  it("fails when nobody answers", async () => {
    const port = await closedLocalPort();
    const result = await runUdpPreflight([{ host: "stun.test", port }], {
      timeoutMs: 150,
      resolveIpv4: async () => "127.0.0.1",
    });
    expect(result.ok).toBe(false);
    expect(result.error).toBeTruthy();
  });

  it("falls through to the next server", async () => {
    const fake = await startFakeStunServer();
    try {
      const deadPort = await closedLocalPort();
      const result = await runUdpPreflight(
        [
          { host: "dead.test", port: deadPort },
          { host: "stun.test", port: fake.port },
        ],
        { timeoutMs: 150, resolveIpv4: async () => "127.0.0.1" },
      );
      expect(result.ok).toBe(true);
      expect(result.server).toBe(`stun.test:${fake.port}`);
    } finally {
      await fake.close();
    }
  });
});

describe("formatUdpPreflight", () => {
  it("formats success with server, mapping, and rtt", () => {
    expect(
      formatUdpPreflight({ ok: true, server: "s:1", mappedAddress: "1.2.3.4:5", roundTripMs: 42 }),
    ).toBe("ok via s:1 mapped=1.2.3.4:5 rtt=42ms");
  });

  it("formats failure with remediation guidance", () => {
    expect(formatUdpPreflight({ ok: false, server: "s:1", error: "nope" })).toMatch(
      /FAILED.*hotspot/,
    );
  });
});
