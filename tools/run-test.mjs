// Runs build/out/test/test.html in headless Chromium with WebGPU enabled and relays its console until the test reports PASSED or FAILED.
// Usage: node tools/run-test.mjs [build-dir]   (needs playwright-core resolvable from build/node, see tools/run-test.sh)
import { createServer } from "node:http";
import { readFile } from "node:fs/promises";
import { createRequire } from "node:module";
import { existsSync, readdirSync } from "node:fs";
import path from "node:path";
import os from "node:os";

const buildDir = path.resolve(process.argv[2] ?? path.join(path.dirname(new URL(import.meta.url).pathname), "..", "build", "out"));
const outDir = path.join(buildDir, "test");
const timeoutMs = Number(process.env.XDL_TEST_TIMEOUT_MS ?? 60000);

const require = createRequire(path.join(buildDir, "..", "node", "package.json"));
const { chromium } = require("playwright-core");

function findChromium() {
    if (process.env.XDL_CHROMIUM) {
        return process.env.XDL_CHROMIUM;
    }
    const cache = path.join(os.homedir(), ".cache", "ms-playwright");
    if (existsSync(cache)) {
        const dirs = readdirSync(cache).filter(d => d.startsWith("chromium-")).sort();
        for (const dir of dirs.reverse()) {
            const candidate = path.join(cache, dir, "chrome-linux64", "chrome");
            if (existsSync(candidate)) {
                return candidate;
            }
        }
    }
    return undefined;
}

const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm" };
const server = createServer(async (req, res) => {
    if (req.url === "/favicon.ico") {
        res.writeHead(204);
        res.end();
        return;
    }
    const file = path.join(outDir, path.normalize(req.url === "/" ? "/test.html" : req.url));
    try {
        const body = await readFile(file);
        res.writeHead(200, { "Content-Type": types[path.extname(file)] ?? "application/octet-stream" });
        res.end(body);
    } catch {
        res.writeHead(404);
        res.end();
    }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}/test.html`;

const browser = await chromium.launch({
    headless: true,
    executablePath: findChromium(),
    args: ["--no-sandbox", "--enable-unsafe-webgpu", "--enable-features=Vulkan", "--use-angle=vulkan", "--ignore-gpu-blocklist"],
});
const page = await browser.newPage({ viewport: { width: 256, height: 256 } });

let verdict = null;
const done = new Promise(resolve => {
    page.on("console", message => {
        const text = message.text();
        console.log(text);
        if (/^XDL_wgpu test (PASSED|FAILED)$/.test(text)) {
            verdict = text.endsWith("PASSED");
            resolve();
        }
    });
    page.on("pageerror", error => {
        console.log(`pageerror: ${error.message}`);
    });
});

await page.goto(url);
const timer = setTimeout(() => {
    console.log(`run-test: no verdict after ${timeoutMs} ms`);
    verdict = false;
}, timeoutMs);
await Promise.race([done, new Promise(resolve => setTimeout(resolve, timeoutMs))]);
clearTimeout(timer);

await page.screenshot({ path: path.join(outDir, "screenshot.png") });
await browser.close();
server.close();
process.exit(verdict ? 0 : 1);
