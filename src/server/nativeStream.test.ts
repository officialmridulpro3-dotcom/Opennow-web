import { describe, expect, it } from "vitest";

import { finalizeNativeContext, nativeSidecar } from "./nativeStream";

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

describe("nativeSidecar status", () => {
  it("reports unsupported without a bundled sidecar", () => {
    delete process.env.OPENNOW_NVST_SIDECAR;
    expect(nativeSidecar.status()).toMatchObject({ supported: false, running: false });
  });
});
