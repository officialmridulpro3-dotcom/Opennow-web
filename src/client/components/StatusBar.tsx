import type { JSX } from "react";
import { useTranslation } from "../i18n";

interface StatusBarProps {
  regionLabel?: string;
  themeLabel?: string;
}

interface Hint {
  keys: string[];
  label: string;
}

export function StatusBar({ regionLabel, themeLabel }: StatusBarProps): JSX.Element {
  const { t } = useTranslation();

  const hints: Hint[] = [
    { keys: ["Arrows"], label: t("statusbar.hint.move") },
    { keys: ["Enter"], label: t("app.actions.play") },
    { keys: ["/"], label: t("app.actions.search") },
    { keys: ["Ctrl", "K"], label: t("statusbar.hint.commands") },
    { keys: ["?"], label: t("statusbar.hint.allShortcuts") },
  ];

  return (
    <footer className="status-bar" aria-hidden="true">
      <div className="status-bar-hints">
        {hints.map((hint) => (
          <div key={hint.label} className="status-bar-hint">
            <span className="status-bar-keys">
              {hint.keys.map((key) => (
                <kbd key={key}>{key}</kbd>
              ))}
            </span>
            <span className="status-bar-hint-label">{hint.label}</span>
          </div>
        ))}
      </div>

      <div className="status-bar-meta">
        <span className="status-bar-meta-item">
          <span className="status-bar-dot" />
          {regionLabel ?? t("statusbar.regionAuto")}
        </span>
        <span className="status-bar-meta-sep" />
        <span className="status-bar-meta-item status-bar-meta-item--muted">
          {themeLabel ?? t("statusbar.themeDeck")}
        </span>
      </div>
    </footer>
  );
}
