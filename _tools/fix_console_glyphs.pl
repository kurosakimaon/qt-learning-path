#!/usr/bin/env perl
# ============================================================================
# _tools/fix_console_glyphs.pl
# ----------------------------------------------------------------------------
# check_console_glyphs.pl 的「动手版」：把会被 Windows 控制台（GBK）打成 ? 的字符，
# 换成 GBK 里有的等价写法。
#
# 三件事必须小心，前两件事是这个脚本的全部难度所在：
#
#   ① **只改字符串/字符字面量里面**，注释一律不动。
#      注释不进 stdout，写成 ? 没人看见；把讲义注释里的 ⚠ 换掉纯属噪音。
#      正则与 check 版同构：一次扫全文，四分支「双引号串 / 单引号字符 / 行注释 / 块注释」
#      按**起始位置先后**竞争，所以注释里那个引号不会被当成字面量开头。
#      不能按行处理：块注释跨行，按行看不出自己在注释里，就会改到注释里去。
#
#   ② **替换表的合法性在启动时一次验完，替换现场只查表。**
#      原因不是洁癖，是实测踩出来的两个 Perl 坑，都会把「改字符」变成「删字符」：
#      (a) Encode 带 CHECK 时**就地改写你传进去的那个标量**。
#          本机 /usr/lib/perl5/core_perl/Encode.pm（perl 5.42.3）第 743~744 行原文：
#            "If the C<Encode::LEAVE_SRC> bit is I<not> set but I<CHECK> is set,
#             then the source string to encode() or decode() will be overwritten in place."
#          我第一版在 /e 代码里写了 encode('gbk', $rep, FB_CROAK)，$rep 正是
#          从 %REPL 取出来那个替换串——它被**原地清空**，于是 ⚠ 不是换成【注意】，
#          而是换成空串，整句话少了四个字符，程序还照样跑绿。
#          => 凡是带 CHECK 的 encode/decode，一律 `| Encode::LEAVE_SRC`（实测见 --selftest）。
#      (b) 因此在 /e 替换现场**什么都不调**：不 encode、不 sprintf、不 push。
#          现场只做一次哈希查表，副作用全挪到循环外面。
#      启动时先把 %REPL 每个值 encode→decode 往返一遍，有任何一个不合格就**整批不写盘**。
#
#   ③ 文件里出现 R"( 原始字符串就**拒绝处理该文件**。
#      正则分不出原始串里的 " 和 /*，硬改会把注释/代码口径搞混。
#      （本仓库目前 0 处；以后有人用了会在这里被挡住，而不是悄悄改坏。）
#
# 故意**不放进替换表**的两个码点（脚本只提醒、不改，交给人判断）：
#   U+207B ⁻  —— 讲义里是 A⁻¹，邻位的 ¹ 本身 GBK 可编。
#                机械换成 ^- 会得到 A^-¹，比原来更难读，只能整段手改成 A^-1。
#   U+1F525 🔥 —— storage/03_json.cpp 用它演示「emoji 原样以 UTF-8 字节写出」，
#                换掉字符就把知识点换了，得改成「代码点 + 字节」的说法而不是删符号。
#
# 用法：
#   perl _tools/fix_console_glyphs.pl --dry-run            # 只看会改什么，不落盘
#   perl _tools/fix_console_glyphs.pl                      # 全仓库替换
#   perl _tools/fix_console_glyphs.pl painting/04*.cpp     # 只改指定文件
#   perl _tools/fix_console_glyphs.pl --list-unsupported   # 列出脚本故意不管的码点
# 退出码：0 = 改完（或本来就干净）；1 = 替换表不合格，已整批中止；2 = 用法错。
# ============================================================================
use strict;
use warnings;
use utf8;
use Encode qw(encode decode);

binmode(STDOUT, ':encoding(UTF-8)');
binmode(STDERR, ':encoding(UTF-8)');

my $ROOT = -d '_tools' ? '.' : '..';
my $DRY = 0;
my $LIST = 0;
my $SELFTEST = 0;
my @explicit;
for my $a (@ARGV) {
    if    ($a eq '--dry-run')          { $DRY = 1; }
    elsif ($a eq '--list-unsupported') { $LIST = 1; }
    elsif ($a eq '--selftest')         { $SELFTEST = 1; }
    elsif ($a =~ /^-/)                 { print "未知参数：$a\n"; exit 2; }
    else                               { push @explicit, $a; }
}

