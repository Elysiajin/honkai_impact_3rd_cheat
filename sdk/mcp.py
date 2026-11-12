# -*- coding: utf-8 -*-
"""bh3_overlay MCP client (Streamable HTTP)."""
import json
import urllib.request

URL = "http://127.0.0.1:18517/mcp"
VER = "2024-11-05"


class Mcp:
    def __init__(self, url=URL):
        self.url = url
        self.sid = None
        self.id = 0
        self._post({
            "jsonrpc": "2.0", "id": 1, "method": "initialize",
            "params": {"protocolVersion": VER, "capabilities": {},
                       "clientInfo": {"name": "py", "version": "1.0"}},
        }, init=True)
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"})
        self.id = 10

    def _post(self, body, init=False):
        data = json.dumps(body).encode()
        req = urllib.request.Request(self.url, data=data, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("Accept", "application/json, text/event-stream")
        if self.sid:
            req.add_header("Mcp-Session-Id", self.sid)
            req.add_header("MCP-Protocol-Version", VER)
        with urllib.request.urlopen(req, timeout=180) as r:
            if init:
                self.sid = r.headers.get("Mcp-Session-Id")
            raw = r.read()
        if not raw:
            return None
        txt = raw.decode("utf-8", "replace")
        if txt.lstrip().startswith("event:") or "\ndata: " in txt:
            for line in txt.splitlines():
                if line.startswith("data: "):
                    return json.loads(line[6:])
            return None
        return json.loads(txt)

    def call(self, name, **args):
        self.id += 1
        r = self._post({"jsonrpc": "2.0", "id": self.id, "method": "tools/call",
                        "params": {"name": name, "arguments": args}})
        if r is None:
            return None
        if "error" in r:
            raise RuntimeError(r["error"])
        res = r.get("result") or {}
        sc = res.get("structuredContent")
        if sc is not None:
            return sc
        content = res.get("content") or []
        for c in content:
            if c.get("type") == "text":
                try:
                    return json.loads(c["text"])
                except Exception:
                    return c["text"]
        return res

    def tools(self):
        self.id += 1
        r = self._post({"jsonrpc": "2.0", "id": self.id, "method": "tools/list",
                        "params": {}})
        return [t["name"] for t in (r.get("result") or {}).get("tools", [])]

    # ---- 便捷封装 ----
    def status(self):
        return self.call("get_status")

    def read(self, addr, size, fmt="hex", mode="auto"):
        if isinstance(addr, str):
            a = addr
        else:
            a = "0x%X" % addr
        return self.call("read_memory", address=a, size=int(size), format=fmt, mode=mode)

    def read_bytes(self, addr, size):
        r = self.read(addr, size, "hex", "inline")
        if not r or not r.get("ok"):
            return b""
        d = r.get("data") or ""
        d = d.replace("\n", " ").strip()
        if not d:
            return b""
        return bytes(int(x, 16) for x in d.split() if x)

    def q(self, addr):
        b = self.read_bytes(addr, 8)
        return int.from_bytes(b, "little") if len(b) == 8 else 0

    def u32(self, addr):
        b = self.read_bytes(addr, 4)
        return int.from_bytes(b, "little") if len(b) == 4 else 0

    def qs(self, addr, n):
        b = self.read_bytes(addr, 8 * n)
        return [int.from_bytes(b[i * 8:i * 8 + 8], "little") for i in range(len(b) // 8)]

    def cstr(self, addr, cap=64):
        b = self.read_bytes(addr, cap)
        z = b.find(b"\x00")
        if z >= 0:
            b = b[:z]
        return b.decode("ascii", "replace")

    def disasm(self, addr, count=40):
        a = addr if isinstance(addr, str) else "0x%X" % addr
        return self.call("disassemble", address=a, count=int(count), mode="inline")

    def call_fn(self, addr, gpr=None, fpr=None, timeout_ms=3000):
        a = addr if isinstance(addr, str) else "0x%X" % addr
        g = []
        for v in (gpr or []):
            g.append(v if isinstance(v, str) else "0x%X" % v)
        return self.call("call_function", address=a, gpr=g, fpr=fpr or [],
                         timeout_ms=timeout_ms)

    def call_method(self, addr, this=None, method_info=None, int_args=None,
                    float_args=None, timeout_ms=3000):
        a = addr if isinstance(addr, str) else "0x%X" % addr
        mi = method_info if isinstance(method_info, str) else "0x%X" % method_info
        args = {"address": a, "method_info": mi,
                "int_args": list(int_args or []), "float_args": list(float_args or []),
                "timeout_ms": timeout_ms}
        if this:
            args["this"] = this if isinstance(this, str) else "0x%X" % this
        return self.call("call_method", **args)


if __name__ == "__main__":
    m = Mcp()
    print("tools:", m.tools())
    s = m.status()
    print("ua:", s.get("ua"), "ready:", s.get("data_ready"))
    print("avatars:", s.get("avatars"), "monsters:", s.get("monsters"))
    print("msg:", s.get("msg"))
