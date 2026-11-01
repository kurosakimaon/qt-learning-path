#!/usr/bin/env perl
# ============================================================================
# _tools/check_console_glyphs.pl
# ----------------------------------------------------------------------------
# 查「Windows 控制台打不出来的字符」——只管**真的会进 stdout/stderr 的那些**。
#
# 为什么要这个：Windows 上 Qt 的日志按**本地代码页**输出（本机是 GBK）。
# 字符串里有 GBK 编码不了的字符时，Qt 不会报错，它直接把那个字符换成 **?**。
# 于是 runlogs 里出现 `⚠ 读不到` 变成 `? 读不到`、`A − B` 变成 `A ? B`，
# 而程序退出码照样 0 —— 恰恰是「证据」那一行变得读不懂。
#
# 口径：
#   · 只检查**注释之外**的字符（注释、文档字符串不参与输出，随你写什么符号）。
#   · 判据 = 「这个字符能不能被 Encode::encode('gbk') 编码」。
#     GBK 覆盖了 ①..⑩、⑴..、×、√、→、℃ 这类常用符号，但**不含**
#     ⚠(U+26A0) ⑪(U+246A) ⓿(U+24FF) ⇒ ↔ ↳ ▶ ✅ ❌ ✓ ✗ − 和任何 emoji。
#   · 也检查**码点转义** `\uXXXX` / `\UXXXXXXXX`：这类行是纯 ASCII，早先的版本会
#     直接跳过，于是 storage/03 那个火苗躲过了自检、在日志里变成两个 ?。
#     现在它按同一个判据报；故意保留的写法在同一行注释里写
#     `glyph-ok:U+1F525 理由`，降级成提示、不影响退出码。`\\U...`（讲转义本身）不算。
#   · 退出码：0 = 干净；1 = 有字符会被打成 ?；2 = 用法/读文件错。
#
# 用法：
#   perl _tools/check_console_glyphs.pl                 # 全仓库（跳过 build*/ _scratch/）
#   perl _tools/check_console_glyphs.pl painting/07*.cpp
#   perl _tools/check_console_glyphs.pl --explain       # 顺带打印替换建议表
# ============================================================================
use strict;
use warnings;
use utf8;
use Encode qw(encode decode);

# 报告本身要往终端打**宽字符**，不设这个就是一堆 "Wide character in printf" 警告盖住正文
binmode(STDOUT, ':encoding(UTF-8)');
binmode(STDERR, ':encoding(UTF-8)');




my $ROOT = -d '_tools' ? '.' : '..';
my $EXPLAIN = 0;
my @explicit;
for my $a (@ARGV) {
    if ($a eq '--explain') { $EXPLAIN = 1; }
    elsif ($a =~ /^-/) { print "未知参数：$a\n"; exit 2; }
    else { push @explicit, $a; }
}

# 建议替换表（全部落在 GBK 里，已逐个 encode 过）
my %SUGGEST = (
    0x26A0 => '【注意】', 0x24FF => '〇',    0x246A => '(11)',  0x21D2 => '=>',
    0x2194 => '<->',      0x21B3 => '->',    0x25B6 => '>',     0x2705 => '【通过】',
    0x274C => '【失败】', 0x2713 => '√',     0x2714 => '√',     0x2715 => '×',
    0x2717 => '×',        0x2718 => '×',     0x2212 => '-',     0x207B => '^-',
    0x00AE => '(R)',      0x2318 => 'Command键', 0x1F525 => '【重要】',
);
sub suggest {
    my ($cp) = @_;
    return $SUGGEST{$cp} // sprintf('(改成 GBK 里有的写法，U+%04X)', $cp);
}

