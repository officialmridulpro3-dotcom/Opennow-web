import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";

import { NativeOverlayRoot } from "./NativeOverlayRoot";
import type { NativeOverlayChannel, NativeOverlayMessage } from "./types";

/**
 * The overlay renders on top of the native video plane, so these tests are the
 * only place its markup can be checked without a Windows build: they cover the
 * two surfaces (deck, live stats) and the "closed" state the shell relies on to
 * keep the window click-through.
 */

function createChannel(): {
  channel: NativeOverlayChannel;
  send: (message: NativeOverlayMessage) => void;
} {
  const listeners = new Set<(message: NativeOverlayMessage) => void>();
  return {
    channel: {
      subscribe(listener) {
        listeners.add(listener);
        return () => listeners.delete(listener);
      },
      dispatch() {
        /* actions are asserted through the bridge, not here */
      },
      reportVisibility() {
        /* the shell owns window visibility */
      },
    },
    send(message) {
      listeners.forEach((listener) => listener(message));
    },
  };
}

const session = {
  gameTitle: "Cyberpunk 2077",
  store: "STEAM",
  region: "EU West",
  startedAtMs: Date.now() - 90_000,
  transport: "nvst" as const,
  enginePid: 4242,
  protocolVersion: 7,
  firstFrame: true,
};

const stats = {
  codec: "h264",
  resolution: "2560x1440",
  decodedFps: 59.4,
  renderFps: 59.1,
  bitrateKbps: 42_000,
  targetBitrateKbps: 45_000,
  rttMs: 18,
  packetLossPercent: 0.2,
  framesDecoded: 120_000,
  hardwareAcceleration: "D3D11VA",
};

describe("NativeOverlayRoot", () => {
  it("stays empty until the host opens something", () => {
    const { channel } = createChannel();
    const html = renderToStaticMarkup(<NativeOverlayRoot channel={channel} />);
    expect(html).not.toContain("nov-deck");
    expect(html).not.toContain("nov-hud");
  });

  it("renders the stream deck with live session metrics", () => {
    const { channel } = createChannel();
    const html = renderToStaticMarkup(
      <NativeOverlayRoot channel={channel} initialDeckOpen initialSession={session} initialStats={stats} />,
    );
    expect(html).toContain("Stream deck");
    expect(html).toContain("Cyberpunk 2077");
    expect(html).toContain("2560×1440");
    expect(html).toContain(">42<span"); // 42000 kbps, integer above 10 Mbps
    expect(html).toContain("Mbps");
    expect(html).toContain(">18<span"); // ping
    expect(html).toContain("D3D11VA");
    expect(html).toContain("End session");
    // The deck is a real tablist, not a GDI menu.
    expect(html).toContain('role="tablist"');
    expect(html).toContain('aria-selected="true"');
  });

  it("renders the compact stats HUD with the engine's counters", () => {
    const { channel } = createChannel();
    const html = renderToStaticMarkup(
      <NativeOverlayRoot channel={channel} initialHudVisible initialSession={session} initialStats={stats} />,
    );
    expect(html).toContain("Performance");
    expect(html).toContain("nov-hud__cell-value");
    expect(html).toContain("0.2%"); // packet loss
    expect(html).not.toContain("nov-deck");
  });
});
