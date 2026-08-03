# 工作区共享库目录

跨项目共享的 Arduino 库放这里（`<lib名>/library.properties + src/`）。

编译时引用（可与项目内 lib 叠加，多次指定）：

```bash
arduino-cli compile --libraries ./lib --libraries ./<project>/lib --fqbn <FQBN> <sketch>
```

项目级库（只服务单个项目）放 `<project>/lib/`，不放这里。
