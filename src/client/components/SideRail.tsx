import type { AuthUser } from "@shared/gfn";
import { House, Library, Settings, Timer, User } from "lucide-react";
import type { JSX, Ref } from "react";
import { useTranslation } from "../i18n";
import { OpenNowLogoMark } from "./OpenNowLogoMark";

export type SideRailPage = "home" | "library" | "playtime" | "settings";

interface SideRailProps {
  currentPage: SideRailPage;
  onNavigate: (page: SideRailPage) => void;
  user: AuthUser | null;
  onOpenAccount: () => void;
  avatarRef?: Ref<HTMLButtonElement>;
}

export function SideRail({ currentPage, onNavigate, user, onOpenAccount, avatarRef }: SideRailProps): JSX.Element {
  const { t } = useTranslation();

  const navItems: Array<{
    id: string;
    label: string;
    icon: typeof House;
    page?: SideRailPage;
    disabled?: boolean;
  }> = [
    { id: "home", label: t("navigation.home"), icon: House, page: "home" },
    { id: "library", label: t("navigation.library"), icon: Library, page: "library" },
    { id: "playtime", label: t("navigation.playtime"), icon: Timer, page: "playtime" },
    { id: "settings", label: t("navigation.settings"), icon: Settings, page: "settings" },
  ];

  const avatarInitial = (user?.displayName?.trim()?.[0] ?? "?").toUpperCase();

  return (
    <aside className="side-rail" aria-label="Primary">
      <div className="side-rail-top">
        <div className="side-rail-brand">
          <button
            type="button"
            className="side-rail-logo"
            onClick={() => onNavigate("home")}
            aria-label="OpenNOW home"
            title="OpenNOW"
          >
            <OpenNowLogoMark className="side-rail-logo-mark" />
          </button>
          <div className="side-rail-brand-copy" aria-hidden="true">
            <span className="side-rail-brand-name">OpenNOW</span>
            <span className="side-rail-brand-caption">GAME LIBRARY</span>
          </div>
        </div>

        <nav className="side-rail-nav">
          {navItems.map((item) => {
            const Icon = item.icon;
            const isActive = !item.disabled && item.page !== undefined && currentPage === item.page;
            return (
              <button
                key={item.id}
                type="button"
                className={`side-rail-item${isActive ? " active" : ""}${item.disabled ? " disabled" : ""}`}
                onClick={() => {
                  if (item.disabled || !item.page) return;
                  onNavigate(item.page);
                }}
                aria-label={item.label}
                aria-current={isActive ? "page" : undefined}
                title={item.label}
                disabled={item.disabled}
              >
                <Icon size={19} aria-hidden="true" />
                <span className="side-rail-item-label">{item.label}</span>
              </button>
            );
          })}
        </nav>
      </div>

      <div className="side-rail-bottom">
        <div className="side-rail-account">
          <button
            type="button"
            ref={avatarRef}
            className="side-rail-avatar"
            onClick={onOpenAccount}
            aria-label={user?.displayName ?? t("auth.accounts.guest")}
            title={user?.displayName ?? t("auth.accounts.guest")}
          >
            {user?.avatarUrl ? (
              <img src={user.avatarUrl} alt="" className="side-rail-avatar-img" />
            ) : user ? (
              <span className="side-rail-avatar-initial">{avatarInitial}</span>
            ) : (
              <User size={18} />
            )}
          </button>
          <span className="side-rail-account-copy" title={user?.displayName ?? t("auth.accounts.guest")}>
            {user?.displayName ?? t("auth.accounts.guest")}
          </span>
        </div>
      </div>
    </aside>
  );
}
