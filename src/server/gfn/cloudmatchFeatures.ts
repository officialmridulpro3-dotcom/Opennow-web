import type { AppLaunchMode, SessionCreateRequest, StreamSettings } from "@shared/gfn";
import { DEFAULT_MINIMUM_FPS_FOR_REFLEX_WITHOUT_VRR } from "@shared/cloudGsync";

import type { CloudMatchRequest } from "./types";

// Wire values used by cloudmatch session requests. Matches the official
// client's mapping: Default -> 1, GamepadFriendly -> 2, TouchFriendly -> 3.
const APP_LAUNCH_MODE_WIRE_VALUES: Record<AppLaunchMode, number> = {
  default: 1,
  gamepadFriendly: 2,
  touchFriendly: 3,
};

export function appLaunchModeWireValue(mode: AppLaunchMode | undefined): number {
  return APP_LAUNCH_MODE_WIRE_VALUES[mode ?? "default"];
}

export function buildRequestedStreamingFeatures(
  settings: StreamSettings,
  bitDepth: number,
  chromaFormat: number,
  _hdrEnabled: boolean,
): CloudMatchRequest["sessionRequestData"]["requestedStreamingFeatures"] {
  const cloudGsync = settings.enableCloudGsync;

  return {
    reflex: shouldRequestReflex(settings),
    bitDepth,
    cloudGsync,
    enabledL4S: settings.enableL4S,
    supportedHidDevices: 0,
    profile: 0,
    fallbackToLogicalResolution: false,
    chromaFormat,
    prefilterMode: 0,
    prefilterSharpness: 0,
    prefilterNoiseReduction: 0,
    hudStreamingMode: 0,
  };
}

/** CloudMatch codec wire values, matching the reference native client. */
export function codecWireValue(codec: StreamSettings["codec"]): number {
  switch (codec) {
    case "H264":
      return 1;
    case "H265":
      return 2;
    case "AV1":
      return 3;
    default:
      return 0;
  }
}

/**
 * Full requestedStreamingFeatures for native (NVST) sessions. Unlike WebRTC
 * (codec negotiated later via SDP), the native seat configures its encoder
 * purely from these fields at allocation — without codec/maxBitrateKbps it
 * never becomes ready and the launch spins in setup forever.
 */
export function buildNativeRequestedStreamingFeatures(
  settings: StreamSettings,
  bitDepth: number,
  chromaFormat: number,
): CloudMatchRequest["sessionRequestData"]["requestedStreamingFeatures"] {
  const codec = codecWireValue(settings.codec);
  // Keep a manually selected codec fixed; constrain color instead (H.264 is
  // 8-bit 4:2:0-only, AV1 is 4:2:0-only on the official client).
  const [depth, chroma] = codec === 1 ? [0, 0] : codec === 3 ? [bitDepth, 0] : [bitDepth, chromaFormat];
  const cloudGsync = settings.nativeCloudGsyncMode === "disabled"
    ? false
    : settings.nativeCloudGsyncMode === "forced"
      ? true
      : settings.enableCloudGsync;
  const maxBitrateMbps = Math.min(200, Math.max(1, Math.round(settings.maxBitrateMbps)));
  return {
    reflex: cloudGsync || settings.fps >= 120,
    bitDepth: depth,
    cloudGsync,
    enabledL4S: settings.enableL4S,
    supportedHidDevices: 0,
    profile: 0,
    fallbackToLogicalResolution: false,
    chromaFormat: chroma,
    prefilterMode: 0,
    prefilterSharpness: 0,
    prefilterNoiseReduction: 0,
    hudStreamingMode: 0,
    codec,
    maxBitrateKbps: maxBitrateMbps * 1000,
    vsync: false,
    audioChannelCount: 2,
    mouseMovementFlags: 0,
    trueHdr: false,
    hidDevices: null,
    qosPolicy: 0,
    touchSupport: false,
    dynamicStreamingMode: 0,
  };
}

export function shouldRequestReflex(settings: StreamSettings): boolean {
  if (typeof settings.cloudGsyncResolution?.reflexEnabled === "boolean") {
    return settings.cloudGsyncResolution.reflexEnabled;
  }

  const reflexMinimum =
    settings.cloudGsyncResolution?.capabilities.minimumFpsForReflexWithoutVrr
    ?? DEFAULT_MINIMUM_FPS_FOR_REFLEX_WITHOUT_VRR;
  return settings.enableCloudGsync || settings.fps >= reflexMinimum;
}

export function shouldEnableInGameSettingsPersistence(
  input: Pick<SessionCreateRequest, "enablePersistingInGameSettings" | "supportsInGameSettingsPersistence">,
): boolean {
  return (
    input.enablePersistingInGameSettings === true &&
    input.supportsInGameSettingsPersistence === true
  );
}
