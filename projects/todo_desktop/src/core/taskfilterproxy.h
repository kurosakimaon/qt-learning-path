// ============================================================================
//  todo_desktop · 过滤/排序代理（第陆阶段第 5 讲的落地点）
// ----------------------------------------------------------------------------
//  为什么要代理而不是在模型里过滤？
//    模型负责「全部数据是什么」，代理负责「当前视图愿意看哪一部分」。
//    搜索框每敲一个字、切换「只看未完成」，都只动代理，源模型一行不动。
//
//  这里走的是「重写 filterAcceptsRow」这条路，而不是 setFilterFixedString：
//    前者能同时看标题与备注两个字段，后者只能盯一列。
//  外部判据变了必须 beginFilterChange() / endFilterChange() 包住，
//  否则视图拿到的行号映射会和真实结果错位（Qt 6.10 起 invalidateFilter 已标废弃）。
// ============================================================================
#pragma once

#include <QSortFilterProxyModel>
#include <QString>

class TaskFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TaskFilterProxy(QObject* parent = nullptr);

    void setOnlyOpen(bool onlyOpen);
    bool onlyOpen() const { return m_onlyOpen; }

    void setQueryText(const QString& text);
    QString queryText() const { return m_query; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    bool m_onlyOpen = false;
    QString m_query;
};
