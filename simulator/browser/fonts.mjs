// SPDX-License-Identifier: MIT
// Rasterize with the browser's installed fonts; no downloadable CJK font or
// per-frame DOM work. The cache holds at most 2 MiB of glyph alpha coverage.
export function installBrowserFonts(host, Canvas = globalThis.OffscreenCanvas) {
  if (!Canvas) return false;
  const canvas = new Canvas(1, 1);
  const ctx = canvas.getContext('2d', {willReadFrequently: true});
  if (!ctx) return false;
  const cache = new Map(); let bytes = 0;
  const font = size => `${size}px system-ui, "Noto Sans CJK SC", "Microsoft YaHei", sans-serif`;
  function glyph(size, code) {
    const key = size * 0x110000 + code;
    if (cache.has(key)) { const g=cache.get(key); cache.delete(key); cache.set(key,g); return g; }
    const text = String.fromCodePoint(code);
    ctx.font = font(size); ctx.textBaseline = 'alphabetic';
    const m = ctx.measureText(text);
    const left = Math.floor(-m.actualBoundingBoxLeft), right = Math.ceil(m.actualBoundingBoxRight);
    const ascent = Math.ceil(m.actualBoundingBoxAscent), descent = Math.ceil(m.actualBoundingBoxDescent);
    const w = Math.max(0,right-left), h = Math.max(0,ascent+descent);
    // Bound native allocations even for unexpected system-font metrics.
    if (w>1024 || h>1024) throw new RangeError('System font glyph is too large');
    const alpha = new Uint8Array(w*h);
    if (w && h) {
      canvas.width=w; canvas.height=h;
      ctx.font=font(size); ctx.textBaseline='alphabetic'; ctx.fillStyle='#fff';
      ctx.fillText(text,-left,ascent);
      const rgba=ctx.getImageData(0,0,w,h).data;
      for(let i=0;i<alpha.length;i++)alpha[i]=rgba[i*4+3];
    }
    const g={advance:Math.round(m.width),w,h,left,descent,alpha};
    while(cache.size && (bytes+alpha.length>2*1024*1024 || cache.size>=1024)) {
      const old=cache.keys().next().value; bytes-=cache.get(old).alpha.length; cache.delete(old);
    }
    cache.set(key,g);bytes+=alpha.length;return g;
  }
  host.browserFontMetrics=(size,code,p)=>{
    const g=glyph(size,code);
    host.HEAP32.set([g.advance,g.w,g.h,g.left,-g.descent],p>>>2);
    return 1;
  };
  host.browserFontBitmap=(size,code,p,stride)=>{
    const g=glyph(size,code);
    for(let y=0;y<g.h;y++)host.HEAPU8.set(g.alpha.subarray(y*g.w,(y+1)*g.w),p+y*stride);
  };
  return true;
}
