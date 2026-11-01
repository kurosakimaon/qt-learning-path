#!/usr/bin/perl
# ============================================================================
# _tools/audit_api_names.pl —— 讲义里「看起来像 Qt 名字」的标识符到底存不存在
#
# 为什么需要它（本轮实跑捞出来的，不是假想）：
#   引用门（audit_citations.pl）查「行号对不对」，清单门（audit_census.pl）查
#   「这行写着 X 对不对」——两把都只吃「文件名 + 数字」这一类断言。**散文里的
#   API 名字**没人管。本轮把 41 个文档页里的 Q* 名字全数拿去和 Qt 源码树对了一遍，
#   当场抓到 4 处「查无此名」：
#     · metaobject/tutorial.md：`QMETHODCODE` / `QSLOTCODE` / `QSIGNALCODE`
#       —— 真名带下划线：qobjectdefs.h:41~43 是 `QMETHOD_CODE` / `QSLOT_CODE` /
#       `QSIGNAL_CODE`。照旧稿去 grep 一无所获。
#     · threads/index.html：run 的标注写成 `Q_NODISCARD` —— Qt 里没有这个宏，
#       构造函数那个叫 `Q_NODISCARD_CTOR`，run 那组叫 `QTCONCURRENT_RUN_NODISCARD`。
#     · quality/05 + quality/tutorial.md：`QVariant::to<T>()` + `QVariant::InvalidTypeException`
#       + 「QtException 家族」—— 三个都不在 6.12 里（qvariant.h 只有 755 行的
#       `value<T>()`；qtbase 全树 grep InvalidTypeException = 0 处；QtException 只出现在
#       qexceptionhandling.h:13 的 `#pragma qt_class(...)`）。
#     · quality/tutorial.md：`QT_EXCEPTIONS` —— 安装树 + Src 全树 0 处；真开关叫
#       `QT_NO_EXCEPTIONS`（qcompilerdetection.h:1515/1518）。
#   这类错比行号更坏：行号错了读者翻一页能发现，**名字错了他去 grep，grep 不到，
#   只会以为是自己找错了地方**。
#
# 它怎么判「不存在」：
#   1. 从 Qt 源码树抓出全部标识符做成集合（本机 6.12.0 的 qtbase/src + qtsvg/src
#      再加安装树 lib/cmake 那批 CMake 侧名字 = 307845 个，建索引实测 16~17 秒）。
#   2. 从被扫文件里抓 `\bQ[A-Za-z_]\w*` 形态的 token（Qt 味的名字都长这样）。
#   3. 四种情况放过：① 集合里有；② 它是集合里某个名字的**前缀**（讲义爱写
#      「三种渐变 QLinear/QRadial/QConical」这种简写，QTRY 这种宏前缀同理）；
#      ③ 本仓 .cpp/.h 的**代码**里出现过这个名字（剥掉注释再抓——本仓的注释就是
#      讲义，连注释一起吃进来等于臆造名自己给自己作证）；
#      ④ 那一行写了 `name-ok:TOKEN 理由`（和字形门的 `glyph-ok:U+XXXX` 同一套约定），
#      而且必须**点名到具体 token**：光写一个 `name-ok` 不认，会被单独列成
#      「放行未点名」。理由：一行里可能并列两个错名，盲放行等于放过第二个。
#      「★ 勘误」段落里复述旧错名、以及「本机头文件里不存在」那种清单，都要点名。
#   5. 其余一律列出来，带 文件:行号 和该行原文。
#
# 它**不是门**，别加进 check_all.sh：它会误伤自造名字（本仓的 CMake 变量、
# 占位符 QxxxItem、术语 QSS……），那些要靠 `name-ok` 或下面的白名单一条条交代。
# 用法和 audit_census.pl 一样：当**人工队列生成器**，跑一次、逐条读完。
#
# 本机 6.12.0 的基线（实测，138 个文件）：
#   token 30033 次｜命中真名/白名单 29946｜前缀式简写放过 25｜name-ok 点名放行 62｜
#   **查无此名 0 种**
#   从最初的「21 种」清到 0 的路线：① 真名写错的直接改名（上面那 4 起就是当场捞出来的，
#   连同更早几轮共十几处）；② 剩下的逐条读完，全是「有意复述旧错名的勘误段落」
#   「本仓自造的占位名」「CMake / 术语名」三类，一条条补上 `name-ok:TOKEN 理由`——
#   现在全仓 26 行点名标记、放行 62 个 token（`grep -rn 'name-ok:'` 数到的 26 行里有一行
#   是 README 在**讲解这个标记本身**，不是真放行）。
#   所以清到 0 **不代表讲义里没有错名**，只代表「每一条查无此名都被点名交代过一次」。
#   以后新写的段落如果又冒出名字，这里会红给你看——这是它的用处，不是它的判决。
#   `--selftest` 用一份夹具复验判卷逻辑本身（7 条断言，含「同一行两个错名只点名一个
#   时，另一个必须还能报出来」这条反向断言）。
#
# 用法：
#   perl _tools/audit_api_names.pl                  # 扫全仓（同引用门的目录规则）
#   perl _tools/audit_api_names.pl quality metaobject   # 只扫这些路径（目录或文件）
#   QT_SRC='/d/Qt/6.12.0/Src/qtbase/src;/d/Qt/6.12.0/Src/qtsvg/src' perl \$0
#   perl _tools/audit_api_names.pl --quiet          # 只打汇总
#
# 退出码：0 = 跑完（哪怕报了名字）；非 0 = 找不到 Qt 源码树 / 一个文件都没读到。
# ============================================================================
use strict;
use warnings;
use utf8;
use File::Find;
binmode STDOUT, ':encoding(UTF-8)';
binmode STDERR, ':encoding(UTF-8)';

