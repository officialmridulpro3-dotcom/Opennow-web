import { useCallback, useEffect, useRef, useState, type JSX } from "react";
import { AnimatePresence, m } from "motion/react";
import { NativeStreamDeck } from "./NativeStreamDeck";
import { NativeStatsHud } from "./NativeStatsHud";
import { NativeToastStack } from "./NativeToastStack";
import { NativeExitDialog } from "./NativeExitDialog";
import {
  defaultOverlayToggles,
  emptyOverlaySession,
  emptyOverlayStats,
  type NativeOverlayAction,
  type NativeOverlayChannel,
  type NativeOverlayMessage,
  type NativeOverlaySession,
  type NativeOverlayStats,
  type NativeOverlayTab,
  type NativeOverlayToast,
  type NativeOverlayToggles,
} from "./types";
import "./native-overlay.css";

export interface NativeOverlayRootProps {
  channel: NativeOverlayChannel;
  /** Preview/debug: start with the deck open and the HUD visible. */
  initialDeckOpen?: boolean;
  initialHudVisible?: boolean;
  initialSession?: Partial<NativeOverlaySession>;
  initialStats?: Partial<NativeOverlayStats>;
  initialTab?: NativeOverlayTab;
}

const STATS_HISTORY_LENGTH = 48;

/**
 * Overlay root — owns all overlay state, keyboard handling and host messaging.
 *
 * Lives in the transparent overlay window that the Tauri shell floats above the
 * native video plane, so every pixel here is real DOM/CSS rather than a Win32
 * draw call. The same component renders in the preview harness against a mock
 * channel.
 */
