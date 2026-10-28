// ============================================================================
// 第 4 讲 · 便利类列表/树/表（QListWidget / QTreeWidget / QTableWidget）
// ----------------------------------------------------------------------------
// 本讲解决什么问题：
//   Qt 的 item view 有两套 API：「model/view（模型/视图）」和「item-based
//   convenience classes（基于项的便利类）」。本讲只讲便利类，但**必须把它的底裤
//   扒给你看**——它内部就是 QAbstractItemModel + QListView/QTreeView/QTableView，
//   你往 QListWidget 里 addItem 的每一个 QListWidgetItem，其实都被塞进一个
//   QListModel 的 QModelIndex 里，靠 role 存取。本讲四块硬货：
//     1) 三个便利类的共同套路：addItem / item(row) / item(row,col) /
//        currentRow / itemSelectionChanged，以及**项（item）不是控件（widget）**；
//     2) 用 model() 这个公开 getter **实测**证明「内部就是一张 model」，
//        并读出它的类名（metaObject 那一套在 metaobject 模块讲过）；
//     3) setModel 在便利类里被降级成 **private**：这是「焊死的封装」，
//        也是判断「我该不该转自定义 model」的第一个硬信号；
//     4) setItemWidget / setCellWidget 的代价，以及**什么时候必须弃用便利类**
//        （数据量大 / 数据在库里 / 需要懒加载 / 需要自定义 role）。
//   → 下一模块：../model_view/（自定义 model、role 系统、代理、懒加载全在那里）
//
// 对应构建目标名：widgets_04_convenience_itemviews
//   find_package(Qt6 REQUIRED COMPONENTS Widgets)
//   add_executable(widgets_04_convenience_itemviews 04_convenience_itemviews.cpp)
//   target_link_libraries(widgets_04_convenience_itemviews PRIVATE Qt6::Widgets)
//   set(CMAKE_AUTOMOC ON)   # 本文件里**没有** Q_OBJECT 类，无需 #include "04_....moc"
//
// 运行前须知：
//   · 本机当前无法编译链接 Qt 运行时，本示例**未在本机实测编译**；
//     每个 API 都对着 C:\msys64\ucrt64\include\qt6 下的真实头文件核对（行号见下）。
//   · 无人值守：QTimer::singleShot 驱动 addItem/setCurrentRow/setCurrentCell/setExpanded，
//     关键结论用 qInfo() 打印，最后 QCoreApplication::quit()。**不弹任何模态框、不调 exec()**。
//   · Qt5 → Qt6 差异（本讲涉及）：
//       - Qt6 把一批容器/控件接口的整数类型换成了 **qsizetype**（本机可核到的一个例子：
//         QtWidgets/qkeysequenceedit.h 第 43 行 `void setMaximumSequenceLength(qsizetype count)`）。
//         但 **item view 的行号在 Qt6 仍是 int**：qlistwidget.h 第 183 行
//         `QListWidgetItem *item(int row) const`、qtablewidget.h 第 217 行
//         `QTableWidgetItem *item(int row, int column) const`。
//         所以本文件读 model 的 rowCount()/columnCount() 时套了一层 int()——
//         那是「两种类型都可能」的防御写法，不是我认为它一定是 qsizetype。
//       - QListWidget::setModel 在本机头文件里位于 **private** 段
//         （qlistwidget.h 第 263~264 行），QTreeWidget 同理（qtreewidget.h 的 private 段）。
//         想「给 QListWidget 换 model」在 Qt5/Qt6 都编不过，别再搜偏方。
//       - 表头：QTableView::horizontalHeader() 返回 QHeaderView*（qtableview.h 38 行），
//         设置列宽策略请写 QHeaderView::setSectionResizeMode（qheaderview.h 102/103 行 +
//         第 42~47 行的 ResizeMode 枚举：Interactive / Stretch / ResizeToContents）。
//         本机头文件里我只核到 setSectionResizeMode 这一个名字，**没去核老名字**，
//         所以老代码里的 setResizeMode 一律照上面改（那是同一件事的新入口）。
// ============================================================================

#include <QApplication>
#include <QCoreApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>          // QtWidgets/qlistwidget.h：QListWidgetItem + QListWidget 同住
#include <QTreeWidget>          // QtWidgets/qtreewidget.h：QTreeWidgetItem + QTreeWidget
#include <QTableWidget>         // QtWidgets/qtablewidget.h：QTableWidgetItem + QTableWidget
#include <QAbstractItemView>    // QtWidgets/qabstractitemview.h：model()/setSelectionMode 都在这
#include <QAbstractItemModel>   // QtCore/qabstractitemmodel.h
#include <QItemSelectionModel>  // QtCore/qitemselectionmodel.h
#include <QModelIndex>
#include <QHeaderView>          // QtWidgets/qheaderview.h
#include <QSize>                // QtCore/qsize.h：setSizeHint(QSize)
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QStringList>

