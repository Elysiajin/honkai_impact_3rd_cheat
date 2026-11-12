# 崩坏3 覆盖层 MCP 使用说明

本文档对应 `overlay/bh3_overlay.dll` 内置的 MCP 服务（v1.0）。

MCP 服务运行在游戏进程内部，外部 AI 客户端通过本机 HTTP 连接后，可以直接：

- 用 `ReadProcessMemory` / `WriteProcessMemory` 读写游戏内存
- 用 Zydis 反汇编目标函数，查看解密后的指令逻辑
- 在游戏主线程调用 IL2CPP 方法或任意 x64 函数
- 通过文件交换大块数据，避免把几十 MB 数据塞进协议消息

---

## 1. 启动与连接

### 1.1 启动条件

- 注入 `bh3_overlay.dll`
- 渲染 hook（DX11 Present）成功后才启动，避免在加载器锁里开线程
- 默认自动启动，监听 `127.0.0.1:18517`

### 1.2 UI 开关

菜单位置：

```
UI界面 → 右侧最下方 → 「MCP 服务」卡片
```

该卡片默认在滚动区之外，需要先把页面滚到底。卡片内容：

- `启用 MCP`：开关
- `监听端口`：默认 `18517`
- `状态`：已停止 / 启动中 / 运行中 / 停止中
- `地址`：运行中时显示 `http://127.0.0.1:<端口>/mcp`
- `工具数`：7
- 启动失败时显示错误原因

修改端口后，服务会在端口数值稳定约 800ms 后自动重启；启动失败会退避 3 秒重试。

### 1.3 客户端连接地址

```
http://127.0.0.1:18517/mcp
```

支持远程 HTTP MCP 的客户端（Cursor、Claude Desktop 等）直接填这个 URL。

只支持 stdio 的客户端，用 `mcp-remote` 桥接：

```powershell
npx mcp-remote http://127.0.0.1:18517/mcp
```

### 1.4 协议要求

服务端实现了 Streamable HTTP MCP，请求要点：

- `Accept: application/json, text/event-stream`
- 初始化后，后续请求带 `Mcp-Session-Id`
- 后续请求必须带 `MCP-Protocol-Version: 2024-11-05`（或其他协商版本）

服务端默认只接受 `localhost / 127.0.0.1 / ::1` 的 Host 头，已防 DNS rebinding。

---

## 2. 工具总览

| 工具 | 用途 | 大块数据 |
|---|---|---|
| `get_status` | 游戏数据状态、UA 基址、metadata 指针、MCP 状态 | 无 |
| `list_symbols` | profile 中已解密符号的 RVA/VA/有效性/前导字节 | 无 |
| `read_memory` | RPM 读取内存 | 超 8KB 自动落盘 |
| `write_memory` | RPM 写入内存，自动改页保护并回读校验 | 可传 `data_file` |
| `disassemble` | Zydis 反汇编 | 超过 96 条自动落盘 |
| `call_function` | 在游戏主线程调用 x64 函数 | 无 |
| `call_method` | 按 IL2CPP 约定调用方法 | 无 |

地址参数统一使用十六进制字符串，例如 `"0x7FF612340000"`。

---

## 3. 工具详解

### 3.1 get_status

无参数。返回示例（字段裁剪）：

```json
{
  "ok": true,
  "ua": "0x7FF6A1B00000",
  "ua_size": 310685696,
  "build_stamp": "0x6AA18D63",
  "data_ready": true,
  "avatars": 1,
  "monsters": 5,
  "cam_ok": true,
  "msg": "等待游戏线程采样",
  "metadata_header": "0x7FF6A3C554B0",
  "metadata_body": "0x1A13D000000",
  "startup_buf": "0x1A13D100000",
  "il2cpp_version": 7,
  "mcp": {
    "state": 2,
    "port": 18517,
    "file_dir": "D:\\My Projects\\Honkai Impact\\mcp_file",
    "tools": 7,
    "error": ""
  }
}
```

说明：

- `ua`：`UserAssembly.dll` 基址
- `metadata_header` / `metadata_body`：解密后的元数据头/主体指针
- `il2cpp_version`：正常为 `7`
- `mcp.state`：`0` 停止、`1` 启动中、`2` 运行中、`3` 停止中

### 3.2 list_symbols

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `filter` | string | 否 | 按符号名子串过滤，例如 `"crit"` |
| `valid_only` | boolean | 否 | 只返回当前构建校验通过的符号 |

返回每个符号：

