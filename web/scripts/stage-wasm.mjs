import { mkdir, copyFile, access, readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
const source = resolve(process.env.WASM_DIR || "../build-wasm/wasm");
const target = resolve("public/simulator");
const assets = ["neospice.mjs", "neospice.wasm", "neospice-api.mjs"];
try {
  for (const asset of assets) await access(resolve(source, asset));
} catch {
  throw new Error(
    `WASM artifacts missing from ${source}. Build with Emscripten first (see docs/webassembly.md), or set WASM_DIR.`,
  );
}
await mkdir(target, { recursive: true });
for (const asset of assets)
  await copyFile(resolve(source, asset), resolve(target, asset));
console.log(`Staged simulator from ${source}`);

const notices = [];
for (const file of ["LICENSE", "NOTICE", "CREDITS.md"]) {
  notices.push(
    `neospice — ${file}\n${await readFile(resolve("..", file), "utf8")}`,
  );
}
for (const name of [
  "react",
  "react-dom",
  "scheduler",
  "lucide-react",
  "uplot",
]) {
  const directory = resolve("node_modules", name);
  const pkg = JSON.parse(
    await readFile(resolve(directory, "package.json"), "utf8"),
  );
  notices.push(
    `${name} ${pkg.version}\n${await readFile(resolve(directory, "LICENSE"), "utf8")}`,
  );
}
await writeFile(
  resolve("public/licenses.txt"),
  notices.join("\n\n----------------------------------------\n\n"),
);