# ---- 码点转义：本工具原先的盲区，现在补上 ----
# 裸字符那套判据看不见「写在源码里的转义」：`\U0001F525` 整行都是纯 ASCII，
# 连下面那个 `[^\x00-\x7f]` 短路都进不去。可它在**运行期**就是一个字符，
# 和裸字符完全同一个病。本机实测（_scratch/emojitrap.cpp，Qt 6.12.0）：
#   QStringLiteral("热\U0001F525火").toLocal8Bit() = c8 c8 3f 3f bb f0
# 也就是「热 + 两个 ? + 火」——不是两个字符，因为 UTF-16 里它是 2 个码元，
# 每个装不下的码元换一个 ?（去掉 QStringEncoder 的 Stateless 也是同样字节，
# 别赖 flag；而 qstring.cpp 5718~5719 行写明这种转换 behavior is undefined，
# 所以 ? 的**个数**不能当契约，只能当「这儿丢了东西」）。
# 判据同裸字符：GBK 编不出去就报。例外写法——同一行注释里写
#   glyph-ok:U+1F525 理由……
# 命中就降级成「提示」，不影响退出码（讲义里故意要展示闷棍的地方用）。
# 注意 `\\U0001F525`（两个反斜杠）是在**讲**转义本身，字符串里没有那个字符，
# 所以正则带 (?<!\\)：前一个字符还是反斜杠的一律不算。
sub scan_escapes {
    my ($line) = @_;
    my @cps;
    while ($line =~ /(?<!\\)\\U([0-9A-Fa-f]{8})/g) { push @cps, hex($1); }
    while ($line =~ /(?<!\\)\\u([0-9A-Fa-f]{4})/g)  { push @cps, hex($1); }
    return @cps;
}
sub allowed_on_line {          # 从**原始行**（含注释）里取 glyph-ok 白名单
    my ($orig) = @_;
    my %ok;
    while ($orig =~ /glyph-ok:U\+([0-9A-Fa-f]{1,8})/g) { $ok{hex($1)} = 1; }
    return %ok;
}

if ($EXPLAIN) {
    print "代码点 -> 建议替换：\n";
    for my $cp (sort { $a <=> $b } keys %SUGGEST) {
        my $enc = eval { encode('gbk', chr($cp), Encode::FB_CROAK) };
        printf("  U+%04X  %s ->  %s   [GBK 可编: %s]\n",
               $cp, chr($cp), $SUGGEST{$cp}, $enc ? '是' : '否');
    }
    exit 0;
}

# ---- 收集文件 ----
# 只走「教程式源码」那几个目录，**不要** File::Find 全仓库：
# build*/ 里几十万个文件，光是遍历就要几分钟（第一版就是这么卡死的）。
my @MODULE_DIRS = qw(start painting events metaobject model_view threads
                    storage network quality widgets projects _assets);
my @files;
if (@explicit) {
    @files = @explicit;
} else {
    for my $d (@MODULE_DIRS) {
        my $abs = "$ROOT/$d";
        next unless -d $abs;
        opendir(my $dh, $abs) or next;
        for my $e (sort readdir $dh) {
            next if $e eq '.' or $e eq '..';
            my $p = "$abs/$e";
            push @files, $p if -f $p && $p =~ /\.(cpp|h|hpp)$/;
            next unless -d $p;                       # projects/ 下还有一层工程目录
            opendir(my $sub, $p) or next;
            for my $e2 (sort readdir $sub) {
                my $p2 = "$p/$e2";
                push @files, $p2 if -f $p2 && $p2 =~ /\.(cpp|h|hpp)$/;
            }
            closedir $sub;
        }
        closedir $dh;
    }
}


# ---- 去注释 ----
# 一个正则一遍扫完全文：匹配「字符串字面量 / 字符字面量 / 行注释 / 块注释」四选一，
# 命中串就原样留下，命中注释就把非换行字符刷成空格（行号保持不变）。
# 之所以用正则而不是逐字符状态机：40KB 的讲义逐字符 substr 要 1.7s，全仓库九十来个文件
# 就是几分钟——第一版就是这么卡到超时的。
sub strip_comments {
    my ($text) = @_;
    $text =~ s{
          ("(?:\\.|[^"\\])*")            # 1 = 字符串字面量（串里的 // 不算注释）
        | ('(?:\\.|[^'\\])*')            # 2 = 字符字面量
        | (//[^\n]*)                     # 3 = 行注释
        | (/\*.*?\*/)                    # 4 = 块注释（/s 让它跨行）
    }{
        (defined $1 || defined $2) ? $& : ($& =~ s/[^\n]/ /gr);
    }gsex;
    return $text;
}

