/* ============================================================================
 * _tools/svg_overflow_check.js —— visuals 页面的「文字捅出方框」体检
 * ----------------------------------------------------------------------------
 * 为什么需要它：
 *   visuals/*.html 里每张图都是**手写的绝对坐标** SVG：`<rect>` 定死宽高，
 *   `<text>` 自己给 x/y。改一个字、把一个词换成长一点的英文，文字就会捅出方框 ——
 *   而这种错误 `_tools/check_syntax.sh`（只管 .cpp）和 `ctest`（只管程序）都抓不到，
 *   肉眼看一页也容易漏（要翻 16 页 × 每页 6~7 步）。
 *
 * 用法（两条都行）：
 *   1) 单页：浏览器打开某个 visuals/<名字>.html，F12 → Console，把本文件**整段**粘进去回车。
 *      它自己逐步切、自己量、自己打印汇总。
 *   2) 全仓：bash _tools/check_svg_overflow.sh —— 无头浏览器开一个汇总页，
 *      把本文件当 <script src> 引进来，对 16 页 × 每一步跑同一个 runDeck()，
 *      所以**两条路径用的是同一套判据**，不存在「手工查过了、脚本没查」。
 *
 * 它做什么：
 *   对每一步渲染出来的 SVG，用 getBBox() 量每个 <text> 的**真实渲染包围盒**，
 *   再找「把这段文字中心点包在里面的最小 <rect>」当容器，四条边各算一个超出量。
 *   分两档：**出界**（有任何一条边越过容器边界，正数=越了几像素）→ 判红；
 *   **贴边**（没越界，但离边不到 3px）→ 只提示，不判红，因为换机器就该变出界了。
 *   每条都附容器 rect 的 x,y,w,h —— 改哪个数直接看这条。
 *
 * 四个坑（都是实测踩出来的，别再改回去）：
 *   1) 必须**按步渲染完再量**：页面一次只往 DOM 里放一步的 SVG，未渲染的步骤量不到。
 *      所以循环里要等一小段，让 visual.js 把新的 stage 换进去。
 *   2) 【勘误·原来这里就是错的】切步**不要**用 `location.hash = '#step-i'` 从 0 数起：
 *      visual.js 的地址栏是 **1 基**的（`canonical = '#step-' + (current + 1)`，
 *      hashchange 里再 `Number(match[1]) - 1` 减回来）。旧写法第 0 步会被夹紧成第 1 步，
 *      于是「第 1 步量两遍、每页最后一步从没量过」。现在改成点页面上真实的导航按钮
 *      （data-step 是 0 基），量 n 次就是 n 次，也和地址栏格式彻底解耦。
 *   3) 找容器用「**中心点**在不在 rect 里」，不能用左上角：箭头上的说明文字多是
 *      text-anchor="middle"，左上角常常压着隔壁的框，用左上角会挑错容器 → 假红
 *      （旧版就是这么冤枉了 moc_signal_slot / delegate_lifecycle 的 3 条）。
 *   4) 量之前先 `animation:none; transition:none`：go() 每次都给 #stage 加 .enter 动画，
 *      无头浏览器的 --virtual-time-budget 会把动画冻在半路，量到的是「动到一半」的坐标。
 *      （getBBox() 本身**会**算上 text-anchor 的居中偏移，这点不用担心；但本仓 SVG
 *      没用 transform，别引入 —— transform 不参与 getBBox，得改用 getBoundingClientRect。）
 *
 * 期望值：**出界 0 条**。查出出界时**改文案或改 rect，别调 TOLERANCE** ——
 *   它一旦调大，真出界也会被判成「贴边」甚至不报，等于把这条体检关掉。
 *   TOLERANCE 现在的用途只剩一个：定义「贴边」这条提示线（离边不到 3px）。
 * ==========================================================================*/