# ---- 替换表：码点 -> 换成什么 ----
my %REPL = (
    0x26A0 => '【注意】',   # ⚠
    0x24FF => '〇',         # ⓿  —— 用汉字「〇」：GBK 的带圈数字只到⑩，⓪/⓿ 那一档没有
    0x246A => '(11)',       # ⑪  —— 同上，11 起没有对应带圈字形
    0x21D2 => '=>',         # ⇒
    0x2194 => '<->',        # ↔
    0x21B3 => '->',         # ↳
    0x25B6 => '>',          # ▶
    0x2705 => '【通过】',   # ✅
    0x274C => '【失败】',   # ❌
    0x2713 => '√',          # ✓
    0x2714 => '√',
    0x2715 => '×',
    0x2717 => '×',
    0x2718 => '×',
    0x2212 => '-',          # − 数学减号
    0x00AE => '(R)',
    0x2318 => 'Command键',  # ⌘
);
my %HAND = (
    0x207B  => 'A⁻¹ 这类上标：邻位 ¹ 本身 GBK 可编，机械替换会留下 A^-¹。整段手改成 A^-1。',
    0x1F525 => 'emoji 若是讲义内容（演示 UTF-8 原字节）：改成「代码点 + 字节」的说法，别删符号。',
);

# ---- 纪律②：替换表先过一遍 GBK 往返，不合格就在此刻退出，绝不带着坏表去改文件 ----
# CHECK 必须 | LEAVE_SRC，否则 encode 会把 $REPL{$cp} 那个替换串**原地清空**（文件头②(a)）。
my $STRICT = Encode::FB_CROAK | Encode::LEAVE_SRC;
my @bad;
for my $cp (sort { $a <=> $b } keys %REPL) {
    my $rep = $REPL{$cp};
    my $rt  = eval { decode('gbk', encode('gbk', $rep, $STRICT), $STRICT) };
    if ($@ || !defined $rt) {
        push @bad, sprintf("U+%04X '%s' 的替换串 '%s' 编不进 GBK", $cp, chr($cp), $rep);
    } elsif ($rt ne $rep) {
        push @bad, sprintf("U+%04X '%s' 的替换串 '%s' GBK 往返后变成 '%s'（丢信息）",
                           $cp, chr($cp), $rep, $rt);
    } elsif (length $rep == 0) {
        push @bad, sprintf("U+%04X 的替换串是空的：要么表写错，要么它被 encode 原地清空了"
                           . "（CHECK 没配 LEAVE_SRC 就是后者）", $cp);
    }
}
if (@bad) {
    print "$_\n" for @bad;
    print "替换表不合格，**没有改任何文件**。\n";
    exit 1;
}

if ($LIST) {
    print "本脚本故意不自动替换的码点（要人判断，理由见文件头）：\n";
    printf("  U+%04X '%s'  %s\n", $_, chr($_), $HAND{$_}) for sort { $a <=> $b } keys %HAND;
    exit 0;
}

# ---- 收集文件（目录口径与 check_console_glyphs.pl 一致）----
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

# 四选一：位置最靠前的 token 获胜 => 注释里的引号不会被当成字面量。
# 两个修饰符都不能少：/x 让缩进与 # 说明不参与匹配（第一版漏了它，于是「永远匹配不上」），
# /s 让块注释的 .*? 跨得过换行。
my $TOKEN = qr{
      ("(?:\\.|[^"\\])*")            # 1 = 双引号字面量
    | ('(?:\\.|[^'\\])*')            # 2 = 字符字面量
    | (//[^\n]*)                     # 3 = 行注释
    | (/\*.*?\*/)                    # 4 = 块注释
}sx;
# 现场只查表 => 把「要换的字符」做成一个字符类，替换值就是哈希查找，不带任何副作用。
# 注意 qr// 里不能拼表达式（那不是代码），所以先算字符串、再塞进 qr//。
my $REPL_CLASS = '[' . join('', map { sprintf '\\x{%X}', $_ } sort { $a <=> $b } keys %REPL) . ']';
my $HAND_CLASS = '[' . join('', map { sprintf '\\x{%X}', $_ } sort { $a <=> $b } keys %HAND) . ']';
my $REPL_RE = qr/$REPL_CLASS/u;
my $HAND_RE = qr/$HAND_CLASS/u;