// ============================================================================
// 一、核对签名（本讲全部依据）
// ============================================================================
// QtWidgets/qlistwidget.h
//   第 23~31 行 class Q_WIDGETS_EXPORT QListWidgetItem
//                enum ItemType { Type = 0, UserType = 1000 };
//                explicit QListWidgetItem(QListWidget *listview = nullptr, int type = Type);
//                explicit QListWidgetItem(const QString &text, QListWidget *listview = nullptr, int type = Type);
//                explicit QListWidgetItem(const QIcon &icon, const QString &text, ...)
//   第 34 行   virtual QListWidgetItem *clone() const;     ← 自定义 item 必须实现它
//   第 39~40 行 void setSelected(bool); bool isSelected() const;
//   第 45~46 行 Qt::ItemFlags flags() const; void setFlags(Qt::ItemFlags);   ← 编辑/可选中/可拖放全在这
//   第 48~49 行 inline QString text() const / void setText(const QString&)   ← 就是 data(Qt::DisplayRole)
//   第 52~53 行 icon()/setIcon()                                             ← 就是 data(Qt::DecorationRole)
//   第 86~91 行 setTextAlignment（三个重载，参数分别是 int / Qt::AlignmentFlag / Qt::Alignment）
//   第 101 行  void setForeground(const QBrush &);
//   第 104~106 行 Qt::CheckState checkState() const / void setCheckState(Qt::CheckState)
//                ↑ 实现就是 data/setData(Qt::CheckStateRole)（对比 qtreewidget.h 124~127 行）
//   第 111 行  void setSizeHint(const QSize &);
//   第 164 行  class Q_WIDGETS_EXPORT QListWidget : public QListView
//   第 168 行  Q_PROPERTY(int currentRow READ currentRow WRITE setCurrentRow NOTIFY currentRowChanged)
//   第 170 行  Q_PROPERTY(bool sortingEnabled READ isSortingEnabled WRITE setSortingEnabled)
//   第 183~184 行 QListWidgetItem *item(int row) const; int row(const QListWidgetItem *item) const;
//   第 185~190 行 insertItem(int row, QListWidgetItem*) / insertItem(int row, const QString&)
//                insertItems(int row, const QStringList&) / addItem(const QString&){insertItem(count(),label)}
//                addItem(QListWidgetItem*) / addItems(const QStringList&)
//   第 191~192 行 QListWidgetItem *takeItem(int row);  ← **交出所有权，调用方负责 delete**
//   第 194~200 行 currentItem()/setCurrentItem(...) / currentRow()/setCurrentRow(...[,flags])
//   第 206 行  void sortItems(Qt::SortOrder order = Qt::AscendingOrder);
//   第 210~214 行 editItem / openPersistentEditor / closePersistentEditor / isPersistentEditorOpen
//   第 216~218 行 QWidget *itemWidget(QListWidgetItem*) const; void setItemWidget(item, QWidget*);
//                removeItemWidget(item)（inline，第 272 行展开成 setItemWidget(aItem, nullptr)）
//   第 225~226 行 QModelIndex indexFromItem(const QListWidgetItem*) const;
//                QListWidgetItem *itemFromIndex(const QModelIndex&) const;   ← **两套 API 的转换阀**
//   第 237 行  void scrollToItem(const QListWidgetItem*, QAbstractItemView::ScrollHint = EnsureVisible);
//   第 238 行  void clear();
//   第 240~252 行 信号 itemPressed/itemClicked/itemDoubleClicked/itemActivated/itemEntered/
//                itemChanged/currentItemChanged/currentRowChanged/itemSelectionChanged
//   第 263~264 行 **private: void setModel(QAbstractItemModel *model) override;**   ← 本讲最重要的一行
// QtWidgets/qtreewidget.h
//   第 32~39 行 class QTreeWidgetItem 的 8 个 ctor，含 explicit QTreeWidgetItem(const QStringList&, int type)
//                与 QTreeWidgetItem(QTreeWidget *treeview, const QStringList &strings, int type)
//   第 53~54 行 void setExpanded(bool); bool isExpanded() const;   （item 自己带展开状态）
//   第 67 行   void setFlags(Qt::ItemFlags);        第 69/71 行 text(int column)/setText(int column, ...)
//   第 84/95 行 setToolTip(int,QString) / setFont(int,const QFont&)
//   第 116/121 行 setBackground(int,const QBrush&) / setForeground(int,const QBrush&)
//   第 124~127 行 checkState(int)/setCheckState(int,Qt::CheckState)  ← 直接读写 Qt::CheckStateRole
//   第 134/135 行 virtual QVariant data(int column, int role) const; virtual void setData(int,int,const QVariant&);
//   第 145~159 行 parent()/child(int)/columnCount()/indexOfChild()/addChild()/insertChild()/
//                removeChild()/takeChild()
//   第 226 行  class Q_WIDGETS_EXPORT QTreeWidget : public QTreeView
//   第 241/242 行 columnCount()/setColumnCount(int)
//   第 244~250 行 invisibleRootItem()/topLevelItem(int)/topLevelItemCount()/insertTopLevelItem(int,item)/
//                addTopLevelItem(item)/takeTopLevelItem(int)/indexOfTopLevelItem(item)
//   第 255~257 行 QTreeWidgetItem *headerItem() const; setHeaderItem(item); setHeaderLabels(const QStringList&);
//   第 260~264 行 currentItem()/setCurrentItem(item[,column[,flags]])
//   第 273~279 行 editItem(item,column=0) / openPersistentEditor / itemWidget(item,column)
//   第 305 行  void clear();
//   Q_SIGNALS（sed 出来的完整段）：itemPressed/itemClicked/itemDoubleClicked/itemActivated/
//                itemEntered/itemChanged(**都带 column 参数**)/itemExpanded/itemCollapsed/
//                currentItemChanged/itemSelectionChanged
//   末尾 private: void setModel(QAbstractItemModel*) override;     ← 同 QListWidget
// QtWidgets/qtablewidget.h
//   第 55~59 行 class Q_WIDGETS_EXPORT QTableWidgetItem
//                enum ItemType{Type=0,UserType=1000}; explicit QTableWidgetItem(int type = Type);
//                explicit QTableWidgetItem(const QString &text, int type = Type);
//                explicit QTableWidgetItem(const QIcon&, const QString&, int type = Type);
//   第 62 行   virtual QTableWidgetItem *clone() const;   第 66/67 行 row()/column()
//   第 193 行  class Q_WIDGETS_EXPORT QTableWidget : public QTableView
//   第 196/197 行 属性 rowCount / columnCount（WRITE setRowCount / setColumnCount）
//   第 208~211 行 setRowCount(int) / setColumnCount(int)
//   第 214/215 行 int row(const QTableWidgetItem*) / int column(const QTableWidgetItem*)
//   第 217/218 行 QTableWidgetItem *item(int row, int column) const; void setItem(int,int,QTableWidgetItem*);
//   第 221/222 行 indexFromItem / itemFromIndex                       ← 又是「转换阀」
//   第 224~232 行 verticalHeaderItem(int)/setVerticalHeaderItem / horizontalHeaderItem(int)/
//                setHorizontalHeaderItem / setHorizontalHeaderLabels(const QStringList&)
//   第 237~240 行 setCurrentItem(item[,flags]) / setCurrentCell(int row,int column[,flags])
//   第 242 行  void sortItems(int column, Qt::SortOrder order = Qt::AscendingOrder);
//   第 252/253 行 QWidget *cellWidget(int,int) const; void setCellWidget(int,int,QWidget*);
//   第 259 行  QList<QTableWidgetItem*> selectedItems() const;
//   第 269/270 行 itemPrototype()/setItemPrototype(const QTableWidgetItem*)   ← 新建 cell 的模板
//   第 283 行  void clear();
//   第 286~303 行 信号 itemPressed/itemClicked/itemDoubleClicked/itemActivated/itemEntered/itemChanged/
//                currentItemChanged/itemSelectionChanged + cellPressed/cellClicked/cellDoubleClicked/
//                cellActivated/cellEntered/**cellChanged(int row,int column)**
// QtWidgets/qabstractitemview.h（三个类的共同基类段）
//   第 55~60 行 enum SelectionMode { NoSelection, SingleSelection, MultiSelection,
//                ExtendedSelection, ContiguousSelection };
//   第 64 行   enum SelectionBehavior { ... }（Rows/Columns/Items）
//   第 101/102 行 virtual void setModel(QAbstractItemModel *model);  QAbstractItemModel *model() const;
//   第 104/105 行 setSelectionModel(QItemSelectionModel*) / QItemSelectionModel *selectionModel() const;
//   第 110/111 行 setSelectionMode / selectionMode
//   第 113 行  void setSelectionBehavior(QAbstractItemView::SelectionBehavior);
//   第 165/168 行 setAlternatingRowColors(bool) / setIconSize(const QSize&)
//   第 218~221 行 void selectAll(); void edit(const QModelIndex&); void clearSelection();
//                void setCurrentIndex(const QModelIndex&);
//   第 244~250 行 信号 clicked(const QModelIndex&) / activated(...) / entered(...)
// QtWidgets/qtableview.h    第 38/39 行 QHeaderView *horizontalHeader() const / *verticalHeader() const
// QtWidgets/qheaderview.h   第 42~47 行 enum ResizeMode { Interactive, Stretch, ..., ResizeToContents }
//                第 102/103 行 setSectionResizeMode(ResizeMode) / (int logicalIndex, ResizeMode)
//                第 121 行 setStretchLastSection(bool)    第 127 行 setDefaultSectionSize(int)
// QtWidgets/qlistview.h     第 38/42/44 行 enum Flow / LayoutMode / ViewMode
//                第 53 行 setFlow(Flow)   第 65 行 setSpacing(int)   第 85 行 setUniformItemSizes(bool)
// QtCore/qabstractitemmodel.h 第 283 行 Q_INVOKABLE virtual QVariant data(const QModelIndex &index,
//                int role = Qt::DisplayRole) const = 0;      ← 所有 item 的文本最终都从这出来
// QtCore/qnamespace.h      第 1511~1514 行 enum CheckState { Unchecked, PartiallyChecked, Checked };