my @SRC = split /;/, ($ENV{QT_SRC} || 'D:/Qt/6.12.0/Src/qtbase/src;D:/Qt/6.12.0/Src/qtsvg/src');
# CMake 那一侧的名字（Qt6Targets / Qt6LinguistToolsMacros / find_package 组件……）不是 C++ 符号，
# 但它们确实「该存在」，所以把安装树里的 cmake 目录也索引进来。
my @CMAKE = split /;/, ($ENV{QT_CMAKE} || 'D:/Qt/6.12.0/mingw_64/lib/cmake');
my @bad = grep { !-d $_ } @SRC;
die "找不到 Qt 源码树：@bad\n  换机器时这样指：QT_SRC=/d/Qt/6.12.0/Src/qtbase/src;/d/Qt/6.12.0/Src/qtsvg/src perl \$0\n" if @bad;

# 白名单：不是 Qt 名字、但确实该出现的。每一条都要写清理由，别默默加。
my %KNOWN = (
    QSS              => 'Qt Style Sheets 的缩写，是个术语不是类名',
    QT_DEMO_LIBS     => '本仓自己的 CMake 变量（CMakeLists 里给每个阶段挑 Qt 库）',
    QT_DEMO_SVG_LIBS => '同上，storage 组带 Qt6::Svg 的那一份',
    QUIET            => 'CMake 的关键字（find_package(... QUIET)），不是 Qt 符号',
    QTEST_FORCE_NON_LATIN1 => '本仓 quality/02 自己读的环境变量，不是 Qt 的',
    QtLearningPath  => '本仓写进 QSettings 的组织名（todo_desktop 实测键名 QtLearningPath/TodoDesktop），是数据不是 Qt 符号',
    QTRY_            => 'QTRY_* 宏族的书写前缀（真名见 qtestcase.h:232/238）',
    QxxxItem         => '讲义里的占位写法，指 QListWidgetItem / QTableWidgetItem / QTreeWidgetItem 那一族，Qt 没有叫这个的类',
);

# ---------- 1. 建 Qt 标识符集合 ----------
my %qtid;
my $t0 = time();
for my $root (@SRC, @CMAKE) {
    next unless -d $root;
    open my $p, '-|', "grep -ohrE '\\b[A-Za-z_][A-Za-z_0-9]*\\b' '$root' 2>/dev/null" or next;
    while (my $w = <$p>) { chomp $w; $qtid{$w} = 1; }
    close $p;
}
die "Qt 源码树里一个标识符都没抓到（$t0 起算），是不是根目录指错了？\n" unless %qtid;
my $ids = scalar keys %qtid;
my $build_sec = time() - $t0;

# 前缀查询用：把所有真名排序，再对候选 token 做「有没有哪个名字以它开头」的范围查。
my @sorted = sort keys %qtid;

