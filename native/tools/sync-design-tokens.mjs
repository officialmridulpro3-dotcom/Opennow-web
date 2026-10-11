#!/usr/bin/env node
// Generates the WinUI theme resource dictionary from the web client's design
// tokens, so the native app cannot drift from the web UI.
//
// Reads the `:root` block of src/client/styles.css, resolves `color-mix()` and
// `var()` references to concrete colours, and writes
// native/src/App/Themes/GeneratedTokens.xaml with:
//   * Color resources for every palette entry
//   * SolidColorBrush + LinearGradientBrush resources for the layered surfaces
//   * CornerRadius, Thickness and duration resources for the layout tokens
//
// Run `npm run design:sync` (or node native/tools/sync-design-tokens.mjs).

import { readFile, writeFile, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, "../..");
const cssPath = resolve(repoRoot, "src/client/styles.css");
const outPath = resolve(repoRoot, "native/src/App/Themes/GeneratedTokens.xaml");

const css = await readFile(cssPath, "utf8");

// ---- token extraction -----------------------------------------------------------

const rootMatch = css.match(/:root\s*\{([\s\S]*?)\n\}/);
if (!rootMatch) throw new Error("could not find the :root block in styles.css");

const rawTokens = new Map();
for (const line of rootMatch[1].split("\n")) {
  const match = line.match(/^\s*(--[\w-]+)\s*:\s*([^;]+);/);
  if (match) rawTokens.set(match[1], match[2].trim());
}

// ---- colour parsing ----------------------------------------------------------------

function parseHex(hex) {
  const value = hex.replace("#", "").trim();
  if (value.length === 3) {
    return value.split("").map((c) => parseInt(c + c, 16));
  }
  if (value.length === 6) {
    return [0, 2, 4].map((i) => parseInt(value.slice(i, i + 2), 16));
  }
  if (value.length === 8) {
    return [0, 2, 4].map((i) => parseInt(value.slice(i, i + 2), 16));
  }
  return null;
}

function mix(a, b, weight) {
  return a.map((channel, index) => Math.round(channel * weight + b[index] * (1 - weight)));
}

function toHex(rgb) {
  return "#" + rgb.map((c) => Math.max(0, Math.min(255, c)).toString(16).padStart(2, "0")).join("");
}

function resolveColor(value, depth = 0) {
  if (depth > 8) return null;
  const text = value.trim();

  const hex = text.match(/^#([0-9a-fA-F]{3,8})\b/);
  if (hex) return parseHex(hex[0]);

  const rgba = text.match(/^rgba?\(\s*([^)]+)\)/);
  if (rgba) {
    const parts = rgba[1].split(",").map((p) => p.trim());
    const base = parts[0].startsWith("#")
      ? parseHex(parts[0])
      : parts.slice(0, 3).map((p) => Number.parseInt(p, 10));
    if (!base || base.some((c) => Number.isNaN(c))) return null;
    return base.slice(0, 3);
  }

  const colorMix = text.match(/^color-mix\(in srgb,\s*(.+?)\s+(\d+)%,\s*(.+?)\s*\)$/);
  if (colorMix) {
    const [, leftRaw, percentRaw, rightRaw] = colorMix;
    const weight = Number.parseInt(percentRaw, 10) / 100;
    const left = resolveTokenColor(leftRaw.trim(), depth + 1);
    const right = resolveTokenColor(rightRaw.trim(), depth + 1);
    if (!left || !right) return null;
    return mix(left, right, weight);
  }

  return resolveTokenColor(text, depth + 1);
}