// ============================================================================
// 二、把 model 侧的证据读出来（这一段是本讲的「看穿封装」）
// ============================================================================
static void dumpModelEvidence(QAbstractItemView *view, const char *tag)
{
    // 核对：qabstractitemview.h 第 102 行  QAbstractItemModel *model() const;
    QAbstractItemModel *m = view->model();
    if (!m) {
        qInfo() << "  " << tag << "：model() 返回 nullptr（不应该发生）";
        return;
    }
    // 行数列数一律先转 int 再打印/比较：便利类这一层的 API 都是 int（见文件头差异说明），
    // 套一层 int() 也让「将来换成 qsizetype 的 model」那段代码不至于报 narrowing。
    const int rows = int(m->rowCount(QModelIndex()));
    const int cols = int(m->columnCount(QModelIndex()));
    qInfo() << "   " << tag << " → model 运行时类名 =" << m->metaObject()->className()
            << "；rowCount =" << rows << " columnCount =" << cols
            << "；model 的父对象 =" << m->parent()
            << "（**便利类自己造了这张 model，并且把它 setParent 给自己**）";
    if (rows > 0 && cols > 0) {
        const QModelIndex first = m->index(0, 0);         // QAbstractItemModel::index(row,col,parent)
        qInfo() << "      model->index(0,0) 有效 =" << first.isValid()
                << "，data(DisplayRole) =" << first.data(Qt::DisplayRole).toString()
                << "（第 283 行那个纯虚 data() —— item 的 text() 只是它的包装）";
    }
}

