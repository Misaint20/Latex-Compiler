#!/usr/bin/env python3
"""Patch the fetched webview library to expose a configuration hook.

The WKURLSchemeHandler for app:// must be registered on the
WKWebViewConfiguration before the WKWebView is created, and the library
creates both internally. The patch adds a single optional global hook that
set_up_web_view() invokes between those two moments, without changing any
existing behavior. Idempotent: re-running on a patched tree is a no-op.
"""
import sys
from pathlib import Path

MARKER = "WEBVIEW_CONFIG_HOOK_PATCH"

HOOK_DECL = """// WEBVIEW_CONFIG_HOOK_PATCH: optional caller hook invoked with the bare
// WKWebViewConfiguration pointer right after creation, before the WKWebView
// exists. Set by the embedding application to register custom URL schemes.
using config_hook_t = void (*)(void *);
inline config_hook_t config_hook = nullptr;
"""

HOOK_CALL = """    // WEBVIEW_CONFIG_HOOK_PATCH: let the embedder register scheme handlers.
    if (config_hook) {
      config_hook(config);
    }
"""


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_webview_hook.py <webview.h>", file=sys.stderr)
        return 2
    header = Path(sys.argv[1])
    text = header.read_text(encoding="utf-8")
    if MARKER in text:
        print("webview hook patch: already applied")
        return 0

    anchor = "using config_hook_t = void (*)(void *);"
    if anchor not in text:
        # Insert the hook declaration at the start of the cocoa engine's file,
        # right before the engine class definition.
        engine_anchor = "class cocoa_wkwebview_engine : public engine_base {"
        if engine_anchor not in text:
            print("webview hook patch: engine anchor not found", file=sys.stderr)
            return 1
        text = text.replace(
            engine_anchor,
            HOOK_DECL + "\n" + engine_anchor,
            1,
        )
    else:
        print("webview hook patch: hook declaration already present")

    setup_anchor = """    m_manager = objc::msg_send<id>(config, "userContentController"_sel);
    m_webview = objc::msg_send<id>("WKWebView"_cls, "alloc"_sel);"""
    if setup_anchor not in text:
        print("webview hook patch: set_up_web_view anchor not found", file=sys.stderr)
        return 1
    text = text.replace(
        setup_anchor,
        HOOK_CALL + "\n" + setup_anchor,
        1,
    )

    header.write_text(text, encoding="utf-8")
    print("webview hook patch: applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
