import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { MotionProvider } from "./components/MotionProvider";
import { NativeOverlayRoot } from "./components/native-overlay/NativeOverlayRoot";
import { createTauriOverlayChannel } from "./components/native-overlay/nativeOverlayChannel";
import { applyAutomaticUiPerformanceMode } from "./lib/uiPerformance";
import "./styles.css";

/**
 * Transparent overlay window entry.
 *
 * Loaded by the Tauri shell into a second, always-on-top, click-through window
 * that floats over the native (NVST) video plane. Because it is a plain web
 * page it inherits the whole client design system — tokens, fonts, motion —
 * and the chrome can be as elaborate as the rest of OpenNOW.
 *
 * The window itself is created and positioned in `src-tauri/src/native_overlay.rs`;
 * this module only wires the host channel and mounts the overlay.
 */

applyAutomaticUiPerformanceMode();
document.documentElement.classList.add("nov-page");

const container = document.getElementById("root");
if (container) {
  createRoot(container).render(
    <StrictMode>
      <MotionProvider>
        <NativeOverlayRoot channel={createTauriOverlayChannel()} />
      </MotionProvider>
    </StrictMode>,
  );
}
