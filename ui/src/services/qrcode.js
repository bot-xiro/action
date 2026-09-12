// 纯 JS 二维码编码器 (无第三方依赖, QuickJS 兼容)
//
// 范围: byte 模式, EC level L, 版本 1-6 自适应 (最大 134 字节).
// 覆盖 B 站扫码登录 url (~110 字符). 掩码固定 mask 2 (解码器按
// format info 还原, 任意合法掩码均可扫描; 屏幕渲染的码面干净, L 级纠错足够).
//
// 输出: {size, rows} — rows[r][c] 为 true=暗模块. 渲染端按行合并暗色游程,
// 减少节点数 (笔端 960x266 屏, 模块 5px, 41x41 约 450 个游程节点).
//
// 实现依据 QR ISO/IEC 18004: 寻像图形/校正图形/时序图形 + BCH(15,5) format
// info + 数据 zigzag 放置 + GF(256) Reed-Solomon 纠错 (多项式 0x11d).

// 版本参数 (EC L): [RS 块数, 每块总码字, 每块数据码字]
var RS_L = [
  [1, 26, 19],   // v1 (17 字节)
  [1, 44, 34],   // v2 (32)
  [1, 70, 55],   // v3 (53)
  [1, 100, 80],  // v4 (78)
  [1, 134, 108], // v5 (106)
  [2, 86, 68]    // v6 (134)
]

// 校正图形中心坐标列表 (按版本 v1-v6). 每个版本给出所有候选中心,
// 两两组合出校正图形位置, 与寻像图形重叠的角落由生成逻辑跳过.
// 依据 ISO/IEC 18004 Table E.1: v2=[6,18] v3=[6,22] v4=[6,26] v5=[6,30] v6=[6,34]
var ALIGN_CENTERS = [
  null,           // v1: 无校正图形
  [6, 18],        // v2
  [6, 22],        // v3
  [6, 26],        // v4
  [6, 30],        // v5
  [6, 34]         // v6
]

var FORMAT_EC_L_BITS = 1  // 01 (L)
var MASK_PATTERN = 2      // 固定掩码 2: c % 3 == 0 取反

// GF(256) 指数/对数表 (本原多项式 0x11d)
var EXP = new Array(256)
var LOG = new Array(256)
;(function () {
  var x = 1
  for (var i = 0; i < 255; i++) {
    EXP[i] = x
    LOG[x] = i
    x = x << 1
    if (x >= 256) x ^= 0x11d
  }
  EXP[255] = EXP[0]
})()

function gmul(a, b) {
  if (a === 0 || b === 0) return 0
  return EXP[(LOG[a] + LOG[b]) % 255]
}

// RS 纠错码字 (生成多项式次数 = ecCount)
// 生成多项式 g(x) = ∏_{i=0}^{ecCount-1} (x - α^i), 系数按「最高次在前」存放:
//   gen[0] 为 x^ecCount 的系数 (恒为 1), gen[ecCount] 为常数项.
// 每步乘 (x - α^i): 乘 x 使次数+1 (结果下标不变), 乘 α^i 使下标+1.
function rsEc(data, ecCount) {
  var gen = [1]
  for (var i = 0; i < ecCount; i++) {
    var next = new Array(gen.length + 1)
    for (var j = 0; j < next.length; j++) next[j] = 0
    for (var j = 0; j < gen.length; j++) {
      next[j] ^= gen[j]                       // gen[j] * x
      next[j + 1] ^= gmul(gen[j], EXP[i])     // gen[j] * α^i
    }
    gen = next
  }
  var rem = new Array(ecCount)
  for (var i = 0; i < ecCount; i++) rem[i] = 0
  for (var i = 0; i < data.length; i++) {
    var factor = data[i] ^ rem[0]
    rem.shift()
    rem.push(0)
    if (factor !== 0) {
      for (var j = 0; j < ecCount; j++) rem[j] ^= gmul(gen[j + 1], factor)
    }
  }
  return rem
}

// BCH(15,5) format info (生成多项式 0x537, 掩码 0x5412)
function bchFormatInfo(data5) {
  var d = data5 << 10
  function digit(v) {
    var n = 0
    while (v !== 0) { n++; v >>>= 1 }
    return n
  }
  var G15 = 0x537
  while (digit(d) - digit(G15) >= 0) {
    d ^= G15 << (digit(d) - digit(G15))
  }
  return ((data5 << 10) | d) ^ 0x5412
}

