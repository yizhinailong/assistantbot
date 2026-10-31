#!/usr/bin/env python3
"""Chat with assistantbot in the terminal, standing in for NapCat.

The script plays both of NapCat's roles. It serves the OneBot v11 HTTP API that
the bot sends its replies to (the OneBot plugin's base_url), and it posts every
line you type to the bot's event route as a private message event, signed the
way NapCat signs them. Only the Python standard library is used.

With the repository's config.json: export the same ONEBOT_ACCESS_TOKEN and
ONEBOT_SECRET values for the bot and this script, start the bot with
`just run`, then run `python3 scripts/chat.py` in another terminal. The
messages come from QQ number 10001 unless --user-id says otherwise. End the
chat with Ctrl-D.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import itertools
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PROMPT = "you> "


def parse_args() -> argparse.Namespace:
    """Read the command line."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--event-url", default="http://127.0.0.1:8080/onebot/event",
                        help="bot event route to post messages to (default: %(default)s)")
    parser.add_argument("--api-port", type=int, default=3000,
                        help="port of the fake OneBot HTTP API on 127.0.0.1, the bot's base_url (default: %(default)s)")
    parser.add_argument("--user-id", type=int, default=10001,
                        help="QQ number of the friend that the messages come from (default: %(default)s)")
    parser.add_argument("--self-id", type=int, default=10000,
                        help="QQ number of the bot account (default: %(default)s)")
    parser.add_argument("--token-env", default="ONEBOT_ACCESS_TOKEN",
                        help="variable holding the access token that the bot must send; unset accepts any (default: %(default)s)")
    parser.add_argument("--secret-env", default="ONEBOT_SECRET",
                        help="variable holding the secret that signs events; unset sends them unsigned (default: %(default)s)")
    return parser.parse_args()


def show(text: str) -> None:
    """Print a line from the bot above the input prompt."""
    print(f"\r{text}\n{PROMPT}", end="", flush=True)


def make_api_handler(token: str, self_id: int, user_id: int) -> type[BaseHTTPRequestHandler]:
    """Build the request handler of the fake OneBot HTTP API.

    send_private_msg prints messages; account and status actions identify this
    simulator. Other actions succeed with null data.
    """
    message_ids = itertools.count(1)

    class ApiHandler(BaseHTTPRequestHandler):
        def do_POST(self) -> None:  # noqa: N802 (name required by BaseHTTPRequestHandler)
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length) or b"{}")
            if token and self.headers.get("Authorization") != f"Bearer {token}":
                self.reply(403, {"status": "failed", "retcode": 1403, "message": "token verify failed"})
                return

            action = self.path.strip("/")
            if action == "send_private_msg":
                text = "".join(segment.get("data", {}).get("text", "") for segment in body.get("message", []))
                show(f"bot> {text}")
                self.reply(200, {"status": "ok", "retcode": 0, "data": {"message_id": next(message_ids)}})
            elif action == "get_login_info":
                self.reply(200, {"status": "ok", "retcode": 0, "data": {"user_id": self_id, "nickname": "chat simulator"}})
            elif action == "get_friend_list":
                self.reply(200, {"status": "ok", "retcode": 0, "data": [{"user_id": user_id, "nickname": "terminal", "remark": "chat simulator"}]})
            elif action == "get_status":
                self.reply(200, {"status": "ok", "retcode": 0, "data": {"online": True, "good": True}})
            elif action == "get_version_info":
                self.reply(200, {"status": "ok", "retcode": 0, "data": {"app_name": "assistantbot-chat", "app_version": "1", "protocol_version": "v11"}})
            else:
                show(f"(bot called {action})")
                self.reply(200, {"status": "ok", "retcode": 0, "data": None})

        def reply(self, status: int, envelope: dict) -> None:
            data = json.dumps(envelope).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, format: str, *args: object) -> None:  # noqa: A002 (base class signature)
            pass

    return ApiHandler


def private_message(user_id: int, self_id: int, message_id: int, text: str) -> dict:
    """Build a private text message event as NapCat posts it."""
    return {
        "time": int(time.time()),
        "self_id": self_id,
        "post_type": "message",
        "message_type": "private",
        "sub_type": "friend",
        "message_id": message_id,
        "user_id": user_id,
        "message": [{"type": "text", "data": {"text": text}}],
        "raw_message": text,
        "font": 14,
        "sender": {"user_id": user_id, "nickname": "terminal"},
    }


def post_event(url: str, event: dict, self_id: int, secret: str) -> None:
    """Post an event to the bot, signed with the secret when there is one."""
    body = json.dumps(event, ensure_ascii=False, separators=(",", ":")).encode()
    request = urllib.request.Request(url, data=body, method="POST")
    request.add_header("Content-Type", "application/json")
    request.add_header("X-Self-ID", str(self_id))
    if secret:
        signature = hmac.new(secret.encode(), body, hashlib.sha1).hexdigest()
        request.add_header("X-Signature", f"sha1={signature}")
    with urllib.request.urlopen(request, timeout=5):
        pass


def main() -> int:
    args = parse_args()
    token = os.environ.get(args.token_env, "")
    secret = os.environ.get(args.secret_env, "")

    server = ThreadingHTTPServer(("127.0.0.1", args.api_port), make_api_handler(token, args.self_id, args.user_id))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print(f"Fake OneBot API on http://127.0.0.1:{args.api_port}; posting events to {args.event_url}")
    print(f"Chatting as {args.user_id}. End with Ctrl-D.")

    message_ids = itertools.count(1)
    try:
        while True:
            text = input(PROMPT).strip()
            if not text:
                continue
            try:
                post_event(args.event_url, private_message(args.user_id, args.self_id, next(message_ids), text),
                           args.self_id, secret)
            except (urllib.error.URLError, OSError) as error:
                print(f"Could not post the event: {error}", file=sys.stderr)
    except (EOFError, KeyboardInterrupt):
        print()
    finally:
        server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