```json
{
  "name": "die_now",
  "rva": "0x51E5960",
  "va": "0x7FF6A21E5960",
  "source": "static",
  "valid": true,
  "needs_review": false,
  "prologue8": "41565657534881EC"
}
```

`source` 取值：

- `static`：编译期表并通过 PE 指纹/前导校验
- `resolved`：编译期失效后按类名+token 运行期解析
- `missing`：当前构建不可用

### 3.3 read_memory

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `address` | string | 是 | 起始地址，十六进制 |
| `size` | integer | 是 | 字节数，最大 64MB |
| `format` | string | 否 | `hex`（默认）、`u32`、`u64`、`f32`、`utf8` |
| `mode` | string | 否 | `auto`（默认）、`inline`、`file` |

行为：

- 分块 64KB 读取，跨区域自动截断，返回值带 `truncated`
- `auto`：超过 8KB 或数组元素超过 1024 个时落盘
- `inline`：内联返回，上限 64KB
- `file`：强制落盘
- `u64` 数组以十六进制字符串返回，避免 JSON 数字精度丢失
- 读取失败会返回 `ok:false`，并给出 `region` 信息

内联示例：

```json
{
  "ok": true,
  "address": "0x1A13D000000",
  "requested": 64,
  "read": 64,
  "truncated": false,
  "region": {
    "base": "0x1A13D000000",
    "size": 4096,
    "protect": "0x4",
    "state": "0x1000"
  },
  "transfer": "inline",
  "encoding": "hex",
  "data": "41 41 41 ..."
}
```

落盘示例：

```json
{
  "ok": true,
  "transfer": "file",
  "raw_path": "D:\\My Projects\\Honkai Impact\\mcp_file\\read_1A13D000000_0001.bin",
  "text_path": "D:\\My Projects\\Honkai Impact\\mcp_file\\read_1A13D000000_0002.txt",
  "preview": "41 41 41 ..."
}
```

### 3.4 write_memory

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `address` | string | 是 | 起始地址 |
| `data_hex` | string | 二选一 | 十六进制数据，空格/换行分隔 |
| `data_file` | string | 二选一 | `mcp_file` 下的 `.bin` 或纯十六进制 `.txt` |
| `verify` | boolean | 否 | 写后回读校验，默认 `true` |

行为：

- 只接受 `mcp_file` 目录下的文件，路径穿越会被拒绝
- 写入前用 `VirtualQuery` 检查区域，必要时 `VirtualProtect` 临时改为可写（代码页改为可执行读写）
- 写入后恢复原页保护，并默认回读比对
- 单次最大 64MB
- 请求体受 MCP HTTP transport 限制（默认 10MB），大块写入请用 `data_file`

返回示例：

```json
{
  "ok": true,
  "address": "0x1A13D000000",
  "written": 8,
  "requested": 8,
  "verified": true,
  "old_protect": "0x4",
  "protect_restored": true
}
```

### 3.5 disassemble

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `address` | string | 是 | 起始地址 |
| `count` | integer | 否 | 指令条数，默认 40，最大 4096 |
| `mode` | string | 否 | `auto`（默认）、`inline`、`file` |
| `syntax` | string | 否 | `intel`（默认）、`att` |

行为：

- 使用 Zydis 5.0 解码，x64 Long Mode
- 每条指令返回地址、原始字节、指令文本
- 跳转、调用、RIP 相对寻址会额外返回 `target`
- 超过 96 条或文本超过 32KB 时自动写 `.txt`
- 解码失败时按 `.byte 0xXX` 前进 1 字节

内联示例：

```json
{
  "ok": true,
  "address": "0x7FF75CC61450",
  "count": 3,
  "transfer": "inline",
  "instructions": [
    {
      "address": "0x7FF75CC61450",
      "bytes": "48 8D 04 11",
      "text": "lea rax, [rcx+rdx*1]"
    },
    {
      "address": "0x7FF75CC61454",
      "bytes": "C3",
      "text": "ret"
    },
    {
      "address": "0x7FF75CC61455",
      "bytes": "66 66 2E 0F 1F 84 00 00 00 00 00",
      "text": "nop [rax+rax*1], ax"
    }
  ]
}
```

落盘文件示例：

```
# bh3 mcp disassembly
# start 0x7FF75CC61450 count 200
0x7FF75CC61450  48 8D 04 11    lea rax, [rcx+rdx*1]
0x7FF75CC61454  C3             ret
0x7FF75CC61500  E8 AB 12 00 00 call 0x7FF75CC627B0  ; -> 0x7FF75CC627B0
```

### 3.6 call_function

