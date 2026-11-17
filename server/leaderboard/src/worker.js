// The Worker's entry: only the handler is exported here, because the runtime takes every named export of the main
// module as an entry point, and src/index.js exports helpers for src/admin.js.
export { default } from "./index.js";
