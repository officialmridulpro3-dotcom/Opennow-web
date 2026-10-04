import { AnimatePresence, m } from "motion/react";
import { AlertTriangle, CheckCircle2, Info, X } from "lucide-react";
import type { JSX } from "react";
import type { NativeOverlayToast } from "./types";

export interface NativeToastStackProps {
  toasts: NativeOverlayToast[];
  onDismiss: (id: string) => void;
}

const ICONS = {
  info: Info,
  success: CheckCircle2,
  warning: AlertTriangle,
} as const;

/** Bottom-centred notification stack — "recording saved", "clipboard pasted", … */
export function NativeToastStack({ toasts, onDismiss }: NativeToastStackProps): JSX.Element {
  return (
    <div className="nov-toasts" role="status" aria-live="polite">
      <AnimatePresence initial={false}>
        {toasts.map((toast) => {
          const Icon = ICONS[toast.kind];
          return (
            <m.div
              key={toast.id}
              className="nov-toast"
              data-kind={toast.kind}
              layout
              initial={{ opacity: 0, y: 18, scale: 0.97 }}
              animate={{ opacity: 1, y: 0, scale: 1 }}
              exit={{ opacity: 0, y: 10, scale: 0.98 }}
              transition={{ duration: 0.28, ease: [0.22, 0.61, 0.36, 1] }}
            >
              <span className="nov-toast__icon">
                <Icon size={15} aria-hidden="true" />
              </span>
              <span className="nov-toast__text">
                <span className="nov-toast__title">{toast.title}</span>
                {toast.detail ? <span className="nov-toast__detail">{toast.detail}</span> : null}
              </span>
              <button
                type="button"
                className="nov-icon-btn"
                style={{ width: 24, height: 24, border: "none", background: "transparent" }}
                onClick={() => onDismiss(toast.id)}
                aria-label="Dismiss notification"
              >
                <X size={13} />
              </button>
            </m.div>
          );
        })}
      </AnimatePresence>
    </div>
  );
}