在游戏主线程调用任意 x64 函数。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `address` | string | 是 | 函数地址 |
| `gpr` | array | 否 | 整数/指针参数，最多 8 个 |
| `fpr` | array | 否 | float 参数，最多 4 个 |
| `timeout_ms` | integer | 否 | 等待毫秒数，默认 3000，范围 100–10000 |

寄存器约定（Windows x64）：

```
gpr[0] → RCX
gpr[1] → RDX
gpr[2] → R8
gpr[3] → R9
gpr[4..7] → 栈参数
fpr[0..3] → XMM0..XMM3
```

返回示例：

```json
{
  "ok": true,
  "address": "0x7FF75CC61450",
  "rax": "0x7",
  "xmm0": 0.0,
  "elapsed_ms": 0,
  "thread": "game"
}
```

说明：

- 调用排队到游戏窗口线程，由 `tick_game_thread` 每帧执行，最多每帧 2 个
- 目标地址必须位于可执行内存
- 函数签名错误、调用非法对象或错误 `MethodInfo` 会导致游戏崩溃
- 大结构体返回值（>16B）不支持直接返回；需要时把返回缓冲区指针放在 `gpr[0]`

### 3.7 call_method

按 IL2CPP 约定调用实例/静态方法。

参数：

| 参数 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `address` | string | 是 | 函数地址 |
| `this` | string | 否 | 实例指针；静态方法省略 |
| `method_info` | string | 是 | `MethodInfo*`，放在最后一个整数参数 |
| `int_args` | array | 否 | 整数参数 |
| `float_args` | array | 否 | float 参数 |
| `timeout_ms` | integer | 否 | 等待毫秒数，默认 3000 |

参数拼接规则：

```
实例方法：gpr = [this] + int_args + [method_info]
静态方法：gpr = int_args + [method_info]
```

`int_args` 与 `this` / `method_info` 合计最多 8 项。

示例：

```json
{
  "address": "0x7FF6A1B4A73910",
  "this": "0x1A13D0012340",
  "method_info": "0x7FF6A1B2C0D880",
  "int_args": [1],
  "timeout_ms": 3000
}
```

---

## 4. 大文件传输

目录：

```
D:\My Projects\Honkai Impact\mcp_file
```

规则：

- `read_memory` 落盘生成：
  - `read_<地址>_<序号>.bin`：原始字节
  - `read_<地址>_<序号>.txt`：纯十六进制文本，每行 16 字节，`#` 开头为注释
- `disassemble` 落盘生成：
  - `disasm_<地址>_<序号>.txt`
- `write_memory.data_file` 支持：
  - `.bin`：按二进制整体写入
  - 其他后缀：按纯十六进制文本解析，忽略空白、逗号、分号、`#` 注释行、`0x` 前缀

限制：

- 单次读写最大 64MB
- 原始数据超过 4MB 时只生成 `.bin`，不生成 `.txt`
- `data_hex` 走协议，请求体默认上限 10MB；大块写入必须用 `data_file`

---

## 5. 典型工作流

### 5.1 读取解密后的元数据

1. `get_status` 拿到 `metadata_body` / `metadata_header` / `startup_buf`
2. `read_memory` 读取元数据头（建议 `size` 先小后大，`mode:"auto"`）
3. 需要完整结构时用 `mode:"file"`，读取生成的 `.txt` 或 `.bin`
4. 结合 `disassemble` 查看解析函数

### 5.2 反汇编一个符号

1. `list_symbols`，例如 `filter:"die_now"`
2. 取 `va`
3. `disassemble`，`count` 先 40，按需增加
4. 关注 `target` 字段，继续跟进调用目标

### 5.3 调用 IL2CPP 方法

1. `list_symbols` / `read_memory` 找到函数 VA
2. 找到对象指针与 `MethodInfo*`
3. 用 `disassemble` 确认参数个数和顺序
4. `call_method` 传 `this`、`int_args` / `float_args`、`method_info`
5. 如果超时，确认游戏已进入可交互状态（加载界面可能不跑消息泵）

### 5.4 批量导出内存

```json
{
  "name": "read_memory",
  "arguments": {
    "address": "0x1A13D000000",
    "size": 2097152,
    "mode": "file"
  }
}
```

返回 `raw_path` 和 `text_path`，AI 直接读取 `.txt` 即可。

---

## 6. 错误与排障

