// ============================================================================
//  todo_desktop · JSON 导入导出（第捌阶段第 3 讲的落地点）
// ----------------------------------------------------------------------------
//  导出用 QSaveFile：先写临时文件，commit() 才原子替换目标文件。
//  所以「写到一半断电」不会留下半截 JSON —— 旧文件还完整地在那儿。
//
//  导入故意写得宽容一些：不认识的对象、缺字段的任务都跳过或补默认值，
//  只在「整个文件不是合法 JSON / 没有 tasks 数组」时才判失败。
//  对外交换格式的第一原则：能读进来的尽量读进来，读不进来的说清哪一条。
// ============================================================================
#pragma once

#include "core/task.h"

#include <QByteArray>
#include <QString>
#include <QVector>

namespace JsonCodec {

// 序列化到内存。导出、单元测试、剪贴板都用得着。
QByteArray toJson(const QVector<Task>& tasks);

bool fromJson(const QByteArray& json, QVector<Task>* tasks, QString* error = nullptr);

// 落盘 / 读盘。save() 内部用 QSaveFile 做原子替换。
bool save(const QString& filePath, const QVector<Task>& tasks, QString* error = nullptr);
bool load(const QString& filePath, QVector<Task>* tasks, QString* error = nullptr);

} // namespace JsonCodec