my $bad_total = 0;
my $esc_total = 0;              # 转义写法里被 glyph-ok 放行的处数，只报数，不算失败
my %by_cp;
my %gbk_ok;                       # 代码点 → 能否编进 GBK（缓存：eval 很贵，别每字符调一次）
sub is_gbk {
    my ($cp) = @_;
    return $gbk_ok{$cp} if exists $gbk_ok{$cp};
    my $ok = eval { encode('gbk', chr($cp), Encode::FB_CROAK); 1 };
    $gbk_ok{$cp} = $ok ? 1 : 0;
    return $gbk_ok{$cp};
}
for my $f (@files) {
    open(my $fh, '<:raw', $f) or do { print "读不到 $f: $!\n"; next; };
    my $raw = do { local $/; <$fh> };
    my $decoded = decode('UTF-8', $raw, Encode::FB_DEFAULT);
    close $fh;
    my $code = strip_comments($decoded);
    my @lines = split /\n/, $code, -1;
    my @origs = split /\n/, $decoded, -1;      # 注释还留在里面：白名单靠它
    for my $idx (0 .. $#lines) {
        my $line = $lines[$idx];
        # 码点转义这一遍**不能**被下面的纯 ASCII 短路跳过：`\U0001F525` 那种行全是 ASCII
        my %allow = allowed_on_line($origs[$idx] // '');
        for my $cp (scan_escapes($line)) {
            next if is_gbk($cp);
            if ($allow{$cp}) {
                ++$esc_total;
                printf("%s:%d  U+%04X 码点转义 已放行（glyph-ok）\n", $f, $idx + 1, $cp);
            } else {
                ++$bad_total;
                $by_cp{$cp}++;
                printf("%s:%d  U+%04X 码点转义 运行期会被打成 ?  → 建议 %s"
                       . "（故意展示闷棍，就在同一行注释写 glyph-ok:U+%04X 理由）\n",
                       $f, $idx + 1, $cp, suggest($cp), $cp);
            }
        }
        next unless $line =~ /[^\x00-\x7f]/;          # 纯 ASCII 行直接跳过
        my @hits;
        for my $ch (split //, $line) {
            my $cp = ord $ch;
            next if $cp < 0x80;
            push @hits, $cp unless is_gbk($cp);
        }
        next unless @hits;
        my %uniq; $uniq{$_}++ for @hits;
        for my $cp (sort { $a <=> $b } keys %uniq) {
            $bad_total += $uniq{$cp};
            $by_cp{$cp} += $uniq{$cp};
            printf("%s:%d  U+%04X '%s'  会被打成 ?  → 建议 %s\n",
                   $f, $idx + 1, $cp, chr($cp), suggest($cp));
        }
    }
}

if ($bad_total == 0) {
    printf("控制台字形自检：所有 .cpp/.h 的**可输出**字符都能被 GBK 编码（共扫 %d 个文件）\n",
           scalar(@files));
    printf("  另有 %d 处码点转义是 glyph-ok 放行的（运行期确实会变成 ?，讲义故意的）\n",
           $esc_total) if $esc_total;
    exit 0;
}
print "---\n共 $bad_total 处会被 Windows 控制台（GBK）打成 ?（裸字符与码点转义一起算）\n";
print "按代码点统计：";
print join(', ', map { sprintf("U+%04X×%d", $_, $by_cp{$_}) } sort { $a <=> $b } keys %by_cp), "\n";
print "改法：换成上面「建议」里的写法（这些建议逐个验过能编进 GBK）。\n";
exit 1;
