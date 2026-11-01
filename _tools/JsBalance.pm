package JsBalance;
# ============================================================================
# _tools/JsBalance.pm —— 一段 JS 文本的「括号配平」扫描器（只读，无依赖）
# ----------------------------------------------------------------------------
# 为什么不用正则数括号：JS 里的 `{` 可以活在字符串、模板字符串、注释和正则字面量里，
# 直接数必然误报；而模板字符串还能用 `${ ... }` 把代码再套回来。所以这里做的是一个
# 够用的「词法走一遍」：注释 / '...' / "..." / `...` / /.../ 五类片段整段吞掉，
# 只有真正的代码括号才进栈。
#
# 为什么值得为这个仓写它：visuals/*.html 的每一页都是手写的 `window.STEPS = [ {...} ]`。
# 少一个逗号、多一个 `},`、反引号没闭合 —— 浏览器给出的后果是**整页空白**，
# 而按正则数 step 的检查会照样报「结构完好」。本仓真就这么坏过 7 页。
#
# 用法：
#   use FindBin; use lib $FindBin::Bin; use JsBalance qw(scan_js);
#   my @errs = scan_js($js_text);        # ('' => { line, col, msg })
#   # line/col 是**这段文本内部**的 1 基行列；调用方负责换算成文件行号。
#
# 它不校验语法（不查逗号、不查关键字）：那些交给浏览器。它只保证一件事 ——
# **括号是一对一对套好的**，从而「页面是不是被改坏了」这件事有个机械答案。
# ==========================================================================*/
use strict;
use warnings;
use utf8;
use Exporter 'import';

our @EXPORT_OK = qw(scan_js);

my %CLOSE = ('}' => '{', ']' => '[', ')' => '(');

sub scan_js {
    my ($src) = @_;
    $src //= '';
    my $s = { src => $src, n => length($src), i => 0, line => 1, col => 1,
             err => [], stack => [], prev => '' };
    scan_code($s);
    if (@{ $s->{stack} }) {
        my ($ch, $l, $c) = @{ $s->{stack}[-1] };
        push @{ $s->{err} }, { line => $l, col => $c,
            msg => sprintf("开括号 '%s'（第 %d 行第 %d 列）一直到文本结尾都没闭合",
                           $ch eq '${' ? '${' : $ch, $l, $c) };
    }
    return @{ $s->{err} };
}

