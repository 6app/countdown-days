// 新建/编辑卡片的对话框
#pragma once

#include "app.h"

// anchor：对话框在该点所在的显示器上居中。点击“确定”返回 true 并写回 data。
bool EditCardDialog(CardData& data, bool isNew, POINT anchor);

// 对话框正打开时返回其窗口句柄，否则 nullptr
HWND EditDialogWindow();