// ============================================================================
// 三、main
// ============================================================================
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QWidget window;
    window.setWindowTitle(QStringLiteral("第 4 讲 · 便利类 item view"));
    window.resize(880, 560);
    auto *root = new QVBoxLayout(&window);
    auto *row = new QHBoxLayout;
    root->addLayout(row);                    // qboxlayout.h 40 行

    // ────────────────────────────────────────────────────────────────
    // 证据 ①  QListWidget：加项、取项、换所有权
    // ────────────────────────────────────────────────────────────────
    auto *list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);   // qabstractitemview.h 110 + 55~60
    list->setAlternatingRowColors(true);                           // 165 行
    list->setUniformItemSizes(true);                               // qlistview.h 85 行：性能开关，见下面注释
    row->addWidget(list);

    list->addItem(QStringLiteral("第 1 项（QString 版 addItem）"));      // qlistwidget.h 188 行
    list->addItems({QStringLiteral("第 2 项"), QStringLiteral("第 3 项")});  // 190 行
    auto *ownItem = new QListWidgetItem(QStringLiteral("第 4 项（我自己 new 的）"));  // 29 行 ctor
    ownItem->setCheckState(Qt::PartiallyChecked);                  // 106 行 + qnamespace.h 1513
    ownItem->setSizeHint(QSize(220, 34));                          // 111 行：单行高度
    list->addItem(ownItem);                                        // 189 行：**所有权交给 list**
    qInfo() << "① addItems/addItem 之后 count =" << list->count()   // 192 行
            << "；item(3) 的文本 =" << list->item(3)->text()          // 183 + 48 行
            << "；checkState 枚举值 =" << int(list->item(3)->checkState())
            << "（1=PartiallyChecked：三态在 list 里也是靠 Qt::CheckStateRole 存的）";
    qInfo() << "① row(item) 反查：" << list->row(ownItem)
            << "；itemFromIndex(indexFromItem(item)) 是否回到同一个对象 ="
            << (list->itemFromIndex(list->indexFromItem(ownItem)) == ownItem);   // 225/226 行

    QObject::connect(list, &QListWidget::currentRowChanged, [](int r) {
        qInfo() << "① currentRowChanged →" << r;
    });
    QObject::connect(list, &QListWidget::itemSelectionChanged, [list] {
        int n = 0;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->isSelected())     // 39~40 行
                ++n;
        qInfo() << "① itemSelectionChanged：当前选中数 =" << n;
    });
    QObject::connect(list, &QListWidget::itemChanged, [](QListWidgetItem *it) {
        qInfo() << "① itemChanged →" << it->text()
                << "（**注意 setText / setCheckState / setFlags 都会发它，程序改也算**）";
    });
    QObject::connect(list, &QListWidget::itemClicked, [](QListWidgetItem *it) {
        qInfo() << "① itemClicked →" << it->text() << "（只有真实鼠标点击才发；代码里请用 setCurrentRow）";
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ②  setItemWidget：往格子里塞一个**真控件**
    // ────────────────────────────────────────────────────────────────
    auto *row2 = new QHBoxLayout;
    auto *inlineBtn = new QPushButton(QStringLiteral("我住在第 2 行里"));
    list->setItemWidget(list->item(2), inlineBtn);                 // qlistwidget.h 217 行
    qInfo() << "② setItemWidget 之后 itemWidget(item(2)) 是同一个对象 ="
            << (list->itemWidget(list->item(2)) == inlineBtn)       // 216 行
            << "；**但 model 的第 2 行 data(DisplayRole) 并不知道有个按钮**："
               "文字与按钮是两套渲染路径（按钮是子控件，不是 role 数据）";
    root->addLayout(row2);

    // ────────────────────────────────────────────────────────────────
    // 证据 ③  QTreeWidget：树 = 递归的 item，列 = 每项的 role 表
    // ────────────────────────────────────────────────────────────────
    auto *tree = new QTreeWidget;
    tree->setColumnCount(3);                                        // qtreewidget.h 242 行
    tree->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("状态"), QStringLiteral("大小")}); // 257
    row->addWidget(tree);

    auto *topA = new QTreeWidgetItem({QStringLiteral("项目 A"), QStringLiteral("就绪"), QStringLiteral("12 KB")});
    auto *childA1 = new QTreeWidgetItem({QStringLiteral("  子文件 a1"), QStringLiteral("锁定"), QStringLiteral("3 KB")});
    auto *childA2 = new QTreeWidgetItem({QStringLiteral("  子文件 a2"), QStringLiteral("就绪"), QStringLiteral("9 KB")});
    topA->addChild(childA1);                                        // 156 行
    topA->addChild(childA2);
    childA2->setCheckState(0, Qt::Checked);                         // 126 行：第 0 列带复选框
    childA2->setToolTip(1, QStringLiteral("被别的进程占了"));         // 84 行
    auto *topB = new QTreeWidgetItem({QStringLiteral("项目 B"), QStringLiteral("缺"), QStringLiteral("0 KB")});
    topB->setFlags(topB->flags() & ~Qt::ItemIsEnabled);             // 67 行：把「不可用」写进 flags
    tree->addTopLevelItem(topA);                                    // 248 行
    tree->addTopLevelItem(topB);
    qInfo() << "③ setHeaderLabels 之后 headerItem()->text(1) =" << tree->headerItem()->text(1)
            << "；topLevelItemCount =" << tree->topLevelItemCount()   // 246 行
            << "；topA 的 childCount 走 child/indexOfChild："
            << topA->indexOfChild(childA2)                          // 154 行
            << "；childA2->parent() 是否等于 topA =" << (childA2->parent() == topA);  // 145 行
    QObject::connect(tree, &QTreeWidget::itemExpanded, [](QTreeWidgetItem *it) {
        qInfo() << "③ itemExpanded →" << it->text(0);
    });
    QObject::connect(tree, &QTreeWidget::itemChanged, [](QTreeWidgetItem *it, int col) {
        qInfo() << "③ itemChanged →" << it->text(col) << "在第" << col << "列";
    });
    QObject::connect(tree, &QTreeWidget::currentItemChanged,
                     [](QTreeWidgetItem *cur, QTreeWidgetItem *prev) {
        qInfo() << "③ currentItemChanged：" << (prev ? prev->text(0) : QStringLiteral("(无)"))
                << " → " << (cur ? cur->text(0) : QStringLiteral("(无)"));
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ④  QTableWidget：稀疏存储 —— 空格子真的没有 item
    // ────────────────────────────────────────────────────────────────
    auto *table = new QTableWidget;
    table->setRowCount(4);                                          // qtablewidget.h 208 行
    table->setColumnCount(3);                                       // 211 行
    // 表头的「尺寸谈判」归 QHeaderView 管：Stretch = 三列平分，拖动把手会关掉该列的 Interactive
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);   // qtableview.h 38 + qheaderview.h 102
    table->horizontalHeader()->setDefaultSectionSize(120);                     // qheaderview.h 127 行
    table->setHorizontalHeaderLabels({QStringLiteral("任务"), QStringLiteral("负责人"), QStringLiteral("天数")}); // 232
    table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("写第 4 讲")));   // 218 + 58 行
    table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("徒弟")));
    table->setItem(0, 2, new QTableWidgetItem(QStringLiteral("3")));
    table->setItem(1, 0, new QTableWidgetItem(QStringLiteral("修布局 bug")));
    table->setItem(1, 1, new QTableWidgetItem(QStringLiteral("老师傅")));
    table->setItem(1, 2, new QTableWidgetItem(QStringLiteral("1")));
    row->addWidget(table);
    // ⚠ 【最容易踩的一条】setRowCount(4) 只开「行槽位」，**不会**创建 QTableWidgetItem。
    //    所以 item(3, 2) 返回 nullptr，直接 ->text() 就是崩。
    qInfo() << "④ item(3,2) 是否为空指针 =" << (table->item(3, 2) == nullptr)
            << "；item(0,0)->text() =" << table->item(0, 0)->text()
            << "；row()/column() 自解释 =" << table->item(0, 0)->row() << table->item(0, 0)->column();  // 66/67
    qInfo() << "④ 表头也是 item：horizontalHeaderItem(1)->text() ="
            << table->horizontalHeaderItem(1)->text();              // 228 行
    QObject::connect(table, &QTableWidget::cellChanged, [](int r, int c) {
        qInfo() << "④ cellChanged(row =" << r << ", col =" << c << ")";   // qtablewidget.h 303 行附近
    });
    QObject::connect(table, &QTableWidget::itemSelectionChanged, [table] {
        qInfo() << "④ itemSelectionChanged：selectedItems 数 =" << table->selectedItems().size();  // 259 行
    });

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑤  看穿封装：三个便利类内部各有一张 model
    // ────────────────────────────────────────────────────────────────
    qInfo() << "\n=== ⑤ 便利类内部到底是什么（用公开 API 读出来的证据）===";
    dumpModelEvidence(list, "QListWidget");
    dumpModelEvidence(tree, "QTreeWidget");
    dumpModelEvidence(table, "QTableWidget");
    // 【本讲最重要的一段】
    //   便利类 = 「内置 model + 内置 view」的成套包装：
    //     QListWidget : QListView  → 内部 QListModel（QAbstractItemModel 派生）
    //     QTreeWidget : QTreeView  → 内部 QTreeModel
    //     QTableWidget: QTableView → 内部 QTableModel
    //   你把 QListWidgetItem 交给它，它就往 model 里放一行，并记下 item ↔ QModelIndex 的映射，
    //   于是 item(2) 本质是「第 2 行第 0 列那个 index 的包装对象」。
    //   **封装被焊死了**：qlistwidget.h 第 263~264 行把 setModel 降成 private，
    //   qtreewidget.h 的 private 段同样。你在 QListWidget 上写 setModel(myModel) 直接编译失败
    //   （'setModel' is private）。这一条比任何文档都直白：便利类不打算让你换 model。
    //   要换 model，请回到 view 本身：QListView *v = new QListView; v->setModel(m);
    //   —— 那就是下一模块 ../model_view/ 的正题。
    qInfo() << "   selectionModel 也在同一层：list->selectionModel() 非空 ="
            << (list->selectionModel() != nullptr)                  // qabstractitemview.h 105 行
            << "（便利类的 setSelected()/currentRow 其实都在操作这个 QItemSelectionModel）";

    // ────────────────────────────────────────────────────────────────
    // 证据 ⑥  排序：便利类的「索引会跳」陷阱
    // ────────────────────────────────────────────────────────────────
    auto *sortList = new QListWidget;
    sortList->addItems({QStringLiteral("banana"), QStringLiteral("apple"), QStringLiteral("cherry")});
    sortList->setSortingEnabled(true);                              // qlistwidget.h 207 行 + 属性 170
    qInfo() << "⑥ setSortingEnabled(true) 之后 item(0) =" << sortList->item(0)->text()
            << "（**立刻重排**，不是「下次添加时才排」）";
    sortList->addItem(QStringLiteral("apricot"));
    qInfo() << "⑥ 再 addItem(\"apricot\") 之后 item(1) =" << sortList->item(1)->text()
            << " count =" << sortList->count()
            << " → 陷阱在这里：开着排序时，**你手里的 item 指针仍然有效，但 row 变了**；"
               "凡是「拿 index 记住一行」的写法（itemClicked 之后按 index 去改）都会错行。"
               "正规做法：全程用 QListWidgetItem* 作为身份，需要 index 时用 list->row(item) 现算。";
    root->addWidget(sortList);

    window.show();

    // ── 自动脚本：把三个类的「程序触发」正道走一遍 ────────────────
    QTimer::singleShot(250, [list, ownItem] {
        list->setCurrentRow(1, QItemSelectionModel::Select);        // qlistwidget.h 200 行：带 SelectionFlags
        qInfo() << "① setCurrentRow(1, Select) 之后 currentRow =" << list->currentRow()  // 198 行
                << "；item(1)->isSelected() =" << list->item(1)->isSelected()
                << "；ownItem->isSelected() =" << ownItem->isSelected();
        list->item(0)->setText(QStringLiteral("第 1 项（程序改过）"));   // 49 行 → 会发 itemChanged
    });

    QTimer::singleShot(500, [list] {
        // takeItem：把所有权收回自己手里，然后**自己 delete**
        QListWidgetItem *stolen = list->takeItem(3);                // qlistwidget.h 191 行
        qInfo() << "① takeItem(3) 之后 count =" << list->count()
                << "；取回来的 item 文本 =" << (stolen ? stolen->text() : QStringLiteral("(空)"))
                << " → 它现在**归你**：忘了 delete 就是泄漏，忘了判断空指针就是崩";
        delete stolen;                                              // 明确回收
        qInfo() << "① delete 之后 count =" << list->count();
    });

    QTimer::singleShot(750, [list, inlineBtn] {
        inlineBtn->click();                                         // 按钮被编程触发：演示「住在格子里的控件」照常收 click
        // qlistwidget.h 218 行（inline 定义在 271~272 行）的签名是
        //   void removeItemWidget(QListWidgetItem *item)   // 展开即 setItemWidget(item, nullptr)
        // **参数是 item，不是 widget**——QListWidget 压根没有「按控件反查所在项」的接口，
        // 所以这里必须写成 list->item(2)（inlineBtn 当初就塞在 item(2) 这一格，见第 265 行）。
        // 它只把控件摘下来，**不 delete**：摘下来的 QWidget 所有权归你。
        list->removeItemWidget(list->item(2));                      // 218 行
        qInfo() << "② removeItemWidget 之后 itemWidget(item(2)) ="
                << list->itemWidget(list->item(2))                  // 216 行（预期 nullptr）
                << "；摘下来的按钮仍然归你管（这里 deleteLater 回收——它是 QPushButton，"
                   "是**真 QObject**，所以能用 deleteLater）";
        inlineBtn->deleteLater();
    });

    QTimer::singleShot(1000, [tree, topA] {
        topA->setExpanded(true);                                    // qtreewidget.h 53 行：item 自己带展开状态
        tree->setCurrentItem(topA->child(1), 2);                    // 263 行 setCurrentItem(item, column) + 146 行 child(int)
        qInfo() << "③ setExpanded(true) 之后 isExpanded =" << topA->isExpanded()   // 54 行
                << "；currentItem()->text(2) =" << tree->currentItem()->text(2)     // 260 + 69 行
                << "；childA2 在第 0 列的 checkState 枚举值 ="
                << int(topA->child(1)->checkState(0));              // 124 行
    });

    QTimer::singleShot(1250, [tree, topB] {
        qInfo() << "③ 摘之前看一眼：topB 还允许被选中吗（Qt::ItemIsEnabled）="
                << topB->flags().testFlag(Qt::ItemIsEnabled)
                << "（上面用 setFlags 把它从 flags 里减掉了，界面上就是灰的、点不动）";
        QTreeWidgetItem *taken = tree->takeTopLevelItem(1);           // qtreewidget.h 249 行
        qInfo() << "③ takeTopLevelItem(1) 之后 topLevelItemCount =" << tree->topLevelItemCount()
                << "；被摘下来的项文本 =" << (taken ? taken->text(0) : QStringLiteral("(空)"))
                << " → **摘下来的 item 也归你 delete**（下面直接 delete 回收）";
        // 【Qt6 生命周期硬知识】qtreewidget.h 第 23~24 行是 `class Q_WIDGETS_EXPORT QTreeWidgetItem\n{`
        // ——**没有基类**（Qt5 时代它继承 QListWidgetItem，这条继承在 Qt6 已经取消）。
        // QListWidgetItem（qlistwidget.h 23~24 行）、QTableWidgetItem（qtablewidget.h 50~51 行）
        // 同样是**无基类的普通 C++ 类**，三者都**不是 QObject**，因此：
        //   · 它们**没有 deleteLater()**，也**没有 parent/child 所有权**，
        //     更**不能给它们挂信号或当信号接收者**（信号都挂在宿主 *Widget 上，
        //     如 QTreeWidget::itemClicked / QListWidget::itemChanged）；
        //   · 回收只有两条路：还挂在视图上就让视图负责，
        //     一旦被 takeItem / takeTopLevelItem 摘下来，所有权转到你手里，**只能 delete**；
        //   · deleteLater 那种「等事件循环空了再删」的安全垫在这里用不了。
        //     所以**别在视图自己的信号槽里直接 delete 正在被引用的 item**：
        //     要么先把摘下来的指针存好、放进 QTimer::singleShot(0, ...) 里再 delete，
        //     要么像这里一样，在独立的定时脚本里摘完立刻删。
        if (taken) delete taken;                                    // 不是 QObject：没有 deleteLater 可用
    });

    QTimer::singleShot(1500, [table] {
        table->setCurrentCell(1, 1);                                // qtablewidget.h 239 行
        QTableWidgetItem *it = table->item(1, 1);                   // 217 行
        qInfo() << "④ setCurrentCell(1,1) 之后 currentCell 的 item 文本 =" << (it ? it->text() : QStringLiteral("(nullptr)"))
                << "；item(2,0) 空不空 =" << (table->item(2, 0) == nullptr)
                << "（**我没 setItem 过 (2,0)，它就必须是 nullptr**）";
        if (!it) {                                                  // 防御式写法：本讲请你照抄
            qInfo() << "   ↑ 这段演示「读 item 前判空」的必要";
            return;
        }
        qInfo() << "④ 现在补一个 (2,0) 的 item，并记住：UserType = 1000 以上留给应用自己用"
                   "（qtablewidget.h 第 55~59 行的 ItemType 与 ctor）";
        table->setItem(2, 0, new QTableWidgetItem(QStringLiteral("新补的任务")));
        qInfo() << "   补完再读 item(2,0) =" << table->item(2, 0)->text();
    });

    QTimer::singleShot(1750, [table] {
        table->sortItems(0, Qt::AscendingOrder);                     // qtablewidget.h 242 行：按第 0 列排**整表**
        QTableWidgetItem *a00 = table->item(0, 0);                   // 217 行
        QTableWidgetItem *a02 = table->item(0, 2);
        qInfo() << "④ sortItems(0) 之后第 0 行 ="
                << (a00 ? a00->text() : QStringLiteral("(nullptr：这行从来没 setItem 过)"))
                << "/" << (a02 ? a02->text() : QStringLiteral("(nullptr)"))
                << " → 行与行**整体换位**：你原先记下的 row 数字全部作废，item 指针仍然有效。"
                   "再加上第 3 行是空行，排序后空行可能跑到最前面——**这就是「读之前必须判空」的第二个理由**。";
    });

    QTimer::singleShot(2000, [list, tree, table] {
        qInfo() << "\n=== ⑤ 第二次读 model 证据（程序改过内容之后）===";
        dumpModelEvidence(list, "QListWidget");
        dumpModelEvidence(tree, "QTreeWidget");
        dumpModelEvidence(table, "QTableWidget");
        qInfo() << "   表头在 view 层的另一套 API：本例已执行"
                   " table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch)"
                << "（qtableview.h 38 行拿 QHeaderView*，qheaderview.h 102 行设模式）；"
                   "顺带提醒：Stretch 模式下 setDefaultSectionSize 是不生效的，"
                   "想按列各自谈判请用 Interactive 或 ResizeToContents（qheaderview.h 42~47 行的枚举）。";
    });

    QTimer::singleShot(2250, [] {
        qInfo() << "\n=== 结论 ===";
        qInfo() << "① 便利类 = 内置 model + 内置 view；item(行,列) 只是 QModelIndex 的包装糖。";
        qInfo() << "② setModel 在便利类里是 **private**（qlistwidget.h 263~264 行）：这不是疏忽，是设计表态。";
        qInfo() << "③ 生命周期三条：addItem 交出去；takeItem/removeTab 式摘取归你；setItemWidget 的控件归你。";
        qInfo() << "④ 排序/换位之后**别拿旧的 row 数字**，请用 item 指针 + 现算 row(item)。";
        qInfo() << "⑤ 什么时候必须弃用便利类：行数 > 几千、数据在数据库/文件里、需要懒加载、";
        qInfo() << "   需要自定义 role、多个视图共享同一份数据 —— 全部转 model_view/ 模块。";
        QCoreApplication::quit();
    });

    return app.exec();
}

