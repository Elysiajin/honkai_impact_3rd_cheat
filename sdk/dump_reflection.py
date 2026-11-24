# -*- coding: utf-8 -*-
"""通过游戏内 xLua 反射把全部程序集的公开方法签名写入 mcp_file/reflect.tsv。"""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mcp import Mcp

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "mcp_file", "reflect.tsv")

TPL = """luanet.load_assembly('mscorlib')
local AD=luanet.import_type('System.AppDomain')
local as=AD.CurrentDomain:GetAssemblies()
if __IDX__>=as.Length then return 'badidx' end
local a=as:GetValue(__IDX__)
local f=io.open('__PATH__','a')
local ts=a:GetTypes()
local n=0
for i=0,ts.Length-1 do
 local t=ts:GetValue(i)
 local ok,ms=pcall(function() return t:GetMethods() end)
 if ok and ms then
  for j=0,ms.Length-1 do
   local mi=ms:GetValue(j)
   local tok=0
   pcall(function() tok=mi.MetadataToken end)
   if tok and tok>0 then
    local mn='?'; pcall(function() mn=mi.Name end)
    local dt='?'; pcall(function() dt=mi.DeclaringType.FullName end)
    local rn='?'; pcall(function() rn=mi.ReturnType.FullName end)
    local hv='0'
    pcall(function() hv=tostring(mi.MethodHandle.Value) end)
    local ps={}
    pcall(function()
     local arr=mi:GetParameters()
     for k=0,arr.Length-1 do
      local p=arr:GetValue(k)
      local s='?'
      pcall(function() s=p:ToString() end)
      ps[#ps+1]=s
     end
    end)
    f:write(string.format('%d\\t%s\\t%s\\t%s\\t%s\\t%s\\n',tok,tostring(dt),tostring(mn),tostring(rn),table.concat(ps,';'),hv))
    n=n+1
   end
  end
 end
end
f:close()
return 'methods='..n"""


def main():
    m = Mcp()
    m.call("lua_eval", code="return 'ok'")
    st = m.status()
    ua = int(st["ua"], 16)
    tbl = m.q(ua + 0x25554F0)
    cnt = m.u32(ua + 0x25554E8)
    raw = m.read_bytes(tbl, 0x58 * cnt)
    namekey = 0x172adc487fda4b76
    names = []
    for i in range(cnt):
        v = int.from_bytes(raw[i * 0x58 + 0x40:i * 0x58 + 0x48], "little")
        p = (v - namekey) & 0xFFFFFFFFFFFFFFFF
        names.append(m.cstr(p, 64) if 0x50000000000 <= p < 0x80000000000 else "")

    path = OUT.replace("\\", "/")
    open(OUT, "w", encoding="utf-8").close()
    t0 = time.time()
    total = 0
    for i, name in enumerate(names):
        code = TPL.replace("__IDX__", str(i)).replace("__PATH__", path)
        try:
            r = m.call("lua_eval", code=code, timeout_ms=30000)
            n = 0
            if r and r.get("ok"):
                out = r.get("output") or ""
                if "methods=" in out:
                    n = int(out.split("methods=")[1].strip() or 0)
            total += n
            print("%3d %-56s %6d  %6.1fs" % (i, name[:56], n, time.time() - t0), flush=True)
        except Exception as e:
            print("%3d %-56s FAIL %s" % (i, name[:56], e), flush=True)
    print("total methods", total, "size", os.path.getsize(OUT) if os.path.exists(OUT) else 0)


if __name__ == "__main__":
    main()
