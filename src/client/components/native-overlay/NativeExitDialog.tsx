import { AnimatePresence, m } from "motion/react";
import { Power } from "lucide-react";
import type { JSX } from "react";

export interface NativeExitDialogProps {
  open: boolean;
  gameTitle: string;
  onCancel: () => void;
  onConfirm: () => void;
}

/** Confirmation shown before the deck's "End session" tears the seat down. */
export function NativeExitDialog({
  open,
  gameTitle,
  onCancel,
  onConfirm,
}: NativeExitDialogProps): JSX.Element {
  return (
    <AnimatePresence>
      {open ? (
        <m.div
          className="nov-dialog-scrim"
          initial={{ opacity: 0 }}
          animate={{ opacity: 1 }}
          exit={{ opacity: 0 }}
          transition={{ duration: 0.2 }}
          onClick={onCancel}
        >
          <m.div
            className="nov-dialog"
            role="alertdialog"
            aria-modal="true"
            aria-label="End stream session"
            initial={{ opacity: 0, y: 16, scale: 0.97 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, y: 10, scale: 0.98 }}
            transition={{ duration: 0.26, ease: [0.22, 0.61, 0.36, 1] }}
            onClick={(event) => event.stopPropagation()}
          >
            <span className="nov-dialog__icon">
              <Power size={18} aria-hidden="true" />
            </span>
            <h2 className="nov-dialog__title">End this session?</h2>
            <p className="nov-dialog__text">
              {gameTitle} will be closed on the GeForce NOW rig and the seat is released. Unsaved
              progress in the game is lost — GeForce NOW does not keep the rig running in the
              background.
            </p>
            <div className="nov-dialog__actions">
              <button type="button" className="nov-btn" onClick={onCancel}>
                Keep playing
              </button>
              <button type="button" className="nov-btn nov-btn--ghost-danger" onClick={onConfirm}>
                End session
              </button>
            </div>
          </m.div>
        </m.div>
      ) : null}
    </AnimatePresence>
  );
}