(function (global) {
  'use strict';

  const STEP_GAP_MS = 320;   // 等 visual.js 把新一步的 SVG 换进 DOM
  const TOLERANCE = 3;       // px：离边不到这么多就算「贴边」（只提示）；真出界一律判红

  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function noAnimations(doc) {
    if (doc.getElementById('__svgMeasureNoAnim')) return;
    const st = doc.createElement('style');
    st.id = '__svgMeasureNoAnim';
    st.textContent = '*,*::before,*::after{animation:none!important;transition:none!important}';
    doc.head.appendChild(st);
  }

  function num(v) { return +v || 0; }

  // 文字中心点落在哪个 rect 里 → 取面积最小的那个当容器；一个都没有就退化成整张画布
  function findHost(rects, vb, bb) {
    const cx = bb.x + bb.width / 2;
    const cy = bb.y + bb.height / 2;
    let host = null;
    for (const r of rects) {
      if (cx >= r.x && cx <= r.x + r.w && cy >= r.y && cy <= r.y + r.h) {
        if (!host || r.w * r.h < host.w * host.h) host = r;
      }
    }
    return { host, limitL: host ? host.x : 0, limitT: host ? host.y : 0,
             limitR: host ? host.x + host.w : vb.width,
             limitB: host ? host.y + host.h : vb.height };
  }

  /** 量一张 SVG，返回记录数组：{step, kind, cls, oL, oT, oR, oB, host, text} */
  function measureSvg(svg, stepNo) {
    const vb = svg.viewBox.baseVal;
    const rects = [...svg.querySelectorAll('rect')].map((r) => ({
      x: num(r.getAttribute('x')), y: num(r.getAttribute('y')),
      w: num(r.getAttribute('width')), h: num(r.getAttribute('height')),
    }));
    const out = [];
    for (const t of svg.querySelectorAll('text')) {
      const bb = t.getBBox();
      if (!bb.width && !bb.height) continue;
      const { host, limitL, limitT, limitR, limitB } = findHost(rects, vb, bb);
      // 四条边都量：以前只管右/下，漏了**居中或右对齐的文字往左捅**（text-anchor 一翻车
      // 就是左边先出界），也没量「文字顶到框上沿」。多量两条边不会多报错，只会多抓真 bug。
      const oL = Math.round(limitL - bb.x);
      const oT = Math.round(limitT - bb.y);
      const oR = Math.round(bb.x + bb.width - limitR);
      const oB = Math.round(bb.y + bb.height - limitB);
      const worst = Math.max(oL, oT, oR, oB);   // 越大越糟：正数=出界，负数=还剩这么多 px
      if (worst <= -TOLERANCE) continue;
      out.push({
        step: stepNo,
        // over = 真出界（ gate 判红的就是它）；tight = 没出界但离边不到 3px，
        // 换个字体/换个 DPI 就出界 —— 只提示，不算失败。别为了凑 0 把 tight 也判红，
        // 那样大家只会去调 TOLERANCE，最后连真出界也查不出来。
        kind: worst > 0 ? 'over' : 'tight',
        cls: t.getAttribute('class') || '-',
        oL, oT, oR, oB,
        host: host ? [host.x, host.y, host.w, host.h].join(',') : 'canvas',
        text: (t.textContent || '').trim().slice(0, 40),
      });
    }
    return out;
  }

  /** 对一整个图解页（window 对象）跑一遍所有步骤，返回 { steps, findings, errors } */
  async function runDeck(win, gap) {
    const doc = win.document;
    const total = Array.isArray(win.STEPS) ? win.STEPS.length : 0;
    if (!total) return { steps: 0, findings: [], errors: ['没有 window.STEPS：这不是图解页'] };
    // 点导航按钮 = 走用户真正走的那条路；导航条的序号是 0 基，不存在 1 基/0 基搞混
    const buttons = [...doc.querySelectorAll('#steps button')];
    if (buttons.length !== total) {
      return { steps: 0, findings: [], errors: [`导航按钮 ${buttons.length} 个，STEPS ${total} 步，对不上`] };
    }
    noAnimations(doc);
    const findings = [];
    const errors = [];
    for (let i = 0; i < total; i++) {
      buttons[i].click();
      await sleep(gap || STEP_GAP_MS);
      const svg = doc.querySelector('#stage svg') || doc.querySelector('svg');
      if (!svg) { errors.push(`第 ${i + 1} 步：DOM 里没有 <svg>（渲染没跟上？把间隔调大）`); continue; }
      measureSvg(svg, i + 1).forEach((f) => findings.push(f));
    }
    return { steps: total, findings, errors };
  }

  global.SvgOverflow = { runDeck, measureSvg, noAnimations, TOLERANCE, STEP_GAP_MS };

  // 直接粘进某一页的 Console 时自动跑一遍；被汇总页当库引进来时无 STEPS，不会自己触发。
  if (Array.isArray(global.STEPS)) {
    (async function consoleRun() {
      const r = await runDeck(global);
      const page = location.pathname.split('/').pop();
      const nOver = r.findings.filter((f) => f.kind === 'over').length;
      const nTight = r.findings.length - nOver;
      console.log(`\n=== ${page}：${r.steps} 步，出界 ${nOver} 处，贴边 ${nTight} 处 ===`);
      r.findings.forEach((f) => console.log(
        `  ${f.kind === 'over' ? '✗ 出界' : '· 贴边'} 第 ${f.step} 步 [${f.cls}] ` +
        `左${f.oL} 上${f.oT} 右${f.oR} 下${f.oB} 容器 rect=${f.host} —— 「${f.text}」`));
      r.errors.forEach((e) => console.warn('  ! ' + e));
      if (!r.findings.length && !r.errors.length) console.log('  ALL CLEAN');
      return nOver;
    })();
  }
})(typeof window !== 'undefined' ? window : globalThis);
