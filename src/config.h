// 配置读写：%APPDATA%\CountDay\config.json
// 如果 exe 同目录下存在 config.json，则使用它（便携模式）。
#pragma once

#include "app.h"

std::wstring ConfigPath();
bool LoadConfig(AppConfig& cfg);        // 文件不存在或损坏时返回 false
bool SaveConfig(const AppConfig& cfg);  // 先写临时文件再替换，避免写一半断电损坏
