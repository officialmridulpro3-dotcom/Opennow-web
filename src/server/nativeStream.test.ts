import { describe, expect, it } from "vitest";

import { buildSidecarEnv, finalizeNativeContext, nativeSidecar, resolveNativeMediaPeer } from "./nativeStream";

function baseContext(): Record<string, unknown> {
  return {
    session: {
      sessionId: "sess-1",
      serverIp: "10.0.0.1",
      rtspsEndpoints: ["rtsps://10.0.0.1:443/session"],
    },
    settings: {
      transportMode: "webrtc",
      resolution: "1280x720",
      fps: 60,
      maxBitrateMbps: 20,
      codec: "h264",
      colorQuality: "8bit_420",
    },
    shortcuts: { stopStream: "Escape" },
  };
}

describe("finalizeNativeContext", () => {
  it("forces the nvst transport and auto video backend", () => {
    const { sessionId, context } = finalizeNativeContext(baseContext());
    expect(sessionId).toBe("sess-1");
    const settings = context.settings as unknown as Record<string, unknown>;
    expect(settings.transportMode).toBe("nvst");
    expect(settings.nativeVideoBackend).toBe("auto");
    const session = context.session as unknown as Record<string, unknown>;
    const extra = session.extra as Record<string, unknown>;
    expect(extra.rtspsEndpoints).toEqual(["rtsps://10.0.0.1:443/session"]);
  });

  it("rejects a missing session id", () => {
    const bad = baseContext();
    (bad.session as Record<string, unknown>).sessionId = "";
    expect(() => finalizeNativeContext(bad)).toThrowError(/session ID/);
  });

  it("rejects a missing server address", () => {
    const bad = baseContext();
    (bad.session as Record<string, unknown>).serverIp = "";
    expect(() => finalizeNativeContext(bad)).toThrowError(/server address/);
  });

  it("rejects a session without RTSPS endpoints", () => {
    const bad = baseContext();
    (bad.session as Record<string, unknown>).rtspsEndpoints = [];
    expect(() => finalizeNativeContext(bad)).toThrowError(/RTSPS/);
  });
});

describe("resolveNativeMediaPeer", () => {
  function contextWithMediaIp(ip: string | undefined) {
    const base = baseContext();
    (base.session as Record<string, unknown>).mediaConnectionInfo =
      ip === undefined ? undefined : { ip, port: 48322, usage: 14 };
    return finalizeNativeContext(base).context;
  }

  it("passes literal IPs through without a DNS lookup", async () => {
    const context = contextWithMediaIp("80.250.98.39");
    const resolved = await resolveNativeMediaPeer(context, async () => {
      throw new Error("must not resolve a literal IP");
    });
    expect(resolved).toBe(context);
  });

  it("resolves a hostname media peer to IPv4, keeping TLS hosts intact", async () => {
    const context = contextWithMediaIp("80-250-98-39.cloudmatchbeta.nvidiagrid.net");
    const resolved = await resolveNativeMediaPeer(context, async (host) => {
      expect(host).toBe("80-250-98-39.cloudmatchbeta.nvidiagrid.net");
      return "80.250.98.39";
    });
    expect(resolved).not.toBe(context);
    const session = resolved.session as unknown as Record<string, unknown>;
    expect(session.mediaConnectionInfo).toEqual({ ip: "80.250.98.39", port: 48322, usage: 14 });
    // TLS/SNI inputs keep their hostnames — only the UDP peer IP is rewritten.
    expect(session.serverIp).toBe("10.0.0.1");
    const extra = session.extra as Record<string, unknown>;
    expect(extra.rtspsEndpoints).toEqual(["rtsps://10.0.0.1:443/session"]);
    // The input context is not mutated.
    const original = context.session.mediaConnectionInfo as unknown as Record<string, unknown>;
    expect(original.ip).toBe("80-250-98-39.cloudmatchbeta.nvidiagrid.net");
  });

  it("passes through when no media peer is present", async () => {
    const context = contextWithMediaIp(undefined);
    const resolved = await resolveNativeMediaPeer(context, async () => {
      throw new Error("must not resolve without a media peer");
    });
    expect(resolved).toBe(context);
  });

  it("throws a clear error when DNS resolution fails", async () => {
    const context = contextWithMediaIp("80-250-98-39.cloudmatchbeta.nvidiagrid.net");
    await expect(
      resolveNativeMediaPeer(context, async () => {
        throw new Error("ENOTFOUND");
      }),
    ).rejects.toThrow(/Couldn't resolve the game server address/);
  });
});

describe("nativeSidecar status", () => {
  it("reports unsupported without a bundled sidecar", () => {
    delete process.env.OPENNOW_NVST_SIDECAR;
    expect(nativeSidecar.status()).toMatchObject({ supported: false, running: false });
  });
});

describe("nativeSidecar handshake timeout", () => {
  it("fails a silent sidecar instead of hanging forever", async () => {
    // Plain node with piped stdio reads stdin to EOF and prints nothing:
    // a perfect silent-sidecar simulator.
    process.env.OPENNOW_NVST_SIDECAR = process.execPath;
    process.env.OPENNOW_NVST_HELLO_TIMEOUT_MS = "300";
    try {
      const { context } = finalizeNativeContext(baseContext());
      await expect(nativeSidecar.start("sess-1", context)).rejects.toThrow(/did not answer the startup handshake/);
      expect(nativeSidecar.status().running).toBe(false);
    } finally {
      delete process.env.OPENNOW_NVST_SIDECAR;
      delete process.env.OPENNOW_NVST_HELLO_TIMEOUT_MS;
    }
  }, 15_000);
});

describe("buildSidecarEnv", () => {
  it("forces the engine's own visible game window", () => {
    const previous = process.env.OPENNOW_NATIVE_EXTERNAL_RENDERER;
    delete process.env.OPENNOW_NATIVE_EXTERNAL_RENDERER;
    try {
      const env = buildSidecarEnv();
      expect(env.OPENNOW_NATIVE_EXTERNAL_RENDERER).toBe("1");
      // Rest of the environment passes through untouched.
      expect(env.PATH).toBe(process.env.PATH);
      process.env.OPENNOW_NATIVE_EXTERNAL_RENDERER = "0";
      expect(buildSidecarEnv().OPENNOW_NATIVE_EXTERNAL_RENDERER).toBe("1");
    } finally {
      if (previous === undefined) {
        delete process.env.OPENNOW_NATIVE_EXTERNAL_RENDERER;
      } else {
        process.env.OPENNOW_NATIVE_EXTERNAL_RENDERER = previous;
      }
    }
  });

  it("hands engine-side input capture to the native window", () => {
    const previous = process.env.OPENNOW_NATIVE_INPUT_OWNER;
    delete process.env.OPENNOW_NATIVE_INPUT_OWNER;
    try {
      expect(buildSidecarEnv().OPENNOW_NATIVE_INPUT_OWNER).toBe("native");
      process.env.OPENNOW_NATIVE_INPUT_OWNER = "browser";
      expect(buildSidecarEnv().OPENNOW_NATIVE_INPUT_OWNER).toBe("native");
    } finally {
      if (previous === undefined) {
        delete process.env.OPENNOW_NATIVE_INPUT_OWNER;
      } else {
        process.env.OPENNOW_NATIVE_INPUT_OWNER = previous;
      }
    }
  });
});
