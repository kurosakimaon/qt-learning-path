#!/usr/bin/perl
# ============================================================================
# _tools/audit_census.pl —— 「第 N 行写着 X」这类清单行的**辅助**核对
#
# 它和 audit_citations.pl 的分工：
#   · audit_citations.pl 是**门**（在 check_all.sh 里，红 = 失败）。它只认
#     「文件名 + 行号」成对出现的引用，比如 `qthread.h 第 145 行`。
#   · 讲义里还有另一大类写法：一段清单先立一个表头 `// QtTest/qtestkeyboard.h`，
#     下面每行只写裸数字 —— `//   第 274/280 行 keyClick(...) / keyClick(...)`。
#     这类引用**没有文件名**，正式门看不见它。本文件专门补这块盲区。
#
# 它**不是门**，别加进 check_all.sh。理由写在下面「已知假阳」里：它是启发式的，
# 会红，而且红了通常不是讲义错。它的正确用法是**人工队列生成器**：
# 一轮勘误之后跑一次，把 MISS 逐条读一遍，真错改掉，假阳留着当噪声基线。
#
# 用法：
#   perl _tools/audit_census.pl                 # 扫全仓库（同正式门的目录规则）
#   perl _tools/audit_census.pl quality/*.cpp   # 只扫指定文件
#   QT_SRC='/d/Qt/6.12.0/Src/qtbase/src;/d/Qt/6.12.0/Src/qtsvg/src' perl _tools/audit_census.pl
#
# 退出码：0 = 跑完了（哪怕有 MISS）；非 0 = 一个文件都没读到 / 找不到 Qt 源码树。
#         它**故意**不因 MISS 而非 0 —— 那样它就成了一个必须被「调绿」的门，
#         而调绿的往往是要留的事实。
#
# 本机 6.12.0 的基线（实测，138 个文件 + 双源码树）：
#   片段 1429｜MISS 44｜OOB 7｜无锚点 230｜无法解析 0
#   → 真正被核对的是 1429 − 230 = 1199 条，其中 44+7 条报红，命中率约 95.7%。
#   这 51 条全部逐条读过，真错都落成了各文件里的「★ 勘误」条目（全仓现在 37 条，
#   其中一批就是这把门顶出来的），剩下的属于下面四类假阳。
#   片段数会随讲义增删漂（本仓从 1424 漂到 1429：本轮补写的「实测口径」声明里
#   带了 36/36、86/86 这类裸数字，被当清单行收了进来——MISS 仍是 44、OOB 仍是 7，一条没多）。
#   **漂了不算红**，要看的是 MISS/OOB 有没有多出没见过的那几条。
#   「无锚点 230」= 注释里出现的普通数字（「12 个任务」「30 秒」这类），
#   它们不是「第 N 行写着 X」的断言，所以不参与命中率分子。
#
# 已知假阳（四类，别指望它们归零；要归零就得改讲义写法，不值得）：
#   a) 散文引用抢走块锚点。例：network/03 的表头是 `qabstractsocket.h`，
#      中途夹了一句「socket 与 …… 见 qabstractsocketengine_p.h」，于是后面
#      `第 234~237 行` 被记到 engine_p.h 名下 → 报 OOB（该文件只有 225 行）。
#      实际 qabstractsocket.h:234~237 = 两行 QT_DECL_METATYPE_EXTERN_TAGGED，是对的。
#      同类：threads/05（qthreadpool_p.h 抢 qthreadpool.cpp:238~241）、
#      widgets/10（qtranslator.h 抢 qtranslator.cpp:749）、
#      storage/04（qresource.h 抢 CMakeLists.txt 的行号）、
#      quality/02（qtestassert.h 抢 qtestkeyboard.h:284）。
#   b) 关键词被当成被断言的标识符：enum / explicit / Q_SIGNALS / ctor / TIMEOUT /
#      KiB / version / cpp / txt / passed —— 它们不是行号旁边那个符号的真名。
#   c) index() 大小写敏感：文中写「started」，源码里是 `reportStarted` → 记 MISS。
#   d) 「★ 勘误」条目自己引用了**旧错号**（为了说明改了什么），旧号当然找不到 → MISS。
#      这类是刻意的，改了反而丢掉证据。
#
# 反面教训（写在这个文件里，免得下次又踩）：
#   · 不带参数直接跑曾经只输出「census 片段 0」——零文件被读，看起来却像全绿。
#     现在没参数就按正式门同一套规则全仓扫，一个文件都没读到就 die。
#   · 曾经有 52 条 qmetaobject.h 的 MISS 全假：配对正则里的 `\s*` 把尾随空格带进了
#     被查找串，index() 永远失配。**先证明号是对的，再动工具**——工具错会让 52 处
#     好文本被「修正」成真错。
# ============================================================================
use strict;
use warnings;
use utf8;
use File::Find;
binmode(STDOUT, ':encoding(UTF-8)');
binmode(STDERR, ':encoding(UTF-8)');