// ============================================================================
// 附：什么时候必须离开便利类（这张表请你贴在手边）
// ============================================================================
/*
 *  症状                                  便利类为什么会疼                正解
 *  --------------------------------------------------------------------------------------
 *  10 万行，打开就卡住                    一次性 new 10 万个 item 对象，   自定义 QAbstractTableModel，
 *                                         每个 item 内部还带 QVariant 表   只在 data() 里按需取值
 *  数据本来在 SQL 结果里                  双份内存 + 双份同步代码          model 直接包 SQL 游标 / QSqlQueryModel
 *  「翻页时才拉下一页」                    便利类没有懒加载钩子             自定义 model 里实现 canFetchMore/fetchMore
 *  同一份数据要列表视图 + 表格视图          item 属于某**一个** view         一份 model，两个 view 共享
 *  单元格要显示进度条/富状态                role 系统才放得下               自定义 role + QStyledItemDelegate
 *  需要跨列合并/复杂表头                    setSpan 之类只能在 QTableWidget  自定义 model 的 headerData/列结构
 *                                         上用（本文件没演示，属于「凑合」）  才是长久之计
 *
 * 关于 setItemWidget 的三条代价（很多人用了才知道贵）：
 *  ① 每行一个真控件 = 每行一份 QWidget 对象 + 一次布局 + 一次绘制，
 *     一万行就是灾难；列表滚动会明显掉帧。
 *  ② 控件**不参与** role 体系：model->data(...) 里没有它的信息，
 *     排序、代理（delegate）、导出、无障碍（accessibility）都看不见它。
 *  ③ 摘下来（removeItemWidget / setCellWidget 覆盖）不会自动 delete，
 *     而且如果那个控件的 parent 还是 view，你会在销毁列表时看到「子控件比父控件活得久」的断言。
 *  → 想给一行加个按钮，先问自己：能不能用 QStyledItemDelegate 画一个？
 *     代理是 model/view 的正道，本模块第 6~10 讲不碰，见 model_view/ 的 delegate 讲。
 *
 * ——— 老师傅的话 ———
 *   徒弟，便利类这个名字，Qt 起得很诚实：convenience（方便）。它没说是「正确」。
 *   它的定位是给「几百行、数据就在内存里、就一个视图看它」的场景省掉 model 那 100 行样板。
 *   你用它不丢人——我用了十几年，原型阶段照用不误。
 *   丢人的是「明知道数据在库里，还先 SELECT 出来再一条条 addItem」，
 *   那是把两件事各做两遍：库负责存，model 负责「按需提供」，
 *   你亲手把「按需」变成了「全量」。
 *
 *   本讲我最想让你带走的是第 ⑤ 段那个证据：
 *   model() 的运行时类名不是 QListWidget 的什么内部实现细节，它就是一张
 *   QListModel，你往里面加几个 item，rowCount 就涨几行——
 *   也就是说：**便利类的所有魔法，都在「item ↔ index 的映射表」这一件事上**。
 *   一旦你理解了这件事，model_view/ 模块里那套纯虚函数（data/index/rowCount/flags）
 *   对你就不神秘了：那是把这层映射交给你自己写。
 *
 *   最后再钉一遍：item(i,j) 可能返回 nullptr。这是本讲出现频率最高的崩溃原因，
 *   比什么悬空指针常见得多。写 `table->item(r,c)->text()` 之前，
 *   先问一句「我 setItem 过这个格子吗」。
 *
 * ——— 小练习 ———
 *   1. 把证据② 的 setItemWidget 换成「什么都不做，只给 item 设一个 icon
 *      （setIcon）+ setCheckState」，比较滚动时的手感差异；
 *      再写一个 2000 行的列表，分别用/不用 setUniformItemSizes(true)，
 *      用 QElapsedTimer 各测一次「加满 2000 行」与「滚到底」的耗时，把数字写回注释。
 *   2. 给 QTreeWidget 加一层「选中统计」：连 itemSelectionChanged，
 *      用 selectionModel()->selectedRows()（qabstractitemview.h 105 行拿 selectionModel）
 *      打印被选中的顶层行数；然后思考：为什么用 view 层的 QItemSelectionModel
 *      统计比遍历 item->isSelected() 更贴近 Qt 的本意。
 *   3. 把证据④ 的表格改成「从 QList<结构体> 生成」，并**故意**用便利类做，
 *      然后把它改写成 QTableView + 一个自己写的 QAbstractTableModel 子类
 *      （只需要 rowCount/columnCount/data 三个函数，先别管编辑）。
 *      两边打印同一个 model 类名，写出三条差别。做完这一步你就毕业了 ——
 *      下一模块 model_view/ 会从这里接上。
 */
// ============================================================================