my ($changed_files, $changed_chars) = (0, 0);
my (@hand_hits, @skipped, @writes);

# ---- 自检：把上面两个坑钉成可执行的断言 ----
# 为什么要有这一段：文件头②那两个坑，症状都是「字符凭空消失」，
# 而当时 check_console_glyphs.pl 会报 0 处（? 没了，字符也没了），**绿灯恰恰是坏的**。
# 所以这里直接断言「换进去了、没少字」，让同类错误在工具里炸，而不是在讲义里静默生效。
sub selftest {
    my @fail;
    my $chk = sub { my ($name, $ok) = @_; push @fail, $name unless $ok; };

    # (1)(2)(3) LEAVE_SRC 语义本身：这三条就是这次踩坑的取证，跑一次就知道谁在骗人
    my $s1 = '【注意】'; encode('gbk', $s1, Encode::FB_CROAK);
    $chk->("(1) 反向断言：不带 LEAVE_SRC 时 encode 把源串清空了（这正是踩到的坑）", length($s1) == 0);
    my $s2 = '【注意】'; my $o2 = eval { encode('gbk', $s2, $STRICT) };
    $chk->("(2) 带上 LEAVE_SRC 后源串保持完整", defined $o2 && length($o2) == 8 && $s2 eq '【注意】');
    $chk->("(3) GBK 编不了的字符必须 CROAK，不能悄悄返回空串",
           do { my $x = '⚠'; my $r = eval { encode('gbk', $x, $STRICT); 1 }; $@ && length($x) == 1 });

    # (4)~(8) 现场只查表：字面量换、注释不换（含跨行块注释里那对引号）、而且不丢字符
    my $sample = join("\n",
        'qInfo() << "⚠ 这里要换 ⇒ 也要换";   // 注释里的 ⚠ 不许动',
        '/* 块注释里的 "带引号的⚠" 也不许动',
        '   第二行还是注释 ⚠ */',
        'qInfo() << "〇 已经是 GBK 里的字，不该被动";');
    my ($got, $hits, $hand, $replaced) = transform($sample);
    $chk->("(4) 字面量里的 ⚠/⇒ 换成了【注意】/=>", $got =~ /"【注意】 这里要换 => 也要换"/);
    $chk->("(5) 行注释里的 ⚠ 原样保留",            $got =~ /注释里的 ⚠ 不许动/);
    $chk->("(6) 跨行块注释（含其中的那对引号）原样保留",
           $got =~ /"带引号的⚠" 也不许动\n   第二行还是注释 ⚠/);
    $chk->("(7) GBK 里本来就有的字（〇）不被改动",  $got =~ /"〇 已经是 GBK 里的字，不该被动"/);
    # (8) 这条是「字符凭空消失」的专用探针：期望变长的 = Σ(替换串长度-1)，差一个都算失败。
    #     当年替换值被清空时，长度是**变短**的，而 ? 数量的检查完全看不出来。
    my $expect_growth = 0;
    $expect_growth += length($REPL{$_}) - 1 for @$replaced;
    $chk->("(8) 长度变化 = Σ(替换串长度-1)，证明每个字符都被**换掉**而不是被**删掉**"
           . "（命中 $hits 处，期望 +$expect_growth，实际 " . (length($got) - length($sample)) . "）",
           $hits == @$replaced && length($got) - length($sample) == $expect_growth);

    # (9) 原始字符串：整份文件跳过的判据本身
    $chk->("(9) 判据能认出 R\"(，且不会把普通字面量误判成原始串",
           has_raw_string('auto s = R"(⚠ 原始串)";') && !has_raw_string('qInfo() << "⚠ 普通串";'));

    if (@fail) { print "$_\n" for @fail; print "自检 **失败** ", scalar(@fail), " 项\n"; return 1; }
    print "自检 9 项全部通过（含「不带 LEAVE_SRC 会把源串清空」这条反向断言）\n";
    return 0;
}

# 纪律③的判据，抽出来是为了让它能被自检打到（主循环也调它，不留两份实现）
sub has_raw_string {
    my ($text) = @_;
    return $text =~ /R"\(/ ? 1 : 0;
}

