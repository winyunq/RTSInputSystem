# 本任务源码备份清单

保存现有国家坦克外观选择入口的国家映射扩展。

| 仓库相对文件路径 | 用途 | 必须备份的原因 |
|---|---|---|
| `.gitignore` | 默认忽略新增文件 | 防止结果、缓存和外来资源自动入库 |
| `GIT_BACKUP.md` | 本任务备份清单 | 明确源码恢复范围 |
| `Source/RTSInputSystem/Private/RTSUnitTypeProtocol.cpp` | 通用坦克按国家选择外观 | 地图开局、玩家生产和 AI 生产复用同一个已有入口 |
| `Source/RTSInputSystem/Public/RTSInputPanelSettings.h` | 既有国家外观入口的声明 | 其他模块可编译并调用同一个公共入口；28,743 字节 |

不新增跟踪 Content 美术、日志、报告、Binaries、Intermediate、Saved 和第三方依赖。单位及生产协议依赖主项目的 `Config/DefaultRTSInputSystem.ini`，美术依赖主项目 `Content/Unit/Actor/Army/Tank` 和共享 MassBattleFrame 资源。本地提交不等于独立磁盘备份。
