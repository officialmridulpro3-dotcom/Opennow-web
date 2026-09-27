import type { ComponentProps } from "react";
import { describe, expect, it } from "vitest";
import { renderToStaticMarkup } from "react-dom/server";

import type { SessionAdState } from "@shared/gfn";

import { StreamLoading } from "./StreamLoading";

function render(overrides: Partial<ComponentProps<typeof StreamLoading>> = {}): string {
  return renderToStaticMarkup(
    <StreamLoading
      gameTitle="Cyberpunk 2077"
      gameCover="https://example.invalid/cover.jpg"
      platformStore="STEAM"
      status="queue"
      queuePosition={42}
      estimatedWait="3 min"
      diagnosticLine="poll #4 · seat status 1 · queue 42"
      onCancel={() => {}}
      {...overrides}
    />,
  );
}

describe("StreamLoading queue console", () => {
  it("centers the launch console and shows the place in queue", () => {
    const html = render();
    expect(html).toContain("qs-root");
    expect(html).toContain("qs-stack");
    expect(html).toContain("qs-main");
    // Giant odometer numeral, one column per digit.
    expect(html.match(/qs-odometer-col/g)?.length).toBe(2);
    expect(html).toContain(">4<");
    expect(html).toContain(">2<");
    expect(html).toContain("Your place in queue");
    expect(html).toContain("41 players ahead of you");
    expect(html).toContain("poll #4 · seat status 1 · queue 42");
  });

  it("pins a launch progress hairline to the top edge", () => {
    // Motion renders the initial width server-side; the stage target is applied
    // in the browser, so only the structure is assertable here.
    const html = render();
    expect(html).toContain('class="qs-progress" aria-hidden="true"');
    expect(html).toContain("qs-progress-fill");
    expect(render({ status: "connecting", queuePosition: undefined })).toContain("qs-progress-fill");
  });

  it("renders every stage of the launch rail", () => {
    const html = render();
    for (const stage of ["Queue", "Setup", "Connect", "Ready"]) {
      expect(html).toContain(`>${stage}</span>`);
    }
    expect(html).toContain("qs-rail-step--active");
    expect(html.match(/qs-rail-line/g)?.length).toBeGreaterThanOrEqual(3);
  });

  it("advances the stage marker as the session leaves the queue", () => {
    const queue = render({ status: "queue" });
    const setup = render({ status: "setup", queuePosition: undefined });
    const connecting = render({ status: "connecting", queuePosition: undefined });

    expect(queue).toContain('data-stage="0"');
    expect(setup).toContain('data-stage="1"');
    expect(connecting).toContain('data-stage="2"');
    // Without a held place the hero falls back to the status sentence.
    expect(setup).toContain("qs-statusline");
    expect(setup).not.toContain("qs-odometer-col");
    expect(connecting).toContain("Connecting to server...");
  });

  it("keeps a cancel affordance and the key art in the frame", () => {
    const html = render();
    expect(html).toContain("qs-btn--cancel");
    expect(html).toContain("Cancel");
    expect(html).toContain("qs-poster");
    expect(html).toContain("qs-backdrop");
    expect(html).toContain("url(https://example.invalid/cover.jpg)");
  });

  it("falls back to a letter tile when the cover is missing", () => {
    const html = render({ gameCover: undefined });
    expect(html).toContain("qs-poster--fallback");
    expect(html).toContain("qs-poster-letter");
    expect(html).toContain(">C<");
  });

  it("renders the error state centered with its code and actions", () => {
    const html = render({
      error: {
        title: "No rig available",
        description: "Every rig in this region is busy.",
        code: "ERR_NO_CAPACITY",
        actionLabel: "Try again",
      },
      onErrorAction: () => {},
    });
    expect(html).toContain("qs-root--error");
    expect(html).toContain("qs-error-ring");
    expect(html).toContain("No rig available");
    expect(html).toContain("ERR_NO_CAPACITY");
    expect(html).toContain("Try again");
    expect(html).not.toContain("qs-stack");
  });

  it("surfaces the ad queue when the session requires ads", () => {
    const adState: SessionAdState = {
      isAdsRequired: true,
      message: "Watch to keep your place in line",
      sessionAds: [{ adId: "ad-1", title: "Sponsored break" }],
      ads: [],
    };
    const html = render({ adState });
    expect(html).toContain("qs-adzone");
    expect(html).toContain("Watch to keep your place in line");

    const paused = render({ adState: { ...adState, isQueuePaused: true } });
    expect(paused).toContain("Session queue paused");
  });
});
