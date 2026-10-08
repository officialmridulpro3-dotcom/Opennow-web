/**
 * Apply the low-cost UI treatment before React paints. Desktop WebView2 is
 * deliberately kept in this profile to leave CPU/GPU headroom for the native
 * game window; browser builds opt in when hardware or data-saver signals look
 * constrained. The stream decoder itself is unaffected.
 */
export function applyAutomaticUiPerformanceMode(): boolean {
  if (typeof navigator === "undefined" || typeof document === "undefined") {
    return false;
  }

  const capabilities = navigator as Navigator & {
    deviceMemory?: number;
    connection?: { saveData?: boolean };
  };
  const cores = capabilities.hardwareConcurrency;
  const memoryGb = capabilities.deviceMemory;
  const isDesktopShell = typeof window !== "undefined"
    && Boolean((window as Window & { __TAURI__?: unknown }).__TAURI__);
  const isConstrained = isDesktopShell
    || capabilities.connection?.saveData === true
    || (Number.isFinite(cores) && cores > 0 && cores <= 4)
    || (typeof memoryGb === "number" && memoryGb > 0 && memoryGb <= 4);

  if (isConstrained) {
    document.documentElement.dataset.uiPerformance = "lite";
  }

  return isConstrained;
}
