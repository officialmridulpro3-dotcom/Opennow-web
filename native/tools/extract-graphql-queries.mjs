#!/usr/bin/env node
// Copies the LCARS GraphQL query text out of the TypeScript client into a C++
// header the native catalog service can include.
//
// NVIDIA serves the catalog through *persisted* queries: normally we only send
// the sha256 hash of a query. When the server no longer knows that hash (it
// returns HTTP 400), the documented recovery is to re-send the full query text.
// Keeping the text in sync with the web client by extracting it at build time
// means the two clients can never drift apart.

import { readFile, writeFile, mkdir } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = resolve(here, "../..");
const sourceFile = resolve(repoRoot, "src/server/gfn/lcarsGraphql.ts");
const outputFile = resolve(repoRoot, "native/src/Core/include/opennow/core/gfn/GeneratedQueries.inc");

const NAMES = [
  ["GAME_SECTION_QUERY", "kGameSectionQuery", "Main / LibrarySection panel query"],
  ["MARQUEE_QUERY", "kMarqueeQuery", "Home marquee query"],
  ["LIBRARY_SECTION_WITH_TIME_QUERY", "kLibrarySectionWithTimeQuery", "Library panel with playtime"],
  ["FILTER_GROUP_AND_SORT_QUERY", "kFilterGroupAndSortQuery", "Filter and sort definitions"],
  ["APP_DATA_FOR_APP_ID_QUERY", "kAppDataForAppIdQuery", "Single app details"],
  ["ADD_OWNED_VARIANT_MUTATION", "kAddOwnedVariantMutation", "Mark a store variant owned"],
];

const source = await readFile(sourceFile, "utf8");

function extract(name) {
  const start = source.indexOf(`const ${name} = `);
  if (start === -1) throw new Error(`query ${name} not found in lcarsGraphql.ts`);
  const tickStart = source.indexOf("`", start);
  if (tickStart === -1) throw new Error(`query ${name} is not a template literal`);
  const tickEnd = source.indexOf("`", tickStart + 1);
  if (tickEnd === -1) throw new Error(`query ${name} has no closing backtick`);
  return source.slice(tickStart + 1, tickEnd).trim();
}

let out = "";
out += "// GENERATED FILE — do not edit.\n";
out += "// Produced by native/tools/extract-graphql-queries.mjs from\n";
out += "// src/server/gfn/lcarsGraphql.ts. Re-run the script after changing the\n";
out += "// queries in the TypeScript client.\n";
out += "#pragma once\n\n";

for (const [sourceName, cppName, comment] of NAMES) {
  const text = extract(sourceName);
  // Guard against a stray backtick or ') sequence inside the query text.
  const delimiter = /\)\"/.test(text) || text.includes(")") === false ? "GRAPHQL" : "GRAPHQL";
  out += `// ${comment}\n`;
  out += `inline constexpr const char* ${cppName} = R"${delimiter}(${text})${delimiter}";\n\n`;
}

out += `inline const char* FullQueryText(const char* queryName) {\n`;
out += `  struct Entry { const char* name; const char* text; };\n`;
out += `  static constexpr Entry kEntries[] = {\n`;
for (const [, cppName] of NAMES) {
  const logical = cppName.replace(/^k/, "").replace(/Query$/, "").replace(/Mutation$/, "");
  out += `      {"${logical}", ${cppName}},\n`;
}
out += `  };\n`;
out += `  for (const Entry& entry : kEntries) {\n`;
out += `    if (entry.name == std::string_view(queryName)) return entry.text;\n`;
out += `  }\n`;
out += `  return nullptr;\n`;
out += `}\n`;

await mkdir(dirname(outputFile), { recursive: true });
await writeFile(outputFile, out, "utf8");
console.log(`wrote ${outputFile}`);
