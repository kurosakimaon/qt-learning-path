#!/usr/bin/perl
# ============================================================================
# _tools/audit_citations.pl —— 头文件/源文件行号引用审计
#
# 这个仓库的每一讲都写着「qthread.h 第 145 行」「qjsonvalue.cpp 第 39~41 行」
# 这类**可核对的**引用。可核对是把双刃剑：它让读者能验，也让写错的人当场现形。
# 人是会记错行号的，而且改一次 Qt 小版本、行号就全体平移 —— 所以这条门必须
# 能在 30 秒内把「第 N 行已经越界 / 这个文件根本不叫这个名」揪出来。
#
# 它查四件事：
#   1. 引用到的文件名在本机 Qt 目录树里存不存在（公开头在 include/，
#      _p.h 私有的和 .cpp 只在 Src/ 里 —— 装好的 SDK 不交付它们，这是设计如此，
#      所以按两级查找：include → Src → 报 MISSING）。
#   2. 引用的行号（含「第 39~41 行」这种区间）是否落在该文件的真实行数内。
#   3. 引用后面紧跟的那个符号（``setParent`` / `isAutoRepeat` 之类）是否真的
#      出现在被引行的 ±WINDOW 行内。查的是「行号对不对得上内容」——
#      这一档会报 WARN 而不是失败：它会有假阳（引用的是整段而不是单行、
#      或者中文写的是「这个类的构造函数」这种不带符号的说法），
#      但 events/05 那处「isAutoRepeat 在第 446 行」的错误就是它逮的。
#   4. 顺手统计有多少条引用被解析出来 —— 数量本身就是回归信号：
#      哪天一次批量替换把引用文本弄丢了，这个数会跳水。
#
# 它**不**判断「这一行的语义是不是文中说的那件事」—— 那只有人读得出来。
# 所以这条门是「假红很少、假绿不少」的那种：它过了不代表引用全对，
# 它红了则一定是哪里坏了。
#
# 反「假绿」两道保险（都是本机踩过之后加的，别拆）：
#   · 汇总第一行报「扫描 N 个文件」——N 是这次真正读到的文件数。
#     以前 `--quiet` 会被当成文件名，于是自动扫描被跳过、一条引用都没查，
#     报告照样「全绿」（解析到 0 条）。现在参数先摘开关、再决定要不要全仓扫。
#   · 一个文件都没读到就直接 die（退出码非 0），不让 CI 拿到绿色的 0。
#
#   perl _tools/audit_citations.pl              # 扫全仓库（*.cpp *.h *.md *.html）
#   perl _tools/audit_citations.pl storage/*    # 只扫指定文件
#   perl _tools/audit_citations.pl --quiet      # 只打汇总，不打逐条 WARN
#   QT_INC=... QT_SRC=... perl _tools/audit_citations.pl
# ============================================================================
use strict;
use warnings;
use utf8;                     # 本文件是 UTF-8 源码：不加这行，正则里的「第」是 3 个字节，
                              # 而被扫文本 decode 后是 1 个字符，永远匹配不上（踩过）。
use Encode    qw(decode encode);
use File::Find;
binmode STDOUT, ':encoding(UTF-8)';

my $INC = $ENV{QT_INC} || 'D:/Qt/6.12.0/mingw_64/include';
my $SRC = $ENV{QT_SRC} || 'D:/Qt/6.12.0/Src/qtbase/src';

-d $INC or die "找不到 Qt 头文件目录：$INC\n  换机器时这样指：QT_INC=/c/Qt/6.12.0/mingw_64/include perl \$0\n";

# ── 建索引：文件名 -> 真实路径（同名取第一个；include 优先于 Src）──────────
my %where;
for my $root ($INC, $SRC) {
  next unless -d $root;
  find(sub {
    my $base = $_;
    return unless -f $base && $base =~ /\.(h|cpp)$/;
    $where{$base} ||= "$File::Find::dir/$base";
  }, $root);
}

# ── 待扫文件 ───────────────────────────────────────────────────────────────
# 先把开关从参数里摘掉再决定「要不要全仓扫」：以前是先 @files = @ARGV，
# 于是 `--quiet` 这一种写法会让 @files 非空、跳过自动扫描，最后只剩一个
# 不存在的文件 —— 报告「解析到引用 0 条」，看着像全绿，其实一条都没查（假绿，踩过）。
my $QUIET = (grep { $_ eq '--quiet' } @ARGV) ? 1 : 0;
my @files = grep { $_ ne '--quiet' } @ARGV;
if (!@files) {
  find(sub {
    my $base = $_;
    return unless -f $base && $base =~ /\.(cpp|h|md|html)$/;
    return if "$File::Find::dir/" =~ m{(^|/)(build[^/]*/|\.git/|runlogs/)};
    push @files, "$File::Find::dir/$base";
  }, '.');
}

my ($checked, $missing, $oorange, $shown) = (0, 0, 0, 0);
my $opened = 0;                 # 真的读到内容的文件数：防止「参数写错 → 一条没查 → 看着全绿」
my ($content_checked, $content_miss, $content_shown) = (0, 0, 0);
my %seen_missing;
my %body;                       # 被引用文件路径 -> 行数组引用（只读一遍）
my $WINDOW = $ENV{CITE_WINDOW} || 6;