export function NativeOverlayRoot({
  channel,
  initialDeckOpen = false,
  initialHudVisible = false,
  initialSession,
  initialStats,
  initialTab = "session",
}: NativeOverlayRootProps): JSX.Element {
  const [deckOpen, setDeckOpen] = useState(initialDeckOpen);
  const [hudVisible, setHudVisible] = useState(initialHudVisible);
  const [hudExpanded, setHudExpanded] = useState(false);
  const [tab, setTab] = useState<NativeOverlayTab>(initialTab);
  const [session, setSession] = useState<NativeOverlaySession>({
    ...emptyOverlaySession,
    ...initialSession,
  });
  const [stats, setStats] = useState<NativeOverlayStats>({ ...emptyOverlayStats, ...initialStats });
  const [toggles, setToggles] = useState<NativeOverlayToggles>({
    ...defaultOverlayToggles,
    hudVisible: initialHudVisible,
  });
  const [toasts, setToasts] = useState<NativeOverlayToast[]>([]);
  const [history, setHistory] = useState<number[]>([]);
  const [exitOpen, setExitOpen] = useState(false);
  const [now, setNow] = useState(() => Date.now());

  const toastSeq = useRef(0);
  const latestBitrate = useRef(0);

  /* ------------------------------- toasts -------------------------------- */

  const pushToast = useCallback((toast: Omit<NativeOverlayToast, "id">) => {
    toastSeq.current += 1;
    const id = `nov-toast-${toastSeq.current}`;
    setToasts((current) => [...current.slice(-3), { ...toast, id }]);
    const ttl = toast.ttlMs ?? 4200;
    if (ttl > 0) {
      window.setTimeout(() => setToasts((current) => current.filter((entry) => entry.id !== id)), ttl);
    }
  }, []);

  const dismissToast = useCallback((id: string) => {
    setToasts((current) => current.filter((entry) => entry.id !== id));
  }, []);

  /* --------------------------- host messages ----------------------------- */

  const handleMessage = useCallback(
    (message: NativeOverlayMessage) => {
      switch (message.kind) {
        case "session":
          setSession((current) => ({ ...current, ...message.session }));
          break;
        case "stats":
          setStats((current) => ({ ...current, ...message.stats }));
          if (typeof message.stats.bitrateKbps === "number") {
            latestBitrate.current = message.stats.bitrateKbps / 1000;
          }
          break;
        case "toggles":
          setToggles((current) => ({ ...current, ...message.toggles }));
          if (typeof message.toggles.hudVisible === "boolean") {
            setHudVisible(message.toggles.hudVisible);
          }
          break;
        case "toast":
          pushToast(message.toast);
          break;
        case "command":
          applyCommand(message.command);
          break;
        default:
          break;
      }

      function applyCommand(command: string): void {
        if (command === "toggle-deck") setDeckOpen((open) => !open);
        else if (command === "open-deck") setDeckOpen(true);
        else if (command === "close-deck") setDeckOpen(false);
        else if (command === "toggle-hud") setHudVisible((visible) => !visible);
        else if (command === "hud-on") setHudVisible(true);
        else if (command === "hud-off") setHudVisible(false);
      }
    },
    [pushToast],
  );

  useEffect(() => channel.subscribe(handleMessage), [channel, handleMessage]);

  useEffect(() => {
    // Tells the shell the page is alive so it can flush the commands that
    // arrived while the overlay window was still booting.
    channel.signalReady?.();
  }, [channel]);

  /* ------------------------- visibility reporting ------------------------ */

  useEffect(() => {
    // The shell uses this to switch the window between interactive (deck) and
    // click-through (HUD only), and to hide it entirely when idle.
    channel.reportVisibility({ deckOpen, hudVisible: hudVisible || toasts.length > 0 });
  }, [channel, deckOpen, hudVisible, toasts.length]);

  useEffect(() => {
    setToggles((current) => ({ ...current, hudVisible }));
  }, [hudVisible]);

  /* ------------------------------- timers -------------------------------- */

  useEffect(() => {
    const timer = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(timer);
  }, []);

  useEffect(() => {
    if (!hudVisible) return;
    const timer = window.setInterval(() => {
      setHistory((current) => [...current, latestBitrate.current].slice(-STATS_HISTORY_LENGTH));
    }, 1000);
    return () => window.clearInterval(timer);
  }, [hudVisible]);

  /* ------------------------------ actions -------------------------------- */

  const handleAction = useCallback(
    (action: NativeOverlayAction, value?: number) => {
      channel.dispatch(action, value);
    },
    [channel],
  );

  const closeDeck = useCallback(() => {
    setDeckOpen(false);
    channel.dispatch("resume");
  }, [channel]);

  const confirmEnd = useCallback(() => {
    setExitOpen(false);
    setDeckOpen(false);
    channel.dispatch("end-session");
  }, [channel]);

  /* ----------------------------- keyboard -------------------------------- */

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent): void => {
      const ctrl = event.ctrlKey || event.metaKey;
      if (ctrl && event.key.toLowerCase() === "g") {
        event.preventDefault();
        setDeckOpen((open) => !open);
        return;
      }
      if (ctrl && event.key.toLowerCase() === "n") {
        event.preventDefault();
        setHudVisible((visible) => !visible);
        return;
      }
      if (event.key === "Escape") {
        event.preventDefault();
        if (exitOpen) {
          setExitOpen(false);
        } else if (deckOpen) {
          closeDeck();
        } else if (hudVisible) {
          setHudVisible(false);
        }
        return;
      }
      if (!deckOpen) return;
      if (event.key === "ArrowRight" || event.key === "ArrowLeft") {
        event.preventDefault();
        setTab((current) => {
          const order: NativeOverlayTab[] = ["session", "controls", "media", "keys"];
          const index = order.indexOf(current);
          const next = event.key === "ArrowRight" ? index + 1 : index - 1;
          return order[(next + order.length) % order.length];
        });
        return;
      }
      if (event.key === "Tab") {
        // Keep focus inside the deck — the video plane must never steal it
        // while the deck is open.
        const deck = document.querySelector<HTMLElement>(".nov-deck");
        if (!deck) return;
        const focusable = Array.from(
          deck.querySelectorAll<HTMLElement>(
            'button:not(:disabled), input:not(:disabled), [tabindex="0"]',
          ),
        );
        if (focusable.length === 0) return;
        const active = document.activeElement as HTMLElement | null;
        const index = active ? focusable.indexOf(active) : -1;
        const step = event.shiftKey ? -1 : 1;
        const nextIndex = (index + step + focusable.length) % focusable.length;
        event.preventDefault();
        focusable[nextIndex].focus({ preventScroll: true });
      }
    };

    window.addEventListener("keydown", onKeyDown, true);
    return () => window.removeEventListener("keydown", onKeyDown, true);
  }, [closeDeck, deckOpen, exitOpen, hudVisible]);

  /* -------------------------------- render ------------------------------- */

  const scrimVisible = deckOpen && !exitOpen;

  return (
    <div className="nov-root">
      <AnimatePresence>
        {scrimVisible ? (
          <m.div
            className="nov-scrim"
            key="nov-scrim"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            transition={{ duration: 0.24 }}
            onClick={closeDeck}
          />
        ) : null}
      </AnimatePresence>

      <AnimatePresence>
        {deckOpen ? (
          <m.div
            key="nov-deck-wrap"
            style={{ position: "absolute", inset: 0, pointerEvents: "none" }}
            initial={{ x: -48, opacity: 0 }}
            animate={{ x: 0, opacity: 1 }}
            exit={{ x: -56, opacity: 0 }}
            transition={{ duration: 0.32, ease: [0.22, 0.61, 0.36, 1] }}
          >
            <NativeStreamDeck
              visible
              session={session}
              stats={stats}
              toggles={toggles}
              tab={tab}
              now={now}
              onTabChange={setTab}
              onAction={handleAction}
              onClose={closeDeck}
              onToggleHud={() => setHudVisible((visible) => !visible)}
              onRequestEnd={() => setExitOpen(true)}
            />
          </m.div>
        ) : null}
      </AnimatePresence>

      <AnimatePresence>
        {hudVisible && !deckOpen ? (
          <m.div
            key="nov-hud-wrap"
            initial={{ opacity: 0, y: -10 }}
            animate={{ opacity: 1, y: 0 }}
            exit={{ opacity: 0, y: -8 }}
            transition={{ duration: 0.24, ease: [0.22, 0.61, 0.36, 1] }}
          >
            <NativeStatsHud
              visible
              stats={stats}
              session={session}
              history={history}
              expanded={hudExpanded}
              onToggleExpanded={() => setHudExpanded((expanded) => !expanded)}
              onClose={() => setHudVisible(false)}
            />
          </m.div>
        ) : null}
      </AnimatePresence>

      <NativeToastStack toasts={toasts} onDismiss={dismissToast} />

      <NativeExitDialog
        open={exitOpen}
        gameTitle={session.gameTitle || "This game"}
        onCancel={() => setExitOpen(false)}
        onConfirm={confirmEnd}
      />

      {/* Screen-reader announcement for the shell-driven open/close path. */}
      <span hidden aria-live="polite">
        {deckOpen ? "Stream deck open" : ""}
      </span>
    </div>
  );
}