function resolveTokenColor(value, depth) {
  if (value.startsWith("var(")) {
    const name = value.match(/var\(\s*(--[\w-]+)/)?.[1];
    if (!name || !rawTokens.has(name)) return null;
    return resolveColor(rawTokens.get(name), depth + 1);
  }
  if (rawTokens.has(value)) return resolveColor(rawTokens.get(value), depth + 1);
  const hex = value.match(/^#([0-9a-fA-F]{3,8})$/);
  if (hex) return parseHex(value);
  return null;
}

// ---- selection of what becomes a resource -------------------------------------------

const COLOR_TOKENS = [
  "color-void", "color-ultra", "color-steel", "color-azure", "color-neon", "color-light",
  "bg-a", "bg-b", "bg-c",
  "panel", "panel-border-solid", "card", "card-hover", "card-selected", "chip",
  "ink", "ink-soft", "ink-muted",
  "accent", "accent-hover", "accent-press", "accent-on",
  "error", "warning", "info", "success",
];

const ALPHA_TOKENS = {
  "panel-border": 0.22,
  "accent-glow": 0.14,
  "accent-surface": 0.08,
  "accent-surface-strong": 0.16,
};

const SIZE_TOKENS = ["r-sm", "r-md", "r-lg", "r-xl", "r-2xl", "r-full", "r-pill"];
const LAYOUT_TOKENS = ["navbar-h", "rail-w", "header-h", "statusbar-h"];

function pascal(name) {
  return name
    .replace(/^--/, "")
    .split("-")
    .map((part, index) => (index === 0 ? part : part[0].toUpperCase() + part.slice(1)))
    .join("");
}

// ---- emit -------------------------------------------------------------------------

let out = "";
out += "<!-- GENERATED FILE — do not edit.\n";
out += "     Produced by native/tools/sync-design-tokens.mjs from src/client/styles.css.\n";
out += `     ${COLOR_TOKENS.length} colours, ${Object.keys(ALPHA_TOKENS).length} alpha brushes, `;
out += `${SIZE_TOKENS.length} radii, ${LAYOUT_TOKENS.length} layout metrics. -->\n`;
out += '<ResourceDictionary\n';
out += '    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"\n';
out += '    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n\n';

out += "    <!-- Palette -->\n";
for (const token of COLOR_TOKENS) {
  const rgb = resolveColor(`var(--${token})`);
  if (!rgb) continue;
  // Windows.UI.Color is not in the presentation XML namespace, so the hex goes
  // straight into the brush instead of through a Color resource.
  out += `    <SolidColorBrush x:Key="${pascal(token)}Brush" Color="${toHex(rgb)}" />\n`;
}

out += "\n    <!-- Alpha surfaces (kept as brushes so XAML opacity animation works) -->\n";
for (const [token, alpha] of Object.entries(ALPHA_TOKENS)) {
  const rgb = resolveColor(`var(--${token})`);
  if (!rgb) continue;
  out += `    <SolidColorBrush x:Key="${pascal(token)}Brush" Color="${toHex(rgb)}" Opacity="${alpha}" />\n`;
}

out += "\n    <!-- Corner radii -->\n";
for (const token of SIZE_TOKENS) {
  const raw = rawTokens.get(`--${token}`) ?? "0";
  const value = raw === "50%" ? "999" : raw.replace("px", "");
  out += `    <CornerRadius x:Key="${pascal(token)}">${value}</CornerRadius>\n`;
}

out += "\n    <!-- Layout metrics -->\n";
for (const token of LAYOUT_TOKENS) {
  const value = (rawTokens.get(`--${token}`) ?? "0").replace("px", "");
  out += `    <x:Double x:Key="${pascal(token)}">${value}</x:Double>\n`;
}

const shadows = [
  ["shadow-sm", "0,4,12", "0.22"],
  ["shadow-md", "0,12,28", "0.28"],
  ["shadow-lg", "0,20,48", "0.34"],
];
out += "\n    <!-- Elevation: XAML has no CSS box-shadow, so these drive ThemeShadow\n";
out += "         depth and the drop-shadow brushes used by elevated cards. -->\n";
for (const [token, offset, opacity] of shadows) {
  out += `    <x:String x:Key="${pascal(token)}">${offset}|${opacity}</x:String>\n`;
}

out += "\n    <!-- Motion: the web client's easing curves, expressed for WinUI -->\n";
out += '    <x:String x:Key="EaseStandard">0.22,0.61,0.36,1</x:String>\n';
out += '    <x:String x:Key="EaseSpring">0.34,1.56,0.64,1</x:String>\n';
out += '    <Duration x:Key="DurationFast">0:0:0.2</Duration>\n';
out += '    <Duration x:Key="DurationNormal">0:0:0.3</Duration>\n';
out += '    <Duration x:Key="DurationSlow">0:0:0.4</Duration>\n';

out += "\n</ResourceDictionary>\n";

await mkdir(dirname(outPath), { recursive: true });
await writeFile(outPath, out, "utf8");
console.log(`wrote ${outPath}`);