| 现象 | 原因 | 处理 |
|---|---|---|
| 看不到 MCP 卡片 | 卡片在「UI界面」页右列最下方 | 进入「UI界面」并向下滚动 |
| 状态「已停止」，错误为端口占用 | 端口被其他程序占用 | 换端口；服务 3 秒后自动重试 |
| `call_*` 超时 | 游戏未跑消息泵（加载中）或函数阻塞 | 等进入战斗/主界面再调用；确认函数不会长时间阻塞 |
| `read_memory` 返回 `ok:false` | 目标区域不可读/未提交 | 看 `region`，先 `VirtualQuery` 信息确认地址 |
| `read_memory` 返回 `truncated:true` | 跨区域或末尾不可读 | 分段读取 |
| `write_memory` 报路径错误 | `data_file` 不在 `mcp_file` 下 | 使用相对文件名或 `mcp_file` 内绝对路径 |
| 大块 `data_hex` 写入失败 | 超过 HTTP 请求体限制 | 改用 `data_file` |
| 编译覆盖 `bh3_overlay.dll` 失败 | 游戏正在运行并加载了 DLL | 先退出游戏再编译 |
| 注入后没有 MCP | 构建时 DLL 被占用，注入的是旧文件 | 看 `overlay/bh3_overlay.dll` 大小/修改时间；重新编译 |

---

## 7. 安全与限制

安全：

- 服务只绑定 `127.0.0.1`，不对局域网开放
- 没有鉴权，本机其他进程也可以连接；多人环境请自行加防火墙/代理限制
- `write_memory` 和 `call_*` 可以直接改游戏状态，签名错误会导致崩溃
- 仅用于离线单机逆向学习；在线游戏使用可能触发反作弊

已知限制：

- 只支持 Streamable HTTP，不提供 stdio / WebSocket
- 整数参数最多 8 个，float 参数最多 4 个
- 只支持 `float`（f32）参数，不支持 `double`
- 不支持直接返回大于 16B 的结构体
- MCP 工具处理是单线程串行；大文件读写会阻塞其他工具调用
- 内存写入不会自动备份原始字节；需要回滚时请先 `read_memory` 保存

---

## 8. 构建与依赖

覆盖层构建：

```powershell
cd overlay
build_overlay.bat
```

依赖位置：

```
..\..\cxxmcp-master           cxxmcp 源码
..\..\cxxmcp-build-mingw      cxxmcp 静态库（MinGW GCC 13.1）
..\..\zydis-master            Zydis 源码
..\..\zydis-build-mingw       Zydis 静态库（Decoder + Formatter）
```

说明：

- cxxmcp 构建需要 `-DCXXMCP_ENABLE_HTTP=ON`，覆盖层编译需要 `-DCXXMCP_ENABLE_HTTP=1`
- Zydis 5.0.0 与本地 Zycore 1.5.1 缺少 `ZYAN_LE16/32/64_TO_NATIVE` 宏；预编译库已处理。若重新构建 Zydis，需要加：
  `-DCMAKE_C_FLAGS="-include D:/zydis-shim/zyan_endian.h"`
- 覆盖层与依赖必须使用同一个 MinGW 工具链，避免 C++ ABI 不一致

---

## 9. 最小客户端示例（PowerShell）

```powershell
$base = 'http://127.0.0.1:18517/mcp'
$init = '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"ps","version":"1.0"}}}'

$r = Invoke-WebRequest -Uri $base -Method Post -ContentType 'application/json' `
  -Headers @{ Accept = 'application/json, text/event-stream' } -Body $init -UseBasicParsing

$sid = $r.Headers['Mcp-Session-Id']
if ($sid -is [array]) { $sid = $sid[0] }

$h = @{
  Accept = 'application/json, text/event-stream'
  'Mcp-Session-Id' = "$sid"
  'MCP-Protocol-Version' = '2024-11-05'
}

Invoke-WebRequest -Uri $base -Method Post -ContentType 'application/json' -Headers $h `
  -Body '{"jsonrpc":"2.0","method":"notifications/initialized"}' -UseBasicParsing | Out-Null

$body = @{
  jsonrpc = '2.0'; id = 2; method = 'tools/call'
  params = @{
    name = 'get_status'
    arguments = @{}
  }
} | ConvertTo-Json -Depth 8 -Compress

(Invoke-RestMethod -Uri $base -Method Post -ContentType 'application/json' -Headers $h -Body $body).result.structuredContent
```

---

## 10. 相关文件

- `overlay/ov_mcp.h`：MCP 对外接口
- `overlay/ov_mcp.cpp`：服务、工具、RPM、文件传输
- `overlay/ov_x64call.S`：x64 通用调用跳板
- `overlay/ui/Pages.cpp`：MCP 设置卡片
- `overlay/ov_gui.cpp`：启动时机、游戏线程 tick、状态上报
- `overlay/build_overlay.bat`：构建脚本