sub qt_lines {
  my ($path) = @_;
  return $body{$path} if $body{$path};
  open my $p, '<:raw', $path or return $body{$path} = [];
  local $/;
  my $c = decode('UTF-8', scalar <$p>, Encode::FB_DEFAULT);
  close $p;
  $c =~ s/\r//g;
  my @cl = split /\n/, $c, -1;
  pop @cl if @cl && $cl[-1] eq '';   # 文件末尾那个换行不构成一行
  return $body{$path} = \@cl;
}

for my $file (sort @files) {
  open my $fh, '<:raw', $file or do { warn "读不了 $file: $!\n"; next };
  ++$opened;
  local $/;
  my $text = decode('UTF-8', scalar <$fh>, Encode::FB_DEFAULT);
  close $fh;
  $text =~ s/\r//g;
  my @lines = split /\n/, $text, -1;

  for my $i (0 .. $#lines) {
    my $line = $lines[$i];
    # 「qthread.h 第 145 行」「qjsonvalue.cpp 第 39~41 行」都吃。
    # 限定 q 开头的小写 Qt 文件名，避免把「本文第 3 节」之类误当引用。
    # 【勘误·覆盖面】这一版原来强制要求「第…行」这一对字，于是 tutorial.md 里
    # 「（qnamespace.h 1926~1980）」这种**只有文件名 + 数字、没有「行」字**的写法整批漏检
    # ——漏掉的正是讲义，因为讲义排版时爱把「行」省掉。本机数过：这样写的引用在
    # tutorial.md/index.html 里有几十条，一条都没被卷过。
    # 文件名与被引数字之间**只许隔标点**（空格 / ，、：（ 这几样，最多 3 个字符）。
    # 这条是给上一版放宽收的尾：那版允许 16 个任意字符，于是
    #   「qbytearray.h：isValidUtf8 / toHex」把 8 读成第 8 行，
    #   「qapplication.h：i18n 的运行时地基」把 18 读成第 18 行，
    #   「只 grep `qwidget.h` 就断言「Qt6 删了…」」把 6 读成第 6 行 —— 全是假阳。
    # 代价是「qresource.h 全文 70 行」这类**文件长度**说法不再被卷（它本来也不是行号引用）。
    while ($line =~ /\b(q[a-z_0-9]+\.(?:h|cpp))([，,、：:（(\s]{0,3})(?:第)?\s*([0-9]{1,5})\s*(?:[~、,至\-\/]{1,2}\s*([0-9]{1,5})\s*)?(?:行|(?![0-9.]))/g) {
      my ($name, $sep, $from, $to) = ($1, $2, $3, $4);
      # 「不存在 / 没有 / 找不到」是叙述语气，不是行号引用。
      next if $sep =~ /不存|没有|找不|未找/;
      $checked++;

      my $path = $where{$name};
      if (!$path) {
        $missing++;
        unless ($seen_missing{$name}++) {
          print "MISSING   $file:" . ($i + 1) . "  $name（include 和 Src 里都没有这个文件）\n";
          $shown++;
        }
        next;
      }

      my $cl = qt_lines($path);
      my $n  = scalar @$cl;
      my $hi = defined $to ? $to : $from;
      if ($from < 1 || $hi > $n) {
        $oorange++;
        print "OUT-OF-RANGE  $file:" . ($i + 1) . "  $name 第$from"
          . (defined $to ? "~$to" : "") . "行（该文件实为 $n 行 → $path）\n";
        $shown++;
        next;
      }

      # ── 第 3 档：引用附近的符号，是否真在被引行附近 ──────────────────────
      #   中文讲义里引用有两种写法，都得吃：
      #     「resize(int,int)（qwidget.h 第 503 行）」  —— 符号在引用**前面**
      #     「qwidget.h 第 503 行 resize(int,int)」      —— 符号在引用**后面**
      #   所以取引用两侧各 60 字符、并在「下一个引用 / 句号 / 分号 / 右括号」处截断，
      #   再从这段上下文里挑出所有 >=5 字符的 ASCII 标识符，要求**至少一个**
      #   出现在被引行的 ±WINDOW 内。用「至少一个」而不是「最长的一个」是踩过之后
      #   改的：一句里常并列好几个引用（qwidget.h 503 / 369），抓错符号的假阳率极高。
      my $start  = $-[0];
      my $after  = substr($line, $+[0]);
      $after =~ s/（(q[a-z_0-9]+\.h).*$//s;
      $after =~ s/[；;。，、）\)<>].*$//s;
      $after =~ s{[A-Za-z0-9_]+/}{}g;
      # 引用**后面**还接着切两刀，这两刀都是这次批量补出来的假阳教出来的：
      #   1) 切到下一个文件名之前 ——「qthreadpool.h：waitForDone(0) 的轮询、clear(81)、
      #      contains(83)」这种一行并列好几个引用的，不切就会拿后一条的符号去查前一条的行。
      #   2) 切到下一个「N 行」之前 ——「qpointer.h：19 行 class QPointer / 86 行 isNull」
      #      里，第 19 行那条的上下文会漏进 86 行的 isNull，于是拿 isNull 去 qpointer.h
      #      第 19 行附近找，报「找不到」——假的，19 行确实是 class QPointer。
      $after =~ s/\s*(?:[qQ][a-z_0-9]+\.(?:h|cpp)).*$//s;
      $after =~ s/\s*第?\s*[0-9]{1,5}\s*(?:[~、,至\-\/]\s*[0-9]{1,5}\s*)?行.*$//s;
      my $before = substr($line, 0, $start);
      $before =~ s/^.*[（(；;。，]//s;
      # 路径片段先抹掉：「QtWidgets/qbuttongroup.h」「sql/kernel/qsqldriver.cpp」里的
      # QtWidgets / sql / kernel 会被当成标识符，然后永远匹配不上 —— 这是纯假阳。
      $before =~ s{[A-Za-z0-9_]+/}{}g;
      my $ctx = substr($before, -60) . ' ' . $sep . ' ' . substr($after, 0, 60);
      # visuals/*.html 的 caption 是带标签的：`<strong>` / `<code>` 会被当成标识符，
      # 然后永远在头文件里找不到 —— 三条假阳就是这么来的。先把标签与实体抹掉再挑符号。
      $ctx =~ s/<[^>]*>/ /g;
      $ctx =~ s/&[a-z]+;/ /g;
      # 「含字母」这一条是这次加的：标识符字符集允许以 ~ 开头，于是
      # 「1257~1277、1317」里的「~1317」被当成符号拿去头文件里找，永远找不到 —— 纯假阳。
      my @cands = grep { length($_) >= 5 && /[A-Za-z]/ } $ctx =~ /[A-Za-z_~][A-Za-z0-9_]{4,}/g;
      my %noise = map { $_ => 1 } qw(
        true false const nullptr override include import class enum enumd
        QString QObject QWidget QList List Name name http https cpp html int
        void this that with from under about
      );
      # 编译开关不是被引文件里的符号：讲义说「Release 定义了 QT_NO_DEBUG，所以这行的
      # Q_ASSERT_X 被编译掉」，引用点在 qglobal.h 那边，被引行里当然找不到它 —— 假阳。
      my %flag = map { $_ => 1 } qw(QT_NO_DEBUG QT_NO_KEYWORDS QT_NO_EMIT QT_NO_CAST_FROM_ASCII);
      # 上一条引用的文件名会漏进这一条的上下文（「…qlocalsocket.h 第 74 行、
      # qsslsocket.h 第 84 行…」），拿它去 qsslsocket.h 里找当然找不到 —— 假阳。
      my $stem = $name; $stem =~ s/\.(h|cpp)$//;
      @cands = grep { !$noise{$_} && !$flag{$_} && lc($_) ne $stem && !$where{"$_.h"} && !$where{"$_.cpp"} } @cands;
      next unless @cands;
      $content_checked++;

      # 讲义里的「第 N 行」是 1 基，@$cl 是 0 基 —— 不 -1 就会整窗错位一行，
      # 「函数体第 604 行、函数名在 598 行」这种就会假报找不到（踩过）。
      my $lo = $from - 1 - $WINDOW; $lo = 0 if $lo < 0;
      my $stop = $hi - 1 + $WINDOW; $stop = $n - 1 if $stop > $n - 1;
      my $band = lc join("\n", map { $cl->[$_] // '' } $lo .. $stop);
      my $hit = 0;
      for my $sym (@cands) {
        # 大小写不敏感：讲义写 nodiscard，头文件里是 QFILE_MAYBE_NODISCARD —— 同一个东西。
        if (index($band, lc $sym) >= 0) { $hit = 1; last }
      }
      if (!$hit) {
        $content_miss++;
        unless ($QUIET) {
          my $cand = join ',', @cands[0 .. ($#cands > 4 ? 4 : $#cands)];
          print "WARN 内容对不上  $file:" . ($i + 1) . "  $name 第$from"
            . (defined $to ? "~$to" : "") . "行  附近 " . (2 * $WINDOW + 1)
            . " 行内找不到「$cand」里任何一个\n";
          print "        引用原文：$line\n";
          print "        实际第 $from 行：" . ($cl->[$from - 1] // '') . "\n";
          $content_shown++;
        }
      }
    }
  }
}

print "-" x 60, "\n";
printf "扫描 %d 个文件｜解析到引用 %d 条｜文件缺失 %d 条｜行号越界 %d 条\n",
       scalar(@files), $checked, $missing, $oorange;
die "一个文件都没读到（参数写错了？路径不存在？），本次没有核对任何引用\n"
  unless $opened;
printf "内容核对 %d 条｜符号在附近找不到 %d 条（WARN，不算失败）\n", $content_checked, $content_miss;
if ($missing || $oorange) {
  print "结论：有引用的行号/文件名对不上本机 Qt —— 逐条改，或把 Qt 版本装对。\n";
  exit 1;
}
print "结论：所有行号都落在被引用文件的真实行数内（语义仍需人读）。\n";
exit 0;
