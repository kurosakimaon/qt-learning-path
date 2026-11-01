#!/usr/bin/perl
# ============================================================================
# _tools/check_html_js.pl —— 全仓 HTML 内联脚本 + *.js 的括号配平闸门
# ----------------------------------------------------------------------------
# 和 _tools/check_visual_steps.pl 的分工：
#   · check_visual_steps.pl 管「图解页面的数据」：step 齐不齐、highlight 下标对不对；
#   · 本脚本管「所有网页的 JS 还能不能被解析」：包括 visuals/visual.js、各阶段
#     index.html、学习路线图、build_deploy 页面 —— 范围更大，判定更粗（只配平）。
#
# 为什么必须有：本仓一次批量替换在 7 个图解页各留下一个悬空的 `},`。
# 后果是 `window.STEPS = [...]` 整段 SyntaxError，浏览器里**整页空白**，
# 而 Qt 侧的编译、ctest、引用审计全是绿的 —— 只有真去解析这段 JS 才能发现。
# 用 node --check 当然更准，但那要求读者装 Node；这里用 _tools/JsBalance.pm，
# 只要 Git Bash 自带 Perl 就能跑（和别的 _tools 脚本一样的门槛）。
#
# 用法：
#   perl _tools/check_html_js.pl              # 扫全仓
#   perl _tools/check_html_js.pl visuals      # 只扫某个目录
#
# 期望值：最后一行 ALL CLEAN。报出来的每一处都要改数据，不许放宽判定。
# ==========================================================================*/
use strict;
use warnings;
use utf8;
use File::Find;
use FindBin;
use lib $FindBin::Bin;
use JsBalance qw(scan_js);

binmode(STDOUT, ':encoding(UTF-8)');
binmode(STDERR, ':encoding(UTF-8)');

my $root = shift // '.';
my @files;

if (-f $root && $root =~ /\.(html|js)$/) {
    @files = ($root);
} else {
    find(sub {
        return if -d $_;
        return if $File::Find::dir =~ m{(^|/)(build|\.git|node_modules|runlogs)(/|$)};
        push @files, $File::Find::name if /\.html$/ || /\.js$/;
    }, $root);
    @files = sort @files;
}
die "没找到任何 .html / .js 文件（路径对不对？$root）\n" unless @files;

my @bad;
my ($checked_js, $checked_blocks) = (0, 0);

for my $f (@files) {
    local $/;
    open my $fh, '<:raw', $f or do { push @bad, "$f: 打不开 $!"; next };
    my $src = <$fh>;
    close $fh;
    $checked_js++;

    if ($f =~ /\.js$/) {
        $checked_blocks++;
        for my $e (scan_js($src)) {
            push @bad, sprintf("%s:%d 第 %d 列：%s", $f, $e->{line}, $e->{col}, $e->{msg});
        }
        next;
    }

    # 每个 <script> 块单独查；块外（HTML 属性里）的东西不归它管。
    my $idx = 0;
    while ($src =~ /<script\b([^>]*)>(.*?)<\/script>/gs) {
        # $-[0] 必须第一时间取：后面任何一次 // 都会覆盖 @-（踩过，行号全算成 3）。
        my $mstart = $-[0];
        my ($attr, $body) = ($1, $2);
        $idx++;
        next if $attr =~ /\bsrc\s*=/;          # 外链脚本，本地没有内容可查
        next if $body !~ /\S/;                 # 空块
        $checked_blocks++;
        my $base = (substr($src, 0, $mstart) =~ tr/\n//);    # 块开始之前已经走完的行数
        for my $e (scan_js($body)) {
            push @bad, sprintf("%s:%d（内联脚本 #%d）第 %d 列：%s",
                               $f, $base + $e->{line}, $idx, $e->{col}, $e->{msg});
        }
    }
}

printf "扫描 %d 个文件（%d 段脚本）\n", $checked_js, $checked_blocks;
if (@bad) {
    print "问题 ", scalar @bad, " 处：\n";
    print "  - $_\n" for @bad;
    exit 1;
}
print "ALL CLEAN\n";
exit 0;
