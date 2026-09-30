// Entry of dist/core.js, loaded by nebula into a QJSEngine (global `NebulaCore`) and by the tests.
import './polyfills.js';
// Values cross the C++ boundary as JSON strings, so no engine-specific object conversion is involved.
import { check, describeComponent, listComponents } from './check.js';

export { check, describeComponent, listComponents };

// host: { readText(path) -> JSON {ok, text|error}, stat(path) -> JSON {ok, size|error}, readBase64(path) -> JSON {ok, base64|error} }
export function checkJson(source, host) {
  const h = { readText: (p) => JSON.parse(host.readText(p)), stat: (p) => JSON.parse(host.stat(p)), readBase64: (p) => JSON.parse(host.readBase64(p)) };
  return JSON.stringify(check(source, h));
}
export const componentsJson = () => JSON.stringify(listComponents());
export const describeJson = (name) => JSON.stringify(describeComponent(name) ?? null);