sub is_prefix_of_real {
    my ($tok) = @_;
    # 二分找第一个 >= $tok 的位置，只看它和后面几个：字典序里能覆盖 $tok 前缀的名字
    # 必然紧跟在 $tok 之后出现。
    my ($lo, $hi) = (0, $#sorted);
    while ($lo < $hi) { my $m = int(($lo + $hi) / 2); if ($sorted[$m] lt $tok) { $lo = $m + 1 } else { $hi = $m } }
    for my $i ($lo .. ($lo + 1 < $#sorted ? $lo + 1 : $#sorted)) {
        return 1 if index($sorted[$i], $tok) == 0 && $sorted[$i] ne $tok;
    }
    return 0;
}

# ---------- 2. 收集要扫的文件 ----------
my $QUIET = (grep { $_ eq '--quiet' } @ARGV) ? 1 : 0;
my $SELFTEST = (grep { $_ eq '--selftest' } @ARGV) ? 1 : 0;
my @args  = grep { $_ !~ /^--/ } @ARGV;
# --selftest 只要 Qt 标识符集合（建索引在上面已经做完），不需要扫全仓
if ($SELFTEST) {
    require File::Temp;
    exit selftest();
}
my @files;
if (@args) {
    for my $a (@args) {
        if (-d $a) {
            find(sub {
                my $b = $_;
                return unless -f $b;
                return if "$File::Find::dir/" =~ m{(^|/)(build[^/]*/|\.git/|runlogs/)};
                push @files, "$File::Find::dir/$b" if $b =~ /\.(cpp|h|md|html)$/;
            }, $a);
        } elsif (-f $a) { push @files, $a }
        else { warn "跳过（既不是文件也不是目录）：$a\n" }
    }
} else {
    find(sub {
        my $b = $_;
        return unless -f $b;
        return if "$File::Find::dir/" =~ m{(^|/)(build[^/]*/|\.git/|runlogs/)};
        push @files, "$File::Find::dir/$b" if $b =~ /\.(cpp|h|md|html)$/;
    }, '.');
}
die "一个文件都没读到（参数写错了？路径不存在？），本次没有核对任何名字\n" unless @files;

# ---------- 3. 扫描（主流程和 --selftest 共用同一段判定代码，避免「测试的是另一套逻辑」） ----------
sub sweep {
    my ($files_ref) = @_;
    my %r = (checked => 0, known => 0, prefix => 0, flagged => 0, loose => 0);
    my %unk;
    # 先把「本仓自己的**代码**」算进已知集合：能编过去的名字不是笔误
    # （struct QmLedger、#define QTLP_I18N_DIR 这一类本仓真有的东西）。
    # 注释和字符串都先剥掉——本仓 .cpp 的注释就是讲义正文，里面大量复述旧错名，
    # 连注释一起吃进来 = 臆造名自己给自己作证；字符串是数据（组织名、UA、提示语），
    # 也不算断言。要留在已知集合里的字符串名，走下面的 %KNOWN 并写清理由。
    for my $f (@$files_ref) {
        next unless $f =~ /\.(cpp|h)$/;
        open my $h2, '<:encoding(UTF-8)', $f or next;
        local $/;
        my $g = <$h2>;
        close $h2;
        next unless defined $g;
        $g =~ s{/\*.*?\*/}{}gs;
        $g =~ s{//[^\n]*}{}g;
        $g =~ s{"(?:[^"\\\n]|\\.)*"}{}g;
        $g =~ s{'(?:[^'\\\n]|\\.)*'}{}g;
        $qtid{$1} = 1 while $g =~ /\b([A-Za-z_][A-Za-z_0-9]*)\b/g;
    }
    for my $f (@$files_ref) {
        open my $h, '<:encoding(UTF-8)', $f or do { warn "打不开：$f\n"; next };
        my $is_code = ($f =~ /\.(cpp|h)$/) ? 1 : 0;
        my $ln = 0;
        while (my $line = <$h>) {
            $ln++;
            # 代码文件里的字符串字面量是**数据**，不是 API 名：
            # QStringLiteral("QtLearningPath") 这种组织名/UA 字符串别拿去找 Qt 符号。
            my $scan = $line;
            $scan =~ s/"(?:[^"\\]|\\.)*"//g if $is_code;
            my @toks = $scan =~ /\b(Q[A-Za-z_][A-Za-z_0-9]*)\b/g;
            next unless @toks;
            # 标记必须**点名**：`name-ok:QSvgItem 理由`。只写一个光秃秃的 `name-ok`
            # 会把整行的查无此名全放行——一行里两个错名，放行一个就等于漏掉另一个，
            # 所以这种写法单独报出来（不认作放行）。
            my %oktok;
            while ($line =~ /\bname-ok[:：]([A-Za-z_0-9,\s]*)/g) {
                for my $c (split /[,\s]+/, $1) { $oktok{$c} = 1 if $c =~ /^Q[A-Za-z_0-9]*$/ }
            }
            my $bare = ($line =~ /\bname-ok\b(?![:：])/) ? 1 : 0;
            for my $t (@toks) {
                $r{checked}++;
                if ($qtid{$t}) { $r{known}++; next }
                # 白名单支持「前缀式条目」：值里写了「前缀」含义的，用 index 命中
                my $wl = (grep { $_ eq $t || (substr($_, -1) eq '_' && index($t, $_) == 0) } keys %KNOWN)[0];
                if (defined $wl) { $r{known}++; next }
                if (is_prefix_of_real($t)) { $r{prefix}++; next }
                if ($oktok{$t}) { $r{flagged}++; next }
                if ($bare) {
                    $r{loose}++;
                    printf "  放行未点名 %s:%d  %s\n      写了 name-ok 但没写成 name-ok:%s——本工具不认\n", $f, $ln, $t, $t;
                    next;
                }
                my $txt = $line; $txt =~ s/^\s+|\s+$//g; $txt =~ s/\s+/ /g;
                push @{ $unk{$t} }, [ "$f:$ln", substr($txt, 0, 120) ];
            }
        }
        close $h;
    }
    $r{unk} = \%unk;
    return \%r;
}