# ── 位置推进 ─────────────────────────────────────────────────────────────────
sub take {
    my ($s, $len) = @_;
    $len ||= 1;
    my $txt = substr($s->{src}, $s->{i}, $len);
    my $nl  = ($txt =~ tr/\n//);
    if ($nl) { $s->{line} += $nl; $s->{col} = length($txt) - rindex($txt, "\n"); }
    else     { $s->{col} += length $txt; }
    $s->{i} += length $txt;
    return $txt;
}

sub here { my ($s) = @_; return { line => $s->{line}, col => $s->{col} } }

sub fail {
    my ($s, $pos, $msg) = @_;
    push @{ $s->{err} }, { line => $pos->{line}, col => $pos->{col}, msg => $msg };
    return;
}

# ── 代码模式 ─────────────────────────────────────────────────────────────────
# 遇到插值收尾的 `}` 时返回（栈顶是 '${'）；其余情况走到文本结尾。
sub scan_code {
    my ($s) = @_;
    while ($s->{i} < $s->{n}) {
        my $c  = substr($s->{src}, $s->{i}, 1);
        my $nx = substr($s->{src}, $s->{i} + 1, 1) // '';
        my $pos = here($s);

        # 注释
        if ($c eq '/' && $nx eq '/') {
            my $p = index($s->{src}, "\n", $s->{i});
            $p = $s->{n} if $p < 0;
            take($s, $p - $s->{i});
            next;
        }
        if ($c eq '/' && $nx eq '*') {
            my $p = index($s->{src}, '*/', $s->{i} + 2);
            if ($p < 0) { fail($s, $pos, '块注释没有收尾的 */'); return }
            take($s, $p + 2 - $s->{i});
            next;
        }

        # 普通字符串：不许跨行
        if ($c eq "'" || $c eq '"') {
            my $j = $s->{i} + 1;
            my $closed = 0;
            while ($j < $s->{n}) {
                my $d = substr($s->{src}, $j, 1);
                if    ($d eq "\\") { $j += 2; next }
                if    ($d eq $c)   { $closed = 1; $j++; last }
                last if $d eq "\n";
                $j++;
            }
            unless ($closed) {
                fail($s, $pos, sprintf("引号 %s 没闭合（或字符串跨了行）", $c));
                take($s, $s->{n} - $s->{i});
                return;
            }
            take($s, $j - $s->{i});
            $s->{prev} = $c;
            next;
        }

        # 模板字符串
        if ($c eq '`') {
            enter_template($s);
            $s->{prev} = '`';
            next;
        }

        # 正则字面量：只有「上一个有效字符不可能是操作数结尾」时，/ 才是正则
        if ($c eq '/' && regex_allowed($s)) {
            my $p = $s->{i};
            my $in_class = 0;
            my $closed = 0;
            $p++;
            while ($p < $s->{n}) {
                my $d = substr($s->{src}, $p, 1);
                last if $d eq "\n";
                if ($d eq "\\") { $p += 2; next }
                if ($d eq '[')  { $in_class = 1; $p++; next }
                if ($d eq ']')  { $in_class = 0; $p++; next }
                if ($d eq '/' && !$in_class) { $closed = 1; $p++; last }
                $p++;
            }
            unless ($closed) {
                fail($s, $pos, '正则字面量的 / 没闭合（跨行了？）');
                take($s, $s->{n} - $s->{i});
                return;
            }
            # 尾部 flag
            $p++ while $p < $s->{n} && substr($s->{src}, $p, 1) =~ /^[a-z]$/;
            take($s, $p - $s->{i});
            $s->{prev} = '/';
            next;
        }

        # 括号
        if ($c eq '{' || $c eq '[' || $c eq '(') {
            push @{ $s->{stack} }, [ $c, $pos->{line}, $pos->{col} ];
            take($s);
            $s->{prev} = $c;
            next;
        }
        if ($CLOSE{$c}) {
            my $want = $CLOSE{$c};
            unless (@{ $s->{stack} }) {
                fail($s, $pos, sprintf("'%s' 在这里没有对应的开括号（多半是上一处改动留下的悬空闭合）", $c));
                take($s);
                next;
            }
            my $top = $s->{stack}[-1];
            if ($top->[0] eq '${' && $c eq '}') {
                pop @{ $s->{stack} };
                take($s);
                $s->{prev} = $c;
                return;                       # 交回给模板字符串继续吃静态部分
            }
            if ($top->[0] ne $want) {
                fail($s, $pos, sprintf("这里闭合了 '%s'，但最近没闭合的开括号是 '%s'（第 %d 行第 %d 列）",
                                       $c, $top->[0], $top->[1], $top->[2]));
            } else {
                pop @{ $s->{stack} };
            }
            take($s);
            $s->{prev} = $c;
            next;
        }

        if ($c =~ /\w/) {
            # 标识符/数字整段吞掉，顺手记住词，用来判 return / typeof 后面的正则
            my $j = $s->{i};
            $j++ while $j < $s->{n} && substr($s->{src}, $j, 1) =~ /^[\w\$]/;
            my $word = substr($s->{src}, $s->{i}, $j - $s->{i});
            take($s, $j - $s->{i});
            $s->{prev}     = substr($word, -1);
            $s->{prevword} = $word;
            next;
        }

        take($s);
        $s->{prev} = $c unless $c =~ /\s/;
    }
    return;
}

# 模板字符串：从当前反引号开始，遇到 ${ 就递归进代码模式。
sub enter_template {
    my ($s) = @_;
    my $start = here($s);
    take($s);                                  # 吃掉开头的 `
    while ($s->{i} < $s->{n}) {
        my $c = substr($s->{src}, $s->{i}, 1);
        if ($c eq "\\") { take($s, 2); next }
        if ($c eq '`')  { take($s); return }
        if ($c eq '$' && substr($s->{src}, $s->{i} + 1, 1) eq '{') {
            my $pos = here($s);
            take($s, 2);
            push @{ $s->{stack} }, [ '${', $pos->{line}, $pos->{col} ];
            scan_code($s);                     # 收尾的 } 由 scan_code 消费并返回
            next;
        }
        take($s);
    }
    fail($s, $start, '模板字符串的反引号 ` 没闭合');
    return;
}

# `/` 到底是除法还是正则？看上一个有效字符：能当作操作数结尾的，只能是除法。
sub regex_allowed {
    my ($s) = @_;
    my $p = $s->{prev};
    return 1 if $p eq '';
    my $val_end = ')]' . chr(96) . q{'} . q{'"};      # ) ] ` ' " —— 都是「值已经写完」
    if ($p =~ /^\w/ || $p eq '$') {
        my $w = $s->{prevword} // '';
        return 1 if $w =~ /^(?:return|typeof|instanceof|in|of|new|delete|void|case|do|else|yield|await)$/;
        return 0;                                     # `a / b`：变量后面是除法
    }
    return 0 if index($val_end, $p) >= 0;
    return 0 if $p eq '}';       # 块/对象字面量结尾，后面多半是运算符，保守按除法
    return 1;                    # 运算符、逗号、( [ { = 后面才是正则
}

1;