function maskFn(pattern, r, c) {
  switch (pattern) {
    case 0: return (r + c) % 2 === 0
    case 1: return r % 2 === 0
    case 2: return c % 3 === 0
    case 3: return (r + c) % 3 === 0
    case 4: return (Math.floor(r / 2) + Math.floor(c / 3)) % 2 === 0
    case 5: return ((r * c) % 2) + ((r * c) % 3) === 0
    case 6: return (((r * c) % 2) + ((r * c) % 3)) % 2 === 0
    default: return ((r * c) % 3 + (r + c) % 2) % 2 === 0
  }
}

/**
 * 生成二维码矩阵.
 * @param {string} text 内容 (UTF-8, ≤134 字节)
 * @returns {{size:number, rows:Array<Array<boolean>>}}
 */
export function makeQR(text) {
  var bytes = toUtf8(String(text))
  // 选版本 (EC L 总数据码字 = 块数 x 每块数据码字, 容量 = 总数据码字 - 2)
  // 注意: v4+ 为多块, 容量必须按块数 x 每块数据码字算 (v6-L: 2x68-2=134)
  var version = 0
  for (var v = 1; v <= 6; v++) {
    var blk = RS_L[v - 1]
    var cap = blk[0] * blk[2] - 2
    if (bytes.length <= cap) { version = v; break }
  }
  if (version === 0) throw new Error('内容过长, 无法生成二维码 (' + bytes.length + ' 字节)')

  var rs = RS_L[version - 1]
  var blockCount = rs[0], totalCount = rs[1], dataCount = rs[2]
  var ecCount = totalCount - dataCount
  var moduleCount = 4 * version + 17

  // ---- 编码数据 (byte mode 0100 + 8bit 计数 + 数据 + 终止符 + 补齐) ----
  var totalDataCodewords = blockCount * dataCount
  var bits = []
  function pushBits(val, len) {
    for (var i = len - 1; i >= 0; i--) bits.push((val >>> i) & 1)
  }
  pushBits(4, 4)                       // byte mode
  pushBits(bytes.length, 8)            // 字符计数 (v1-9 为 8bit)
  for (var i = 0; i < bytes.length; i++) pushBits(bytes[i], 8)
  var capBits = totalDataCodewords * 8
  if (bits.length > capBits - 4) throw new Error('内容过长')
  pushBits(0, Math.min(4, capBits - bits.length))          // 终止符 (≤4bit)
  while (bits.length % 8 !== 0) bits.push(0)               // 对齐字节
  var padBytes = [0xEC, 0x11]
  for (var pi = 0; bits.length < capBits; pi++) pushBits(padBytes[pi % 2], 8)

  // 码字
  var dataCw = []
  for (var i = 0; i < bits.length; i += 8) {
    var b = 0
    for (var j = 0; j < 8; j++) b = (b << 1) | bits[i + j]
    dataCw.push(b)
  }
  // 多块时分块交织 (v1-5 单块, v6-L 2 块)
  var dataBlocks = []
  var ecBlocks = []
  for (var bi = 0; bi < blockCount; bi++) {
    var blk = dataCw.slice(bi * dataCount, (bi + 1) * dataCount)
    dataBlocks.push(blk)
    ecBlocks.push(rsEc(blk, ecCount))
  }
  var finalCw = []
  for (var i = 0; i < dataCount; i++) {
    for (var bi = 0; bi < blockCount; bi++) finalCw.push(dataBlocks[bi][i])
  }
  for (var i = 0; i < ecCount; i++) {
    for (var bi = 0; bi < blockCount; bi++) finalCw.push(ecBlocks[bi][i])
  }

  // ---- 模块矩阵 ----
  var modules = []
  for (var r = 0; r < moduleCount; r++) {
    var row = []
    for (var c = 0; c < moduleCount; c++) row.push(null)
    modules.push(row)
  }

  function probePattern(row, col) {
    for (var r = -1; r <= 7; r++) {
      for (var c = -1; c <= 7; c++) {
        var rr = row + r, cc = col + c
        if (rr < 0 || rr >= moduleCount || cc < 0 || cc >= moduleCount) continue
        modules[rr][cc] =
          (r >= 0 && r <= 6 && (c === 0 || c === 6)) ||
          (c >= 0 && c <= 6 && (r === 0 || r === 6)) ||
          (r >= 2 && r <= 4 && c >= 2 && c <= 4)
      }
    }
  }

  probePattern(0, 0)
  probePattern(moduleCount - 7, 0)
  probePattern(0, moduleCount - 7)

  // 时序图形
  for (var i = 8; i < moduleCount - 8; i++) {
    if (modules[6][i] === null) modules[6][i] = i % 2 === 0
    if (modules[i][6] === null) modules[i][6] = i % 2 === 0
  }

  // 校正图形 (v2-v6): 标准中心坐标组合, 与寻像图形重叠的位置跳过.
  // 注意: 同类图形必须「全部」画出 —— 只画一个会让解码器无法校正网格,
  // 表现为二维码肉眼正常但扫不出来 (真机 + OpenCV 实测).
  var acCenters = ALIGN_CENTERS[version - 1]
  if (acCenters) {
    for (var ai = 0; ai < acCenters.length; ai++) {
      for (var aj = 0; aj < acCenters.length; aj++) {
        var ar = acCenters[ai], acc = acCenters[aj]
        // 跳过与三个寻像图形重叠的角落
        var nearTL = (ar <= 8 && acc <= 8)
        var nearTR = (ar <= 8 && acc >= moduleCount - 9)
        var nearBL = (ar >= moduleCount - 9 && acc <= 8)
        if (nearTL || nearTR || nearBL) continue
        for (var r2 = -2; r2 <= 2; r2++) {
          for (var c2 = -2; c2 <= 2; c2++) {
            modules[ar + r2][acc + c2] =
              Math.abs(r2) === 2 || Math.abs(c2) === 2 || (r2 === 0 && c2 === 0)
          }
        }
      }
    }
  }

  // 固定暗模块
  modules[moduleCount - 8][8] = true

  // format info (两份). 位序按标准: fmt bit14..bit0 依次写入下列坐标.
  var fmt = bchFormatInfo((FORMAT_EC_L_BITS << 3) | MASK_PATTERN)
  // 第一份: 沿左上寻像图形两侧的 L 形 (bit14 -> bit0)
  var FORMAT_POS_A = [
    [8, 0], [8, 1], [8, 2], [8, 3], [8, 4], [8, 5], [8, 7], [8, 8],
    [7, 8], [5, 8], [4, 8], [3, 8], [2, 8], [1, 8], [0, 8]
  ]
  for (var i = 0; i < 15; i++) {
    modules[FORMAT_POS_A[i][0]][FORMAT_POS_A[i][1]] = ((fmt >> (14 - i)) & 1) === 1
  }
  // 第二份: 右上横条 + 左下竖条.
  // 标准位序: (8,n-1) 放 bit0, 向左递增到 (8,n-8) 放 bit7;
  //           (n-7,8) 放 bit8, 向下递增到 (n-1,8) 放 bit14.
  for (var i = 0; i < 8; i++) {
    modules[8][moduleCount - 1 - i] = ((fmt >> i) & 1) === 1
  }
  for (var i = 0; i < 7; i++) {
    modules[moduleCount - 7 + i][8] = ((fmt >> (8 + i)) & 1) === 1
  }

  // ---- 数据放置 (zigzag, 自右下角, 跳过第 6 列) ----
  var inc = -1
  var row = moduleCount - 1
  var bitIndex = 7
  var byteIndex = 0
  for (var col = moduleCount - 1; col > 0; col -= 2) {
    if (col === 6) col--
    while (true) {
      for (var c = 0; c < 2; c++) {
        if (modules[row][col - c] === null) {
          var dark = false
          if (byteIndex < finalCw.length) {
            dark = ((finalCw[byteIndex] >>> bitIndex) & 1) === 1
          }
          if (maskFn(MASK_PATTERN, row, col - c)) dark = !dark
          modules[row][col - c] = dark
          bitIndex--
          if (bitIndex === -1) { byteIndex++; bitIndex = 7 }
        }
      }
      row += inc
      if (row < 0 || row >= moduleCount) { row -= inc; inc = -inc; break }
    }
  }

  return { size: moduleCount, rows: modules }
}

// UTF-8 编码 (含代理对)
function toUtf8(str) {
  var out = []
  for (var i = 0; i < str.length; i++) {
    var c = str.charCodeAt(i)
    if (c >= 0xd800 && c <= 0xdbff && i + 1 < str.length) {
      var c2 = str.charCodeAt(i + 1)
      if (c2 >= 0xdc00 && c2 <= 0xdfff) { c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00); i++ }
    }
    if (c < 0x80) out.push(c)
    else if (c < 0x800) { out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f)) }
    else if (c < 0x10000) { out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)) }
    else { out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 0x3f), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f)) }
  }
  return out
}