# 源码树可以指多个（用 `;` 分隔；Windows/Git Bash 里路径自带冒号，所以不用 `:`）。
# 默认带 qtsvg：讲义里 QtSvg/qsvgrenderer.h 那一组引用不在 qtbase 里，只搜 qtbase
# 时它们会被归到「文件名无法解析」——核对不到 ≠ 通过。
my @SRC = split /;/, ($ENV{QT_SRC} || 'D:/Qt/6.12.0/Src/qtbase/src;D:/Qt/6.12.0/Src/qtsvg/src');
my @missing = grep { !-d $_ } @SRC;
die "找不到 Qt 源码树：@missing\n  换机器时这样指：QT_SRC=/d/Qt/6.12.0/Src/qtbase/src;/d/Qt/6.12.0/Src/qtsvg/src perl \$0\n" if @missing;

my @files = @ARGV;
if (!@files) {
    find(sub {
        my $base = $_;
        return unless -f $base;
        return if "$File::Find::dir/" =~ m{(^|/)(build[^/]*/|\.git/|runlogs/)};
        return unless $base =~ /\.(cpp|h|md|html)$/;
        push @files, "$File::Find::dir/$base";
    }, '.');
}
die "一个文件都没读到（参数写错了？路径不存在？），本次没有核对任何清单行\n" unless @files;

my %LINES;
sub src { my ($b)=@_; return $LINES{$b} if exists $LINES{$b};
          my @f;
          for my $root (@SRC) {
              my @hit = `find "$root" -name '$b' 2>/dev/null`; chomp @hit;
              push @f, @hit;
          }
          my $g=''; for my $p (@f){ open my $h,'<',$p or next; local $/; $g=<$h>; close $h; $g=~s/\r\n/\n/g; last if length $g; }
          return $LINES{$b}=[split /\n/,$g,-1]; }

# 这些词出现在行号旁边时不可能是「该行的内容」，直接排除（假阳类 b）
my %STOP = map { $_=>1 } qw(class constexpr inline static virtual void const return public protected private
  override template struct nodiscard signal slot signals slots bool int uint Q_SIGNAL Q_SLOTS Q_INVOKABLE
  Qt QString QList QVector QVariant QObject QWidget QGuiApplication QApplication nullptr true false);

