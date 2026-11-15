"""Мок API бота krendeltrader для проверки монитора без сервера (только стандартная библиотека).

    python tools/mock_api.py            # слушает 127.0.0.1:8900, токен любой

Отдаёт /api/state, plans, events, trades, logs (с курсором after), candles, ticker,
symbols, advice (GET/PUT, 409 при чужой правке) и WebSocket /api/stream (hello, tick, state, plan, notify).
"""
from __future__ import annotations

import base64
import hashlib
import json
import math
import random
import socket
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, unquote, urlparse

PORT = 8900
BASE = {"BTCUSDT": 64300.0, "ETHUSDT": 3410.0, "1000PEPEUSDT": 0.0105, "SOLUSDT": 152.0, "XRPUSDT": 0.52,
        "DOGEUSDT": 0.16, "BNBUSDT": 590.0, "1000BONKUSDT": 0.024, "SUIUSDT": 1.9, "LINKUSDT": 14.2}
RANGES = {"15m": (60, 15), "30m": (60, 30), "1h": (60, 60), "4h": (300, 48), "12h": (300, 144), "24h": (900, 96),
          "7d": (3600, 168)}

lock = threading.Lock()
advice = {"01-general.md": ("# Общие\nТоргуй аккуратно.\n", time.time() - 86400),
          "02-hunt.md": ("# Охота\nИщи прострелы >2% за 15 минут.\n", time.time() - 3600)}
log_lines: list[str] = []
plans: list[dict] = []
events: list[dict] = []
clients: list[socket.socket] = []
subs: dict[socket.socket, set] = {}
state = {"mode": "auto", "symbol": "BTCUSDT", "price": BASE["BTCUSDT"], "api_ok": True, "planning": False,
         "bot_account": "main", "operator_account": "manual", "bot_error": None,
         "position": {"side": "long", "size": 0.01, "entry_price": 64200.0, "leverage": 40, "liquidation_price": 62750.0,
                      "stop_loss": 63950.0, "take_profit": 64700.0, "unrealized_pnl": 1.0, "owner": "bot"},
         "plan": None, "budget": {"budget_usd": 50, "realized_pnl": -3.2, "unrealized_pnl": 1.0, "equity": 47.8},
         "trades_today": 2}


def price_at(symbol: str, ts: float) -> float:
    b = BASE.get(symbol, 10.0)
    seed = sum(map(ord, symbol))
    return b * (1 + 0.004 * math.sin(ts / 900 + seed) + 0.002 * math.sin(ts / 97 + seed * 2) + 0.0007 * math.sin(ts / 13))


def candles(symbol: str, rng: str) -> dict:
    step, n = RANGES.get(rng, RANGES["1h"])
    now = time.time()
    start = (now // step) * step - (n - 1) * step
    out = []
    for i in range(n):
        t0 = start + i * step
        pts = [price_at(symbol, t0 + k * step / 6) for k in range(7) if t0 + k * step / 6 <= now]
        out.append({"ts": t0, "open": pts[0], "high": max(pts), "low": min(pts), "close": pts[-1], "volume": 10.0})
    return {"symbol": symbol, "range": rng, "interval": {60: "1m", 300: "5m", 900: "15m", 3600: "1h"}[step], "source": "mock",
            "candles": out}


def add_log(msg: str, level: str = "INFO") -> None:
    with lock:
        log_lines.append(time.strftime("%Y-%m-%d %H:%M:%S") + f",000 {level} bot.engine: {msg}")


def new_plan(reason: str, comment: str | None) -> dict:
    p = {"id": len(plans) + 1, "ts": time.time(), "reason": reason, "operator_comment": comment,
         "model": "claude-opus-5-5", "usage": {"input_tokens": 4100, "output_tokens": 900},
         "reasoning": "Цена держится над 64к, объёмы падают. Лонг по откату допустим, шорт — нет.",
         "plan": {"regime": random.choice(["range", "high_volatility", "trend_up"]), "trading_enabled": True,
                  "allow_long": True, "allow_short": False, "spike_window_minutes": 60, "spike_threshold_pct": 2.0,
                  "leverage": 20, "position_usd": 650, "stop_loss_pct": 1.0, "take_profit_pct": 0.9,
                  "max_hold_minutes": 45, "valid_for_minutes": 60,
                  "summary": "Рынок во флэте у 64.3к. Беру лонг только по откату, стоп 1%."}}
    plans.append(p)
    state["plan"] = {"id": p["id"], "ts": p["ts"], "age_min": 0, "regime": p["plan"]["regime"], "summary": p["plan"]["summary"]}
    return p


# ----------------------------------------------------------------- websocket
def ws_send(sock: socket.socket, obj: dict) -> bool:
    data = json.dumps(obj, ensure_ascii=False).encode()
    head = bytes([0x81])
    n = len(data)
    head += bytes([n]) if n < 126 else (bytes([126]) + struct.pack(">H", n) if n < 65536 else bytes([127]) + struct.pack(">Q", n))
    try:
        sock.sendall(head + data)
        return True
    except OSError:
        return False


def ws_recv(sock: socket.socket) -> str | None:
    def exact(n):
        b = b""
        while len(b) < n:
            chunk = sock.recv(n - len(b))
            if not chunk:
                raise OSError("closed")
            b += chunk
        return b
    try:
        h = exact(2)
        op, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126: n = struct.unpack(">H", exact(2))[0]
        elif n == 127: n = struct.unpack(">Q", exact(8))[0]
        mask = exact(4) if h[1] & 0x80 else bytes(4)
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(exact(n)))
    except OSError:
        return None
    if op == 8:
        return None
    return data.decode() if op == 1 else ws_recv(sock)


