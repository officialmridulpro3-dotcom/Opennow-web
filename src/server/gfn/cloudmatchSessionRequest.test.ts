import { describe, expect, it } from "vitest";

import type { SessionCreateRequest, StreamSettings } from "@shared/gfn";

import {
  buildClaimRequestBody,
  buildSessionRequestBody,
} from "./cloudmatchSessionRequest";

function streamSettings(transportMode: "webrtc" | "nvst"): StreamSettings {
  return {
    resolution: "1280x720",
    fps: 60,
    maxBitrateMbps: 20,
    codec: "h264",
    colorQuality: "8bit_420",
    keyboardLayout: "us",
    gameLanguage: "en_US",
    enableL4S: false,
    enableCloudGsync: false,
    transportMode,
  } as unknown as StreamSettings;
}

function createInput(transportMode: "webrtc" | "nvst"): SessionCreateRequest {
  return {
    appId: "12345",
    internalTitle: "Test Game",
    zone: "prod",
    settings: streamSettings(transportMode),
  };
}

type WireData = Record<string, unknown>;
type MetaEntry = { key: string; value: string };

function sessionRequestData(body: unknown): WireData {
  return (body as { sessionRequestData: WireData }).sessionRequestData;
}

function metaKeys(body: unknown): string[] {
  return (sessionRequestData(body).metaData as MetaEntry[]).map((entry) => entry.key);
}

describe("buildSessionRequestBody transport provisioning", () => {
  it("provisions WebRTC by default", () => {
    const body = buildSessionRequestBody(createInput("webrtc"), "device-1", null);
    const data = sessionRequestData(body);
    expect(metaKeys(body)).toContain("GSStreamerType");
    expect(data.secureRTSPSupported).toBe(false);
    expect(data.enhancedStreamMode).toBe(1);
    expect(data.transport).toBeUndefined();
  });

  it("provisions the classic RTSPS streamer for nvst (no GSStreamerType)", () => {
    const body = buildSessionRequestBody(createInput("nvst"), "device-1", null);
    const data = sessionRequestData(body);
    expect(metaKeys(body)).not.toContain("GSStreamerType");
    expect(metaKeys(body)).toContain("SubSessionId");
    expect(data.secureRTSPSupported).toBe(true);
    expect(data.enhancedStreamMode).toBe(0);
    expect(data.transport).toBeNull();
  });
});

describe("buildClaimRequestBody transport echo", () => {
  it("echoes WebRTC provisioning on resume", () => {
    const body = buildClaimRequestBody("sess-1", "12345", streamSettings("webrtc"));
    const data = sessionRequestData(body);
    expect(metaKeys(body)).toContain("GSStreamerType");
    expect(data.secureRTSPSupported).toBe(false);
    expect(data.enhancedStreamMode).toBe(1);
  });

  it("echoes NVST provisioning on resume (drops clientPhysicalResolution)", () => {
    const body = buildClaimRequestBody("sess-1", "12345", streamSettings("nvst"));
    const data = sessionRequestData(body);
    expect(metaKeys(body)).not.toContain("GSStreamerType");
    expect(metaKeys(body)).not.toContain("clientPhysicalResolution");
    expect(data.secureRTSPSupported).toBe(true);
    expect(data.enhancedStreamMode).toBe(0);
  });
});

describe("resolveLaunchTransportMode sidecar gate", () => {
  it("honors nvst only with a sidecar present", async () => {
    const { resolveLaunchTransportMode } = await import("../nativeStream");
    process.env.OPENNOW_NVST_SIDECAR = "src/server/nativeStream.ts";
    expect(resolveLaunchTransportMode("nvst")).toEqual({ clientMode: "native", transportMode: "nvst" });
    delete process.env.OPENNOW_NVST_SIDECAR;
    expect(resolveLaunchTransportMode("nvst")).toEqual({ clientMode: "web", transportMode: "webrtc" });
    expect(resolveLaunchTransportMode("webrtc")).toEqual({ clientMode: "web", transportMode: "webrtc" });
    expect(resolveLaunchTransportMode(undefined)).toEqual({ clientMode: "web", transportMode: "webrtc" });
  });
});
