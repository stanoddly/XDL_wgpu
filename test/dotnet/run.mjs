// Runs the published link test under Node and exits with its exit code.
// Usage: node test/dotnet/run.mjs <publish wwwroot>
import path from "node:path";
import { pathToFileURL } from "node:url";

const wwwroot = path.resolve(process.argv[2]);
const { dotnet } = await import(pathToFileURL(path.join(wwwroot, "_framework", "dotnet.js")).href);
process.exitCode = await dotnet.runMain();
