#!/usr/bin/perl
# ============================================================================
# _tools/check_visual_steps.pl —— visuals/*.html 的「步骤数据」结构体检
# ----------------------------------------------------------------------------
# 为什么需要它：
#   visuals/<页>.html 把每一张图写成一个 JS 数组 `window.STEPS = [ {...}, ... ]`，
#   每个元素有 title / nav / ms / stage（一段内联 SVG）/ caption / code / highlight。
#   这堆东西**全是手写文本**，没有编译、没有测试：
#     · `highlight: [1, 8, 12]` 是**下标**，不是行号（visual.js 用它去切 code 的行数组）；
#       越界 → 那一行高亮悄悄消失；落在空行上 → 高亮框画在空气上；
#     · code 面板是模板字符串，里面反引号、`${`、没闭合的反引号都会把整页 JS 打断，
#       打断之后页面直接空白（读者看到的就是「这个图解坏了」）；
#     · 批量替换时最容易出事：一条 perl 正则写错就能把几十个 step 整体删掉
#       （本仓踩过：`$1[$new]` 被 Perl 解析成「@1 的第 [$new] 个元素」，
#        于是「匹配到的整段」被替换成空 —— 17 个文件少了 24 步）。
#   所以这个脚本按「step 数组」的粒度去数、去查，改一次跑一次，30 秒内告诉你
#   有没有把结构改坏。**它只读，不改文件。**
#
# 用法：
#   perl _tools/check_visual_steps.pl                # 查 visuals/ 全部页面
#   perl _tools/check_visual_steps.pl visuals        # 只查某个目录
#   perl _tools/check_visual_steps.pl visuals/qobject_tree.html   # 只查一个文件
#
# 它查七件事：
#   1. 页面里确实有 `window.STEPS = [`，且能按 `  {` 边界数出 step 个数（≥ 1）；
#   2. 每个 step 都有 title / nav / ms / stage / caption / code / highlight 七个字段；
#   3. `highlight` 的每个下标都是非负整数、且 < 该 code 面板的行数（**0 基**）；
#   4. 下标不许落在空行、也不许落在单独的 `}` / `};` 上（高亮它没意义）；
#   5. code 面板里不许出现未转义的反引号与 `${`（后者会被 JS 当模板插值）；
#   6. 汇总每页 step 数 —— 数量本身就是回归信号：某天一次批量替换把 step 弄丢了，
#      这个数会跳水，本脚本当场就能看见。
#   7. 整段内联脚本的括号配平（借 _tools/JsBalance.pm，它会剥掉字符串/模板/注释/正则再数）：
#      这一项是**必需**的。本仓一次批量替换留下 7 个页面各多一个悬空的 `},`，
#      页面在浏览器里全白，而 1~6 项全绿 —— 正则看不见语法错误，只有配平能看见。
#
# 期望值：最后一行 ALL CLEAN。查出来的每一条都要**改数据**，
# 不许改这个脚本的判定去凑绿灯。
# ==========================================================================*/
use strict;
use warnings;
use utf8;
use FindBin;
use lib $FindBin::Bin;
use JsBalance qw(scan_js);

binmode(STDOUT, ':encoding(UTF-8)');
binmode(STDERR, ':encoding(UTF-8)');

my $target = shift // 'visuals';

my @files;
if (-d $target) {
    opendir(my $d, $target) or die "打不开目录 $target: $!\n";
    @files = map { "$target/$_" } sort grep { /\.html$/ && $_ ne 'index.html' } readdir($d);
    closedir $d;
} elsif (-f $target) {
    @files = ($target);
} else {
    die "既不是目录也不是文件：$target\n";
}

my @bad;
my $grand_steps = 0;

