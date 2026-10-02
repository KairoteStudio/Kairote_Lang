# Kairote HTTP 服务器

Linux x86-64 原生 TCP 示例。服务器绑定 `127.0.0.1:0`，由内核分配端口，并打印 `PORT=<端口>`。`GET /` 返回问候，其他路径返回 404；每个响应包含 Content-Length，发送后关闭连接。

```sh
build/selfhost/stage2/program examples/http-server/Server.krt \
  examples/http-server/Main.krt --linker "$PWD/build/ArkLink/ArkLink" \
  -O2 -o build/http-server
build/http-server
```

`Main.krt` 持续监听，按 Ctrl+C 退出。`HttpServer.Run(port, connection_limit)` 也允许指定端口及接受连接的数量，数量为零时持续监听；客户端未完成的请求和发送失败不会终止整个服务器。

实现使用 `System.Sys` 直接进行 Linux 网络系统调用。SockAddrIn 使用 16 字节的真实 struct 布局，包含八字节内联数组；启动时检查 sizeof、字段地址及原始字节。接收缓冲区为 4096 字节，累计请求到完整的 CRLFCRLF；收发超时为五秒。此示例顺序处理连接，只实现 GET 和单次请求，不提供 TLS、keep-alive、请求体处理或完整 HTTP 协议栈。

```sh
SELFHOST_COMPILER="$PWD/build/selfhost/stage2/program" \
ARKLINK="$PWD/build/ArkLink/ArkLink" \
python -m unittest Test.SelfHost.test_http_server -v
```

测试在空 PATH 下用 native CLI 编译 O0/O2 原生 ELF，在实际 loopback TCP 上检查 GET、404、请求分段、提前断连后继续服务、响应长度、连接 EOF 和服务退出。Python 只实现客户端和测试进程管理，服务器与系统调用均由 Kairote 实现。