# ---------- 4. --selftest：拿夹具把四条放行规则各咬一遍 ----------
# 夹具故意「一半对一半错」：同名的两个臆造名只点名一个，另一个必须漏不出来。
sub selftest {
    my ($dir, $fc, $fm);
    $dir = File::Temp::tempdir(CLEANUP => 1);
    $fc = "$dir/fixture.cpp"; $fm = "$dir/fixture.md";
    open my $a, '>:encoding(UTF-8)', $fc or die "写不了夹具：$!\n";
    print {$a} <<"CPP";
// QBogusInCommentOnly 只出现在注释里：注释不能自己给自己作证，必须报「查无此名」。
struct QRepoDefinedInCode { int x; };   // 代码里定义的真名 → 必须放过
static const char *kOrg = "QBogusInStringOnly";   // 字符串是数据 → 不抓
CPP
    close $a;
    open my $b, '>:encoding(UTF-8)', $fm or die "写不了夹具：$!\n";
    print {$b} <<"MD";
- 散文里的臆造名：QBogusInProseOnly 必须报出来。
- 同一行两个臆造名只点名一个：QMarkedOK 与 QNotMarkedOther，name-ok:QMarkedOK 只放过前一个。
- 光秃秃的标记不认：QBarBareMarker，这一行只写了 name-ok 这个标记本身。
MD
    close $b;
    my $r = sweep([$fc, $fm]);
    my @cases = (
        ['注释里的臆造名照样报（自索引必须剥注释）',
         (exists $r->{unk}{QBogusInCommentOnly}) ? 1 : 0],
        ['本仓代码里定义的名字放过（自索引吃代码）',
         ($r->{unk}{QRepoDefinedInCode}) ? 0 : 1],
        ['字符串字面量里的名字不抓（扫描与自索引都剥字符串）',
         ($r->{unk}{QBogusInStringOnly}) ? 0 : 1],
        ['散文里的臆造名必须报出来',
         (exists $r->{unk}{QBogusInProseOnly}) ? 1 : 0],
        ['name-ok 点名到的那个放过', ($r->{unk}{QMarkedOK}) ? 0 : 1],
        ['同一行没点名的臆造名漏不出来（不许盲放行）',
         (exists $r->{unk}{QNotMarkedOther}) ? 1 : 0],
        ['光秃秃的 name-ok 不认作放行，改成单独报「放行未点名」',
         ($r->{loose} >= 1 && !$r->{unk}{QBarBareMarker}) ? 1 : 0],
    );
    my $bad = 0;
    for my $i (0 .. $#cases) {
        my ($name, $ok) = @{ $cases[$i] };
        printf "  %s 第 %d 条：%s\n", $ok ? '✓' : '✗', $i + 1, $name;
        $bad++ unless $ok;
    }
    printf "--selftest：夹具 token %d｜查无此名 %d 种｜放行未点名 %d｜**%s**\n",
      $r->{checked}, scalar keys %{ $r->{unk} }, $r->{loose}, $bad ? "$bad 条不通过" : "全部通过";
    return $bad ? 1 : 0;
}

# ---------- 5. 主流程 ----------
my $r = sweep(\@files);
my %unk = %{ $r->{unk} };
for my $t (sort keys %unk) {
    print "查无此名  $t\n";
    for my $where (@{ $unk{$t} }) {
        printf "        %s\n            %s\n", $where->[0], $where->[1];
    }
}
printf "----\n" unless $QUIET;
printf "API 名核对｜Qt 标识符集合 %d 个（建索引 %d 秒）｜扫描 %d 个文件｜token %d 次\n",
       $ids, $build_sec, scalar(@files), $r->{checked};
printf "  命中真名/白名单 %d｜前缀式简写放过 %d｜name-ok 放行 %d｜**查无此名 %d 种**\n",
       $r->{known}, $r->{prefix}, $r->{flagged}, scalar keys %unk;
printf "  ⚠ 另有 %d 处写了 name-ok 却没点名 token（不认作放行，见上面「放行未点名」）\n", $r->{loose} if $r->{loose};
print "（本工具是辅助队列，不是门。查无此名 ≠ 一定写错：自造名、术语、复述旧错名的段落\n",
      "  都会在这里出现，逐条读完再决定是改名还是加 name-ok。）\n" unless $QUIET;
exit 0;
