<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<!--
  这一份 .ts 是第 10 讲「附一 Linguist 工作流」的**活样本**，不是随手写的 XML。

  它是怎么来的（全部在本机 Qt 6.12.0 上真实跑过）：
      1) lupdate 用来抽取的真实源码在 10_internationalization.cpp 里，
         上下文名 = 类名这条规矩由 lupdate 保证。本机最新一轮（加进 GaugePanel 之后重跑）：
             lupdate -no-obsolete 10_internationalization.cpp -ts out.ts
             → "Found 16 source text(s) (16 new and 0 already existing)"
           分布是 7 个上下文：AlphaProbe 5 / MainWindow 4 / GaugePanel 2 /
           BetaProbe 2 / QDialogButtonBox 1 / OtherPanel 1 / FileMenu 1。
           （OtherPanel 那条是 647 行 QCoreApplication::translate("OtherPanel", ...)
           被 lupdate 当真收进来了——它不要求真有一个叫 OtherPanel 的类，
           上下文就是个字符串。）
      2) 本文件是**手填译文**的那一步（平时这一步叫 Qt Linguist，或者直接改 XML）。
      3) lrelease gauge_zh_CN.ts -qm gauge_zh_CN.qm
         → "Generated 3 translation(s) (3 finished and 0 unfinished)"（本机实测）
      4) 示例第 ⑧ 步用 QTranslator::load() 把它读进来，再打印 translate() 的前后对比。

  关于下面三个 <location>：它们都是**真行号**，由 lupdate 在这一版本源码上跑出来后核对过
  （214 行 Pressure gauge / 215 行 %n valve(s) open / 229 行带消歧注释的 Open）。
  第三条是 GaugePanel 里唯一的一条 "Open"，所以它不跟同上下文的同名源文撞车 ——
  这一点很重要：同一个 context 下源文也一样、而且**两条都填了译文**时，lrelease 会打
      Warning: dropping duplicate messages in 'xxx.qm':
      * Context: Panel   * Source: Open   * Line in .ts file: 6   * Duplicate at line: 10
      Generated 1 translation(s) (1 finished and 0 unfinished)
  并只留第一条（本机用最小样本量到的原文）。靠 <extracomment>/comment 区分两条不同
  译文是走不通的。AlphaProbe 那边正好留了这个坑的现场：128 行 tr("Open") 和
  134 行 tr("Open", 注释) 同属 AlphaProbe、源文都是 "Open"，一旦两条都填上译文，
  第二条就进不了 .qm。（只有空译文时不报，因为压根没东西可去重。）

  为什么要把 .qm 也提交进仓库，而不是让 CMake 现场生成：
  ctest 要在**没装 LinguistTools 的机器**上也判绿。lrelease 属于可选工具包
  （Qt 精简安装可以没有它），把它的产物当数据提交，运行时就再没有第二条环境分支了。
  .qm 只有 213 字节，且完全可由本文件 + 上面第 3 步那条命令复现。
  （213 这个数字在本文件改注释前后都一样：XML 注释和 <location> 都不进 .qm。）

  ⚠ 你正在读的这段注释本身是**易碎**的：lrelease 吃它，但 lupdate 重写 .ts 时会把
  顶层 XML 注释整条删掉（本机在 build_deploy/qt_cmake_05_translations 上对过前后两份
  文件）。这份 gauge_zh_CN.ts 之所以还在，是因为没有任何构建步骤会对它跑 lupdate。
  真要重跑抽取，就把这段先抄走。

  <translation> **有没有填**决定这条会不会进 .qm：空译文被 lrelease 踢出去并打印
  "Ignored N untranslated source text(s)"，运行时查不中、退回源文。而 type="unfinished"
  本身**不影响运行期**——填了译文的 unfinished 条目照样能查到（本机用最小样本量过，
  过程记在 10_internationalization.cpp 附一第 3 条）。别把「去掉 unfinished」当成
  「让翻译生效」的动作；那个动作是**把译文写上**。
  还有一条更阴的：0 条译文的 .qm 也照样 load() 成功。所以判断翻译生效要看返回的
  字符串变没变，不能看 load() 的布尔值。
-->
<TS version="2.1" language="zh_CN">
<context>
    <name>GaugePanel</name>
    <message>
        <location filename="../10_internationalization.cpp" line="214"/>
        <source>Pressure gauge</source>
        <translation>压力表</translation>
    </message>
    <message numerus="yes">
        <location filename="../10_internationalization.cpp" line="215"/>
        <source>%n valve(s) open</source>
        <translation>
            <numerusform>%n 个阀门开着</numerusform>
        </translation>
    </message>
    <message>
        <location filename="../10_internationalization.cpp" line="229"/>
        <source>Open</source>
        <extracomment>给译者的注释，来自 tr() 的第二个参数；它不进 .qm 的查表键，只在 .ts 里给人看</extracomment>
        <translation>打开</translation>
    </message>
</context>
</TS>