for my $f (@files) {
    local $/;
    open my $fh, '<:encoding(UTF-8)', $f or do { push @bad, "$f: 打不开 $!"; next };
    my $src = <$fh>;
    close $fh;

    unless ($src =~ /window\.STEPS\s*=\s*\[/) {
        printf "%-32s (跳过：没有 window.STEPS)\n", basename($f);
        next;
    }

    # ── 7. 括号配平：整段内联脚本必须能被 JS 解析器接受 ──────────────────────
    # step 字段齐不齐是「内容」问题，这一项查的是「这页还是不是合法的 JS」。
    # 悬空的一个 `},` 就能让 window.STEPS 整段解析失败，visual.js 拿到的不是数组，
    # 页面直接空白 —— 而下面按正则数 step 的判定完全看不见它（踩过，7 个页面一起中招）。
    my ($block, $block_start) = ('', 0);
    while ($src =~ /<script\b[^>]*>(.*?)<\/script>/gs) {
        my $mstart = $-[0];
        my $body   = $1;
        next unless $body =~ /window\.STEPS/;
        $block = $body;
        $block_start = (substr($src, 0, $mstart) =~ tr/\n//);
        last;
    }
    if ($block eq '') {
        push @bad, "$f: 找到 window.STEPS 却没抠出它所在的 <script> 块";
    } else {
        for my $e (scan_js($block)) {
            push @bad, sprintf("%s:%d 内联脚本括号不配平：%s", $f, $block_start + $e->{line}, $e->{msg});
        }
    }

    # 上面那个 while(/gs) 会把 pos($src) 留在末尾；不重置的话，下面按 /gs 抠 step
    # 会从「文档结尾」开始匹配，于是「一个 step 都没解析出来」——假红灯（踩过）。
    pos($src) = 0;

    # 一个 step 以「两个空格 + { + 换行 + title:」开头，到下一个同级别的 { 或数组收尾为止。
    # 这个边界跟 visual.css/visual.js 的实际书写格式绑定，改格式时记得同步这里。
    my @chunks = $src =~ /\{\s*\n\s*title:.*?(?=\n\s{2}\{|\n\];)/gs;
    unless (@chunks) {
        push @bad, "$f: 有 window.STEPS 却没解析出任何 step（格式被动过？）";
        next;
    }

    my $i = 0;
    for my $c (@chunks) {
        my ($title) = $c =~ /title:\s*'([^']*)'/;
        $title //= '(无题)';
        my $short = length($title) > 22 ? substr($title, 0, 22) . '…' : $title;

        # ── 2. 七个字段齐不齐 ────────────────────────────────────────────────
        for my $field (qw(title nav ms stage caption code highlight)) {
            push @bad, sprintf("%s step%d 「%s」缺字段 %s", basename($f), $i, $short, $field)
              unless $c =~ /\b\Q$field\E:/;
        }

        my ($code) = $c =~ /code:\s*`(.*?)`/s;
        unless (defined $code) { $i++; next; }
        my @lines = split /\n/, $code, -1;

        # ── 5. 模板字符串里的雷 ──────────────────────────────────────────────
        push @bad, sprintf("%s step%d 「%s」code 里有 \${，会被 JS 当模板插值", basename($f), $i, $short)
          if $code =~ /\$\{/;
        push @bad, sprintf("%s step%d 「%s」stage 里有 \${", basename($f), $i, $short)
          if ($c =~ /stage:\s*`(.*?)`/s && $1 =~ /\$\{/);

        # ── 3+4. highlight 下标 ──────────────────────────────────────────────
        my ($hl) = $c =~ /highlight:\s*\[([^\]]*)\]/;
        unless (defined $hl) { $i++; next; }
        my @idx = grep { length } split /\s*,\s*/, ($hl =~ s/\s//gr);
        for my $h (@idx) {
            if ($h !~ /^\d+$/) {
                push @bad, sprintf("%s step%d 「%s」highlight 里有非数字 '%s'", basename($f), $i, $short, $h);
                next;
            }
            if ($h >= @lines) {
                push @bad, sprintf("%s step%d 「%s」highlight=%d 越界（code 共 %d 行，合法 0..%d）",
                                   basename($f), $i, $short, $h, scalar @lines, $#lines);
                next;
            }
            (my $l = $lines[$h]) =~ s/^\s+|\s+$//g;
            push @bad, sprintf("%s step%d 「%s」highlight=%d 落在空行", basename($f), $i, $short, $h)
              if $l eq '';
            push @bad, sprintf("%s step%d 「%s」highlight=%d 落在收尾括号 '%s' 上", basename($f), $i, $short, $h, $l)
              if $l =~ /^\}/;
        }
        $i++;
    }

    $grand_steps += scalar @chunks;
    printf "%-32s %d steps 已查\n", basename($f), scalar @chunks;
}

print "\n共 ", scalar @files, " 个页面、", $grand_steps, " 个 step\n";
if (@bad) {
    print "问题 ", scalar @bad, " 处：\n";
    print "  - $_\n" for @bad;
    exit 1;
}
print "ALL CLEAN\n";
exit 0;

sub basename {
    my ($p) = @_;
    $p =~ s{.*[\\/]}{};
    return $p;
}