def broadcast(kind: str, data: dict) -> None:
    msg = {"type": kind, "ts": time.time(), "data": data}
    for c in list(clients):
        if not ws_send(c, msg):
            clients.remove(c)


def ticker_loop() -> None:
    while True:
        time.sleep(2)
        p = price_at("BTCUSDT", time.time())
        state["price"] = p
        state["position"]["unrealized_pnl"] = round((p - 64200) * 0.01, 2)
        broadcast("tick", {"symbol": "BTCUSDT", "price": p, "bid": p - 0.1, "ask": p})
        for c, ss in list(subs.items()):
            for s in ss:
                q = price_at(s, time.time())
                ws_send(c, {"type": "tick", "ts": time.time(), "data": {"symbol": s, "price": q, "bid": q, "ask": q}})
        if random.random() < 0.3:
            add_log(f"tick BTCUSDT {p:.1f}")
        if random.random() < 0.05:
            add_log("risk: сигнал отклонён — плечо выше лимита", "WARNING")


# ----------------------------------------------------------------- http
class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):  # тихо
        pass

    def reply(self, data, status=200, ok=True):
        body = json.dumps({"ok": ok, "data": data} if ok else {"ok": False, "error": data}, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def body(self) -> dict:
        n = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(n) or b"{}") if n else {}

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        p = u.path
        if p == "/api/stream" and self.headers.get("Upgrade", "").lower() == "websocket":
            key = self.headers["Sec-WebSocket-Key"]
            acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
            self.wfile.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                              f"Sec-WebSocket-Accept: {acc}\r\n\r\n").encode())
            self.wfile.flush()
            ws_send(self.connection, {"type": "hello", "ts": time.time(), "data": state})
            clients.append(self.connection)
            subs[self.connection] = set()
            try:
                while True:
                    msg = ws_recv(self.connection)
                    if msg is None:
                        break
                    m = json.loads(msg)
                    for key, fn in (("subscribe", set.add), ("unsubscribe", set.discard)):
                        v = m.get(key)
                        for sym in ([v] if isinstance(v, str) else (v or [])):
                            fn(subs[self.connection], sym.upper())
                    ws_send(self.connection, {"type": "subscribed", "ts": time.time(),
                                              "data": {"symbols": sorted(subs[self.connection]), "rejected": [], "max": 10}})
            except (OSError, ValueError):
                pass
            subs.pop(self.connection, None)
            self.close_connection = True
            return
        if p == "/api/state":
            return self.reply(state)
        if p == "/api/plans":
            return self.reply(list(reversed(plans))[: int(q.get("limit", 50))])
        if p == "/api/events":
            return self.reply(list(reversed(events))[: int(q.get("limit", 50))])
        if p == "/api/trades":
            now = time.time()
            return self.reply([{"id": 1, "owner": "bot", "side": "short", "size": 0.009, "leverage": 45,
                                "ts_open": now - 2400, "entry_price": price_at("BTCUSDT", now - 2400),
                                "ts_close": now - 1500, "exit_price": price_at("BTCUSDT", now - 1500),
                                "realized_pnl": -1.2, "close_reason": "стоп-лосс"}])
        if p == "/api/logs":
            with lock:
                if "after" in q:
                    a = int(q["after"])
                    lines = log_lines[a:]
                else:
                    lines = log_lines[-int(q.get("lines", 200)):]
                g = q.get("grep")
                if g:
                    lines = [x for x in lines if g in x]
                return self.reply({"lines": lines, "offset": len(log_lines)})
        if p == "/api/candles":
            return self.reply(candles(q.get("symbol", "BTCUSDT").upper(), q.get("range", "1h")))
        if p.startswith("/api/ticker/"):
            s = unquote(p.rsplit("/", 1)[1]).upper()
            if s.startswith("COIN"):   # пример отказа торгового API с кодом
                body = json.dumps({"ok": False, "error": "символ не разрешён аккаунту", "code": "risk",
                                   "ret_code": None}, ensure_ascii=False).encode()
                self.send_response(400); self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
                return
            pr = price_at(s, time.time())
            return self.reply({"symbol": s, "price": pr, "bid": pr, "ask": pr})
        if p == "/api/symbols":
            lst = [{"symbol": s, "base": s.replace("USDT", "").replace("1000", ""), "turnover_24h": BASE[s] * 1e6}
                   for s in BASE] + [{"symbol": f"COIN{i}USDT", "base": f"COIN{i}", "turnover_24h": 0} for i in range(30)]
            return self.reply({"symbols": lst, "ready": True, "error": None})
        if p == "/api/advice":
            return self.reply([{"name": n, "size": len(t.encode()), "mtime": m} for n, (t, m) in sorted(advice.items())])
        if p.startswith("/api/advice/"):
            n = unquote(p.split("/api/advice/", 1)[1])
            if n not in advice:
                return self.reply("нет такого файла", 404, ok=False)
            t, m = advice[n]
            return self.reply({"name": n, "text": t, "mtime": m})
        return self.reply("нет такого метода", 404, ok=False)

    def do_POST(self):
        p = urlparse(self.path).path
        b = self.body()
        if p == "/api/replan":
            if state["planning"]:
                return self.reply({"accepted": False, "reason": "уже планирует"})
            state["planning"] = True
            comment = b.get("comment")
            broadcast("planning", {"active": True, "kind": "plan", "reason": "оператор", "comment": comment})

            def finish():
                time.sleep(4)
                plan = new_plan("оператор", comment)
                state["planning"] = False
                broadcast("planning", {"active": False, "kind": "plan", "ok": True, "plan_id": plan["id"]})
                broadcast("plan", plan)
                broadcast("state", state)
                broadcast("notify", {"text": f"Новый план #{plan['id']}: {plan['plan']['regime']}"})
            threading.Thread(target=finish, daemon=True).start()
            ev = {"id": len(events) + 1, "ts": time.time(), "kind": "operator_action", "message": "replan", "data": b}
            events.append(ev)
            broadcast("event", ev)
            add_log(f"replan по запросу оператора: {comment}")
            return self.reply({"accepted": True})
        if p == "/api/advice":
            n = b.get("name", "")
            if n in advice:
                return self.reply("файл уже есть", 409, ok=False)
            advice[n] = (b.get("text", ""), time.time())
            return self.reply({"name": n, "size": len(advice[n][0].encode()), "mtime": advice[n][1]})
        return self.reply("нет такого метода", 404, ok=False)

    def do_PUT(self):
        p = urlparse(self.path).path
        if p.startswith("/api/advice/"):
            n = unquote(p.split("/api/advice/", 1)[1])
            b = self.body()
            if n not in advice:
                return self.reply("нет такого файла", 404, ok=False)
            if not b.get("mtime"):
                return self.reply("нужен mtime", 400, ok=False)
            if abs(advice[n][1] - b["mtime"]) > 1e-3:
                return self.reply("файл изменился на сервере", 409, ok=False)
            advice[n] = (b.get("text", ""), time.time())
            return self.reply({"name": n, "size": len(advice[n][0].encode()), "mtime": advice[n][1]})
        return self.reply("нет такого метода", 404, ok=False)


if __name__ == "__main__":
    for i in range(40):
        add_log(f"старт, строка {i}")
    new_plan("старт", None)
    events.append({"id": 1, "ts": time.time() - 600, "kind": "risk_reject", "message": "сигнал short отклонён",
                   "data": {"reasons": ["шорт запрещён планом"]}})
    threading.Thread(target=ticker_loop, daemon=True).start()
    print(f"mock API на http://127.0.0.1:{PORT}")
    ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
