#!/usr/bin/env node
// Converts the web client's locale files into WinUI .resw resource files.
//
// locales/*.json uses nested objects ("app": { "actions": { "play": "Play" } }).
// .resw is flat, so nested keys are joined with "." — the same dotted key the
// React i18n layer uses — and every language gets the same key set so a missing
// translation falls back to English at build time rather than showing blank UI.
//
// Run with: node native/tools/sync-locales.mjs

import { readdir, readFile, writeFile, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, "../..");
const localesDir = resolve(repoRoot, "locales");
const outputRoot = resolve(repoRoot, "native/src/App/Strings");

function flatten(value, prefix, out) {
  if (value === null || value === undefined) return;
  if (typeof value === "string") {
    out.set(prefix, value);
    return;
  }
  if (typeof value === "object") {
    for (const [key, child] of Object.entries(value)) {
      flatten(child, prefix ? `${prefix}.${key}` : key, out);
    }
  }
}

function escapeXml(text) {
  return String(text)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    // .resw is XML: apostrophes and quotes are legal in element content.
    .replace(/\r\n/g, "\n");
}

function resw(entries) {
  let out = "";
  out += '<?xml version="1.0" encoding="utf-8"?>\n';
  // The comment goes after the declaration: an XML declaration must be the very
  // first thing in the document or the file will not parse.
  out += "<!-- GENERATED FILE — produced by native/tools/sync-locales.mjs from locales/*.json -->\n";
  out += "<root>\n";
  out += '  <xsd:schema id="root" xmlns="" xmlns:xsd="http://www.w3.org/2001/XMLSchema" '
       + 'xmlns:msdata="urn:schemas-microsoft-com:xml-msdata">\n';
  out += '    <xsd:import namespace="http://www.w3.org/XML/1998/namespace" />\n';
  out += '    <xsd:element name="root" msdata:IsDataSet="true">\n';
  out += '      <xsd:complexType>\n';
  out += '        <xsd:choice maxOccurs="unbounded">\n';
  out += '          <xsd:element name="metadata">\n';
  out += '            <xsd:complexType>\n';
  out += '              <xsd:sequence>\n';
  out += '                <xsd:element name="value" type="xsd:string" minOccurs="0" />\n';
  out += "              </xsd:sequence>\n";
  out += '              <xsd:attribute name="name" use="required" type="xsd:string" />\n';
  out += '              <xsd:attribute name="type" type="xsd:string" />\n';
  out += '              <xsd:attribute name="mimetype" type="xsd:string" />\n';
  out += "            </xsd:complexType>\n";
  out += "          </xsd:element>\n";
  out += '          <xsd:element name="assembly">\n';
  out += '            <xsd:complexType>\n';
  out += '              <xsd:attribute name="alias" type="xsd:string" />\n';
  out += '              <xsd:attribute name="name" type="xsd:string" />\n';
  out += "            </xsd:complexType>\n";
  out += "          </xsd:element>\n";
  out += '          <xsd:element name="data">\n';
  out += '            <xsd:complexType>\n';
  out += '              <xsd:sequence>\n';
  out += '                <xsd:element name="value" type="xsd:string" minOccurs="0" '
       + 'msdata:Ordinal="1" />\n';
  out += '                <xsd:element name="comment" type="xsd:string" minOccurs="0" '
       + 'msdata:Ordinal="2" />\n';
  out += "              </xsd:sequence>\n";
  out += '              <xsd:attribute name="name" type="xsd:string" use="required" '
       + 'msdata:Ordinal="1" />\n';
  out += '              <xsd:attribute name="type" type="xsd:string" msdata:Ordinal="3" />\n';
  out += '              <xsd:attribute name="mimetype" type="xsd:string" msdata:Ordinal="4" />\n';
  out += "            </xsd:complexType>\n";
  out += "          </xsd:element>\n";
  out += '          <xsd:element name="resheader">\n';
  out += '            <xsd:complexType>\n';
  out += '              <xsd:sequence>\n';
  out += '                <xsd:element name="value" type="xsd:string" minOccurs="0" '
       + 'msdata:Ordinal="1" />\n';
  out += "              </xsd:sequence>\n";
  out += '              <xsd:attribute name="name" type="xsd:string" use="required" />\n';
  out += "            </xsd:complexType>\n";
  out += "          </xsd:element>\n";
  out += "        </xsd:choice>\n";
  out += "      </xsd:complexType>\n";
  out += "    </xsd:element>\n";
  out += "  </xsd:schema>\n";
  out += '  <resheader name="resmimetype">\n    <value>text/microsoft-resx</value>\n  </resheader>\n';
  out += '  <resheader name="version">\n    <value>2.0</value>\n  </resheader>\n';
  out += '  <resheader name="reader">\n'
       + '    <value>System.Resources.ResXResourceReader, System.Windows.Forms, Version=4.0.0.0, '
       + "PublicKeyToken=b77a5c561934e089</value>\n  </resheader>\n";
  out += '  <resheader name="writer">\n'
       + '    <value>System.Resources.ResXResourceWriter, System.Windows.Forms, Version=4.0.0.0, '
       + "PublicKeyToken=b77a5c561934e089</value>\n  </resheader>\n";

  for (const [key, value] of entries) {
    out += `  <data name="${escapeXml(key)}" xml:space="preserve">\n`;
    out += `    <value>${escapeXml(value)}</value>\n`;
    out += `    <comment>GENERATED from locales/*.json — do not edit.</comment>\n`;
    out += "  </data>\n";
  }

  out += "</root>\n";
  return out;
}

const files = (await readdir(localesDir)).filter((name) => name.endsWith(".json")).sort();
if (files.length === 0) throw new Error("no locale files found");

const tables = new Map();
for (const file of files) {
  const language = file.replace(/\.json$/, "");
  const raw = JSON.parse(await readFile(resolve(localesDir, file), "utf8"));
  const table = new Map();
  flatten(raw, "", table);
  tables.set(language, table);
}

// English is the fallback: any key another language lacks is filled from it so
// the UI never renders an empty string.
const english = tables.get("en") ?? tables.values().next().value;

let totalKeys = 0;
for (const [language, table] of tables) {
  const merged = new Map();
  for (const [key, value] of table) {
    merged.set(key, value);
  }
  for (const [key, value] of english) {
    if (!merged.has(key)) merged.set(key, value);
  }
  const sorted = new Map([...merged.entries()].sort((a, b) => (a[0] < b[0] ? -1 : 1)));
  totalKeys = sorted.size;

  const directory = resolve(outputRoot, language);
  await mkdir(directory, { recursive: true });
  await writeFile(resolve(directory, "Resources.resw"), resw(sorted), "utf8");
  console.log(`${language}: ${sorted.size} strings`);
}

console.log(`\n${tables.size} languages, ${totalKeys} strings each -> ${outputRoot}`);