my ($tot,$miss,$oob)=(0,0,0);
my ($noanchor,$unresolved)=(0,0);
my $read = 0;
for my $path (@files) {
  open my $fh,'<:encoding(UTF-8)',$path or do {
      # 参数里点了这个文件却打不开 = 路径写错，**必须响**，不能像以前那样
      # `or next` 一句带过、最后输出「片段 0」假装全绿。
      warn "打不开：$path\n"; next;
  };
  $read++;
  my @rl=<$fh>; close $fh; s/\r?\n$// for @rl;
  my $cur=0;
  for my $i (0..$#rl) {
    my $body;
    if ($rl[$i] =~ m{^\s*(?://|\*|/\*|<!--)\s*(.*)$}) { $body = $1 } else { $cur = 0; next }
    $body =~ s/\*\*//g;
    # 严格块锚点：① 文件名在正文开头；② 文件名紧跟 ：（ —— 「第 N 行起」；
    # ③ 句中引用「（qfoo.cpp 2984~3048 行，…）」只有在其后 3 行内确实出现「裸数字开头」
    #    的清单行时才当表头 —— 否则它是散文引用（假阳类 a），
    #    抢了锚点会把后面整段清单指到错文件上。
    if ($body =~ m{^(?:[A-Za-z_0-9]+/)?(q[a-z_0-9]+\.(?:h|cpp))\b}
        or $body =~ m{((?:[A-Za-z_0-9]+/)?q[a-z_0-9]+\.(?:h|cpp))\s*(?:[：:(]|——|,?\s*第?\s*\d+\s*行\s*起)}i) {
      my $f = $1; $f =~ s{^.+/}{}; $cur = $f;
    }
    elsif ($body =~ m{((?:[A-Za-z_0-9]+/)?q[a-z_0-9]+\.(?:h|cpp))\D{0,14}?\d{1,5}\s*[~、\-]\s*\d{1,5}\s*(?:行|lines?)}i
           or $body =~ m{((?:[A-Za-z_0-9]+/)?q[a-z_0-9]+\.(?:h|cpp))\D{0,14}?\d{1,5}\s*(?:行|lines?)[^：:]*[：:]\s*$}i) {
      my $f = $1; $f =~ s{^.+/}{};
      my $hdr = 0;
      for my $j ($i+1 .. $i+3) {
        next if $j > $#rl;
        if ($rl[$j] =~ m{^\s*(?://|\*)\s*\d{2,5}(?:\s*[~、\-]\s*\d{2,5})?\s*\S}) { $hdr = 1; last; }
      }
      # 「（qdrag.cpp 35 行文档原话）」这种单行散文引用不是表头：既没范围也没以冒号收尾
      $cur = $f if $hdr;
    }
    # 行内若有「文件名 + 数字」的直接引用，这些数字归它自己（正式门已查），从本行剔掉
    my $b2 = $body;
    $b2 =~ s{(?:[A-Za-z_0-9]+/)?q[a-z_0-9]+\.(?:h|cpp)\s*[:：]?\s*第?\s*\d{1,5}(?:\s*[~、\-]\s*\d{1,5})?\s*(?:行|lines?)?}{}g;
    # 「第 51/52 行 A() / B()」这种并列写法：数字与标识符的配对不确定，整行跳过
    # （正式门只查「文件名+行号」那种一一对应的引用，不碰它）。
    next if $b2 =~ m{\d\s*/\s*\d};
    # 先把本行的行号位点全找出来，再让「每个号管到下一个号之前」那段文字做它的断言域，
    # 这样 `728 position() 729 buttons()` 一行里多条声明都能各查各的。
    my @nums;
    # 号后面紧跟「. / _ / 数字」的不是行号：13.1.0（MinGW 版本）、6.12、06_qrunnable_threadpool（文件名）
    while ($b2 =~ /(?<![0-9A-Za-z_.~、\-\/])(\d{2,5})(?:\s*[~、\-]\s*(\d{2,5}))?(?![0-9A-Za-z_.])/g) {
      push @nums, [ $-[0], $+[0], $1, $2 ];
    }
    for my $n0 (@nums) {
      my ($p0,$p1,$a,$bb) = @$n0;
      # 两种写法都要认：「754 class QFoo」号在前 → 取号后第一个标识符；
      # 「methodSignature 26 / name 27」号在后 → 取号前紧挨着的那个标识符。
      # 上一版只认第一种，把 metaobject/08 整段 15 条全配错（配错≠讲义错）。
      my $before = substr($b2, 0, $p0);
      my $tail   = substr($b2, $p1);
      $tail =~ s/^\s*行\s+/ /;
      my $claim;
      # 优先「号在前」（第 87 exists / 728 position()）：要求号与标识符之间只有空白。
      # 不满足才退回「号在后」（methodSignature 26）。对齐表格里 `→ QPointF   729 buttons()`
      # 的上一列名字会被误当所有者，所以这个先后不能反。
      if ($tail =~ /^\s+((?:[A-Za-z_]|~[A-Za-z])[A-Za-z_0-9]{2,})/) {
        my $id = $1;
        $claim = $id unless $STOP{$id} || $STOP{lc $id};
      }
      if (!defined $claim) {
        # 允许标识符后面跟 `()`：`loopLevel() 78 行` 这种写法里 78 归 loopLevel，不归下一个名字
        my $idre = qr/((?:[A-Za-z_]|~[A-Za-z])[A-Za-z_0-9]{2,}\s*(?:\(\))?)\s*(?:[~、\-]\s*\d{2,5}\s*)?$/;
        if    ($before =~ $idre) { $claim = $1 }
        elsif ($before =~ /((?:[A-Za-z_]|~[A-Za-z])[A-Za-z_0-9]{2,}\s*(?:\(\))?)\s+$/) { $claim = $1 }
        $claim =~ s/\s*\(\)$// if defined $claim;
      }
      if (!defined $claim) {
        # 兜底扫描：号与标识符之间只允许空白和「行」字。一旦出现别的字符（中文、单位 px/ms、
        # 半个句子），这个号就不是「此行写着 X」的断言。
        my $t = $tail;
        while ($t =~ /((?:[A-Za-z_]|~[A-Za-z])[A-Za-z_0-9]{2,})/g) {
          my $id  = $1;
          my $pre = substr($t, 0, $-[0]);
          last unless $pre =~ /^[\s行]*$/;
          next if $STOP{$id} || $STOP{lc $id};
          $claim = $id; last;
        }
      }
      next unless defined $claim;
      $claim =~ s/^\s+|\s+$//g;      # idre 里的 \s* 会把尾随空格带进 claim，index() 于是永远找不到
      next unless length $claim;
      $tot++;
      # 「算了但没验」必须显式计数，不然就是假绿。旧版这里写的是
      # `next if !defined $cur`（而 $cur 初始化成 0，defined！）加上 `next unless $n`，
      # 两条路都把片段静默丢掉：夹具第一次跑报「片段 2｜MISS 0｜OOB 0」，
      # 看着全绿，其实一条都没核对。
      if (!$cur) { $noanchor++; next }
      my $s = src($cur); my $n = scalar @$s;
      unless ($n) {
          $unresolved++;
          printf "UNRESOLVED %s:%d  %s L%s [%s]（Qt 源码树里找不到这个文件名，或读出来是空）\n",
                 $path,$i+1,$cur,$a,$claim;
          next;
      }
      if ($a > $n) { $oob++; printf "OOB  %s:%d  %s L%s (only %d lines)\n",$path,$i+1,$cur,$a,$n; next; }
      my $hi = $bb // $a; next if $hi > $n;
      my $win = join ' ', map { defined $_?$_:'' } @{$s}[ $a-1 .. ($hi>$a?$hi:$a+1)-1 ];
      next if index($win,$claim) >= 0;
      $miss++;
      my $actual = defined $s->[$a-1] ? $s->[$a-1] : '';
      $actual =~ s/^\s+|\s+$//g;
      printf "MISS %s:%d %s L%s [%s]\n     实际 %s:%s = %s\n",
        $path,$i+1,$cur,$a,$claim,$cur,$a,substr($actual,0,110);
    }
  }
}
die "一个文件都没读到，本次没有核对任何清单行（见上面的「打不开」）\n" unless $read;
printf "----\ncensus 扫描 %d 个文件｜片段 %d｜MISS %d｜OOB %d｜无锚点 %d｜文件名无法解析 %d\n",
       $read, $tot, $miss, $oob, $noanchor, $unresolved;
print "（本工具是辅助队列，不是门：MISS/OOB 需逐条人工判定，四类假阳见文件头。\n",
      "  「无锚点 / 无法解析」是**核对不到**的片段，不是通过——别把它当绿色。）\n";