# 把一份源码文本按规则走一遍，返回四个值：
#   ($改后文本, 换掉的字符数, [[偏移,码点]...] 要人判断的位置, [被换掉的码点...]）
# 自检和主循环**共用这一条通路**——不要写两套实现，否则「测过的」和「跑的是的」不是一个东西。
sub transform {
    my ($text) = @_;
    my ($built, $cursor, $hits) = ('', 0, 0);
    my (@hand, @replaced);
    while ($text =~ /$TOKEN/gs) {
        my ($start, $end) = ($-[0], $+[0]);
        $built .= substr($text, $cursor, $start - $cursor);
        $cursor  = $end;
        my $tok = substr($text, $start, $end - $start);
        if (defined $1 || defined $2) {                # 捕获先落到词法变量，下面每次正则都会冲掉它
            my $q        = substr($tok, 0, 1);
            my $body     = substr($tok, 1, length($tok) - 2);
            my $body_off = $start + 1;
            my $b2 = $body;
            while ($b2 =~ /($HAND_RE)/g) { push @hand, [ $body_off + $-[0], ord $1 ] }
            my @cp;
            my $b3 = $body;
            while ($b3 =~ /($REPL_RE)/g) { push @cp, ord $1 }
            push @replaced, @cp;
            $hits += @cp;
            my $new = $body;
            $new =~ s/$REPL_RE/$REPL{ord $&}/ge;       # 现场唯一的动作：查表
            $built .= $q . $new . $q;
        } else {
            $built .= $tok;                            # 注释原样
        }
    }
    $built .= substr($text, $cursor);
    return ($built, $hits, \@hand, \@replaced);
}

if ($SELFTEST) { exit selftest(); }

for my $f (@files) {
    open(my $fh, '<:raw', $f) or do { print "读不到 $f: $!\n"; next; };
    my $raw = do { local $/; <$fh> };
    close $fh;
    my $text = decode('UTF-8', $raw, Encode::FB_DEFAULT);

    if (has_raw_string($text)) {                       # 纪律③
        push @skipped, "$f（含 R\"( 原始字符串，本工具不解析它，跳过）";
        next;
    }

    # 行号表：报告用。一次算好，别在每个命中里回数。
    my @line_start = (0);
    {
        my $off = 0;
        while ((my $nl = index($text, "\n", $off)) >= 0) { push @line_start, $nl + 1; $off = $nl + 1; }
    }
    my $line_of = sub {
        my ($off) = @_;
        my ($lo, $hi) = (0, $#line_start);
        while ($lo < $hi) { my $mid = int(($lo + $hi + 1) / 2); if ($line_start[$mid] <= $off) { $lo = $mid } else { $hi = $mid - 1 } }
        return $lo + 1;
    };

    my ($built, $file_touches, $hand) = transform($text);
    push @hand_hits, sprintf("%s:%d  U+%04X '%s'  %s",
                             $f, $line_of->($_->[0]), $_->[1], chr($_->[1]), $HAND{$_->[1]})
        for @$hand;
    next unless $file_touches;

    $changed_files++;
    $changed_chars += $file_touches;
    printf("%-56s 换 %d 处%s\n", $f, $file_touches, $DRY ? '（--dry-run，未写盘）' : '');
    push @writes, [ $f, $built ] unless $DRY;
}

if (@skipped) { print "跳过：\n"; print "  $_\n" for @skipped; }
for my $w (@writes) {
    open(my $out, '>:raw', $w->[0]) or do { print "写不了 $w->[0]: $!\n"; next; };
    print {$out} encode('UTF-8', $w->[1]);
    close $out;
}

print $DRY ? "--- dry-run 结束：上面的改动还没落盘 ---\n" : "--- 替换完成 ---\n";
printf("共 %d 个文件、%d 处字符%s。\n",
       $changed_files, $changed_chars, $DRY ? '（待写入）' : '已写入');
if (@hand_hits) {
    print "\n以下位置**故意留给你手改**（脚本没动，check 版还会报）：\n";
    print "  $_\n" for @hand_hits;
}
print "\n下一步：perl _tools/check_console_glyphs.pl   # 期望 0 处\n";
exit 0;
