import type { ComponentProps } from "react";
import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";

import type { SessionAdState } from "@shared/gfn";

import { StreamLoading, movementBarPercent } from "./StreamLoading";

const HERO = "https://example.invalid/hero.jpg";
const COVER = "https://example.invalid/cover.jpg";

function render(overrides: Partial<ComponentProps<typeof StreamLoading>> = {}): string {
  return renderToStaticMarkup(
    <StreamLoading
      gameTitle="Cyberpunk 2077"
      gameCover={COVER}
      gameHero={HERO}
      platformStore="STEAM"
      status="queue"
      queuePosition={42}
      estimatedWait="3 min"
      diagnosticLine="GeForce NOW · node fra3 · ping 18 ms · build 2.14.0"
      onCancel={() => {}}
      {...overrides}
    />,
  );
}

describe("StreamLoading queue spread", () => {
  it("splits the screen into an art band and an information panel", () => {
    const html = render();
    expect(html).toContain('data-art="on"');
    expect(html).toContain("qs-main");
    expect(html).toContain("qs-panel-inner");
    // The deck is a layered ground, not a flat black rectangle.
    for (const layer of [
      "qs-panel-grid",
      "qs-panel-scan",
      "qs-panel-glow",
      "qs-panel-sweep",
      "qs-panel-edge",
    ]) {
      expect(html).toContain(layer);
    }
    // The old framed treatments must stay gone.
    expect(html).not.toContain("qs-card");
    expect(html).not.toContain("qs-cover");
  });

  it("layers the art band and never sets panel text on it", () => {
    const html = render();
    // Image, grade and bloom are decorative layers; no copy lives in the band.
    expect(html).toContain(`<div class="qs-art" data-kind="hero" aria-hidden="true">`);
    expect(html).toContain(`<div class="qs-art-image" style="background-image:url(${HERO})"></div>`);
    expect(html).toContain("qs-art-grade");
    expect(html).toContain("qs-art-bloom");
    expect(html).not.toContain("qs-art-takeover");
  });

  it("prefers the wide hero and falls back to box art with a top crop", () => {
    expect(render()).toContain('data-kind="hero"');
    const fallback = render({ gameHero: undefined });
    expect(fallback).toContain(`url(${COVER})`);
    expect(fallback).toContain('data-kind="cover"');
  });

  it("falls back to a ghost initial when there is no art at all", () => {
    const html = render({ gameCover: undefined, gameHero: undefined });
    expect(html).toContain('data-art="off"');
    expect(html).toContain('data-kind="none"');
    expect(html).toContain("qs-art-letter");
    expect(html).toContain(">C<");
    expect(html).not.toContain("background-image");
  });

  it("keeps one line of chrome: status, elapsed clock and cancel", () => {
    const html = render();
    expect(html).toContain("qs-topbar");
    expect(html).toContain("qs-status-pill");
    expect(html).toContain("qs-status-text");
    expect(html).toContain(">In queue<");
    expect(html).toContain("qs-timer");
    expect(html).toContain(">00:00<");
    expect(html).toContain("qs-btn-ghost");
    expect(html).toContain(">Cancel<");
  });

  it("sets the store and title as the masthead of the panel", () => {
    const html = render();
    expect(html).toContain("qs-provider-badge");
    expect(html).toContain(">S<");
    expect(html).toContain(">Steam<");
    expect(html).toContain("qs-title");
    expect(html).toContain("Cyberpunk 2077");
  });

  it("makes the live position the focal element with a rolling numeral", () => {
    const html = render();
    expect(html).toContain('<section class="qs-plate" role="status"');
    expect(html).toContain("qs-hero-label");
    expect(html).toContain(">Your position<");
    expect(html).toContain("qs-numeral");
    expect(html).toContain("qs-live-dot");
    expect(html.match(/qs-odometer-col/g)?.length).toBe(2); // one masked column per digit
    expect(html).toContain('aria-hidden="true"');
  });

  it("gives assistive tech a sentence instead of the animated numeral", () => {
    const html = render();
    expect(html).toContain('role="status"');
    expect(html).toContain('aria-live="polite"');
    expect(html).toContain("qs-sr-only");
    expect(html).toContain("Position #42 in queue");
  });

  it("reports the wait and the players ahead as two HUD cells", () => {
    const html = render();
    expect(html).toContain("qs-hud");
    expect(html.match(/qs-cell-label/g)?.length).toBe(2);
    expect(html).toContain(">Estimated wait<");
    expect(html).toContain(">3 min<");
    expect(html).toContain(">Players ahead<");
    expect(html).toContain(">41<");
  });

  it("says you're next when nobody is ahead, and mutes what it cannot know", () => {
    const next = render({ queuePosition: 1 });
    expect(next).toContain("qs-cell--next");
    expect(next).toContain(">You&#x27;re next<");

    const unknown = render({ estimatedWait: undefined, status: "setup", queuePosition: undefined });
    expect(unknown.match(/qs-cell--muted/g)?.length).toBe(2);
    expect(unknown).toContain(">Calculating…<");
    expect(unknown).toContain('qs-cell-value">—<');
  });

  it("names the stage instead of a number once the place is gone", () => {
    const setup = render({ status: "setup", queuePosition: undefined });
    expect(setup).toContain('data-stage="1"');
    expect(setup).toContain(">Current stage<");
    expect(setup).toContain("qs-stage-word");
    expect(setup).toContain(">Setup<");
    expect(setup).not.toContain("qs-odometer");
    expect(render({ status: "connecting", queuePosition: undefined })).toContain(">Connect<");
  });

  it("exposes the launch rail as a progressbar with four slanted segments", () => {
    const html = render();
    expect(html).toContain('role="progressbar"');
    expect(html).toContain('aria-valuenow="12"'); // 12.5% — the centre of stop one
    expect(html).toContain('aria-valuetext="Step 1 of 4: Queue"');
    expect(html).toContain("qs-progress-track");
    expect(html).toContain("qs-progress-fill");
    expect(html).toContain("qs-segments");
    expect(html).toContain("qs-segment-index");
    for (const step of ["Queue", "Setup", "Connect", "Ready"]) {
      expect(html).toContain(`qs-segment-label">${step}</span>`);
    }
    expect(html).toContain('data-state="active"');
    expect(html).toContain('aria-current="step"');
    expect(html.match(/data-state="next"/g)?.length).toBe(3);
  });

  it("keeps an informative body line and a voice line under it", () => {
    const html = render();
    expect(html).toContain("qs-body");
    expect(html).toContain("The live queue is moving. Your place is secured at #42.");
    expect(html).toContain("qs-editorial-rule");
    expect(html).toContain("qs-editorial");
    expect(html).toContain("keep your place");
  });

  it("keeps the diagnostics in the footer with the full text on hover", () => {
    const html = render();
    expect(html).toContain("qs-footer");
    expect(html).toContain(
      'title="GeForce NOW · node fra3 · ping 18 ms · build 2.14.0">GeForce NOW · node fra3 · ping 18 ms · build 2.14.0',
    );
    expect(render({ diagnosticLine: null })).not.toContain("qs-footer");
  });

  it("renders the error state as an alert that keeps the masthead and the art", () => {
    const html = render({
      error: {
        title: "No rig available",
        description: "Every rig in this region is busy.",
        code: "ERR_NO_CAPACITY",
      },
      onErrorAction: () => {},
    });
    expect(html).toContain('data-error="true"');
    expect(html).toContain("qs-error");
    expect(html).toContain('role="alert"');
    expect(html).toContain("qs-error-eyebrow");
    expect(html).toContain("No rig available");
    expect(html).toContain("qs-code-chip");
    expect(html).toContain("ERR_NO_CAPACITY");
    expect(html).toContain("qs-btn-primary");
    expect(html).toContain("Try again");
    // The spread survives: art band, store and title are still there.
    expect(html).toContain("qs-art");
    expect(html).toContain("Cyberpunk 2077");
    expect(html).not.toContain("qs-hero");
    expect(html).toContain(">Couldn&#x27;t start your session<");
  });

  it("folds a pending ad into the body line instead of giving it a section", () => {
    const adState: SessionAdState = {
      isAdsRequired: true,
      message: "Watch to keep your place in line",
      sessionAds: [{ adId: "ad-1", title: "Sponsored break" }],
      ads: [],
    };
    const html = render({ adState });
    expect(html).toContain("qs-body");
    expect(html).toContain("Watch to keep your place in line");
    // No ad furniture in the panel at all.
    expect(html).not.toContain('class="qs-ad"');
    expect(html).not.toContain("qs-ad-label");
    expect(html).not.toContain("qs-adzone");
    expect(html).not.toContain(">Sponsored<");

    const paused = render({ adState: { ...adState, isQueuePaused: true } });
    expect(paused).toContain("Session queue paused");
    expect(paused).toContain("Resume ads to stay in queue.");
  });

  it("gives a playing ad the whole art band, tagged and nothing more", () => {
    const html = render({
      adState: { isAdsRequired: true, ads: [], sessionAds: [] },
      activeAd: { adId: "ad-9", title: "Explore the new expansion" } as never,
      activeAdMediaUrl: "https://example.invalid/ad.mp4",
    });
    expect(html).toContain('data-ad="playing"');
    expect(html).toContain("qs-art-takeover");
    expect(html).toContain("qs-art-adtag");
    expect(html).toContain(">Ad<");
    // The band now carries the player, so it is no longer hidden from AT.
    expect(html).toContain('<div class="qs-art" data-kind="hero">');
    expect(html).not.toContain("qs-ad-item-row");
  });

  it("traces real queue movement and scales the bars against the largest jump", () => {
    // Nothing has moved yet, so the trace stays out of the markup entirely.
    expect(render()).not.toContain("qs-movement");
    expect(movementBarPercent(6, 12)).toBe(50);
    expect(movementBarPercent(12, 12)).toBe(100);
    expect(movementBarPercent(1, 12)).toBe(12); // floor: a single place still shows
    expect(movementBarPercent(3, 0)).toBe(100); // no history yet: never divide by zero
    expect(movementBarPercent(-4, 12)).toBe(12); // a stray negative reads as the floor
  });
});
