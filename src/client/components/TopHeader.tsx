import { Search } from "lucide-react";
import { useEffect, useRef, type JSX } from "react";
import { useTranslation } from "../i18n";

interface TopHeaderProps {
  title: string;
  count: number | null;
  countLabel?: string;
  searchQuery: string;
  onSearchChange: (query: string) => void;
  searchPlaceholder: string;
  /** Pages with nothing to filter (Playtime) drop the search field entirely. */
  hideSearch?: boolean;
}

export function TopHeader({
  title,
  count,
  countLabel,
  searchQuery,
  onSearchChange,
  searchPlaceholder,
  hideSearch = false,
}: TopHeaderProps): JSX.Element {
  const { t } = useTranslation();
  const inputRef = useRef<HTMLInputElement | null>(null);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const isTyping = target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.isContentEditable);
      if (event.key === "/" && !isTyping) {
        event.preventDefault();
        inputRef.current?.focus();
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, []);

  const resolvedCountLabel = countLabel ?? (count !== null ? t("library.gameCount", { count }) : "");

  return (
    <header className="top-header">
      <div className="top-header-titles">
        <h1 className="top-header-title">{title}</h1>
        {resolvedCountLabel && <span className="top-header-count">{resolvedCountLabel}</span>}
      </div>

      {!hideSearch && (
      <div className="top-header-search">
        <Search className="top-header-search-icon" size={16} />
        <input
          ref={inputRef}
          type="text"
          className="top-header-search-input"
          placeholder={searchPlaceholder}
          value={searchQuery}
          onChange={(event) => onSearchChange(event.target.value)}
          aria-label={searchPlaceholder}
        />
        <kbd className="top-header-search-kbd">/</kbd>
      </div>
      )}
    </header>
  );
}
