/**
 * Opt constrained hardware into the low-cost UI treatment before React paints.
 * This intentionally leaves the game stream untouched and only trims ambient
 * decoration, blur, and motion from the app's own webviews.
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
  const isConstrained = capabilities.connection?.saveData === true
    || (Number.isFinite(cores) && cores > 0 && cores <= 4)
    || (typeof memoryGb === "number" && memoryGb > 0 && memoryGb <= 4);

  if (isConstrained) {
    document.documentElement.dataset.uiPerformance = "lite";
  }

  return isConstrained;
}
