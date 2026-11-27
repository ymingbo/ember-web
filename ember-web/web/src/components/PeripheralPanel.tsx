import { useRef, useState, useEffect } from 'react';
import type { FloppyDriveState, PrinterPageInfo } from '../types/ember';

interface PeripheralPanelProps {
  // 软驱
  floppyDriveCount: number;
  floppyStates: FloppyDriveState[];
  /**
   * 各软驱马达的实时状态。指示灯要跟着读盘闪，所以读 ref 而不读 state：
   * 每帧改动 state 会触发整棵树重渲染，把模拟器拖慢。
   */
  floppyMotorRef: React.MutableRefObject<boolean[]>;
  onFloppyInsert: (index: number, file: File, writeProtected?: boolean) => Promise<boolean>;
  onFloppyEject: (index: number) => boolean;
  onFloppySave: (index: number) => Uint8Array | null;
  // 打印机
  hasPrinter: boolean;
  printerPages: number;
  onGetPrinterPage: () => PrinterPageInfo | null;
  // 磁带机
  hasTape: boolean;
  tapeLoaded: boolean;
  tapePlaying: boolean;
  onTapeLoad: (file: File) => Promise<boolean>;
  onTapeRewind: () => void;
}

/**
 * 软驱读写指示灯。
 * 真机的灯跟驱动器的马达同亮同灭（马达一转就在读盘），所以这里直接跟着
 * floppyMotorRef 的高低电平走。用 rAF 直改 DOM 的 className，不走 React 状态：
 * 灯每帧都可能变，走 setState 会让整个面板每帧重渲染。
 */
function FloppyLed({ motorRef, index }: { motorRef: React.MutableRefObject<boolean[]>; index: number }) {
  const ledRef = useRef<HTMLSpanElement>(null);

  useEffect(() => {
    let raf = 0;
    let lit = false;
    const tick = () => {
      const on = motorRef.current[index] === true;
      if (on !== lit) {
        lit = on;
        ledRef.current?.classList.toggle('led-on', on);
      }
      raf = requestAnimationFrame(tick);
    };
    raf = requestAnimationFrame(tick);
    return () => cancelAnimationFrame(raf);
  }, [motorRef, index]);

  return <span ref={ledRef} className="floppy-led" title="读盘指示" />;
}

/**
 * 外设控制面板：软驱、打印机、磁带机。
 * 仅在对应外设可用时显示对应区块。
 */
export function PeripheralPanel({
  floppyDriveCount,
  floppyStates,
  floppyMotorRef,
  onFloppyInsert,
  onFloppyEject,
  onFloppySave,
  hasPrinter,
  printerPages,
  onGetPrinterPage,
  hasTape,
  tapeLoaded,
  tapePlaying,
  onTapeLoad,
  onTapeRewind,
}: PeripheralPanelProps) {
  const fileInputRefs = useRef<(HTMLInputElement | null)[]>([]);
  const tapeInputRef = useRef<HTMLInputElement>(null);
  const [showPrinterView, setShowPrinterView] = useState(false);
  const printerCanvasRef = useRef<HTMLCanvasElement>(null);
  const [lastPageHash, setLastPageHash] = useState<number>(0);

  // 没有任何外设就不显示
  if (floppyDriveCount === 0 && !hasPrinter && !hasTape) {
    return null;
  }

  // ------------------------------------------------------------------
  // 软驱
  // ------------------------------------------------------------------
  const handleFloppyFileChange = (index: number, e: React.ChangeEvent<HTMLInputElement>) => {
    const file = e.target.files?.[0];
    if (file) {
      onFloppyInsert(index, file, false);
    }
    // 重置 input 以便重复选择同一文件
    e.target.value = '';
  };

  const handleSaveFloppy = (index: number) => {
    const data = onFloppySave(index);
    if (!data) return;
    const state = floppyStates[index];
    const name = state?.name || `disk_${index}.img`;
    // TS 5.7 起 Uint8Array<ArrayBufferLike> 不能直接当 BlobPart（可能是 SharedArrayBuffer），
    // 切一片独立的 ArrayBuffer 再包，语义不变（Blob 本来就要复制一份）。
    const buf = new Uint8Array(data.byteLength);
    buf.set(data);
    const blob = new Blob([buf.buffer as ArrayBuffer], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    a.click();
    URL.revokeObjectURL(url);
  };

  // ------------------------------------------------------------------
  // 打印机渲染
  // ------------------------------------------------------------------
  useEffect(() => {
    if (!showPrinterView || !printerCanvasRef.current || printerPages === 0) return;

    const page = onGetPrinterPage();
    if (!page?.data || page.width === 0 || page.height === 0) return;

    // 简单哈希判断是否同一页
    let hash = 0;
    for (let i = 0; i < Math.min(page.data.length, 1000); i++) {
      hash = ((hash << 5) - hash + page.data[i]) | 0;
    }
    if (hash === lastPageHash) return;
    setLastPageHash(hash);

    const canvas = printerCanvasRef.current;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    canvas.width = page.width;
    canvas.height = page.height;

    // 1 位位图渲染：每行按字节补齐，高位在左
    const bytesPerRow = Math.ceil(page.width / 8);
    const imgData = ctx.createImageData(page.width, page.height);

    for (let y = 0; y < page.height; y++) {
      const rowOffset = y * bytesPerRow;
      for (let x = 0; x < page.width; x++) {
        const byteIdx = rowOffset + Math.floor(x / 8);
        const bitIdx = 7 - (x % 8); // 高位在左
        const pixel = (page.data[byteIdx] >> bitIdx) & 1;
        const pxOffset = (y * page.width + x) * 4;
        // 白底黑字
        const color = pixel === 0 ? 255 : 0;
        imgData.data[pxOffset] = color;
        imgData.data[pxOffset + 1] = color;
        imgData.data[pxOffset + 2] = color;
        imgData.data[pxOffset + 3] = 255;
      }
    }

    ctx.putImageData(imgData, 0, 0);
  }, [showPrinterView, printerPages, onGetPrinterPage, lastPageHash]);

  /** 按钮基础样式在 app.css 里，这里只区分具体动作。 */
  const btn = (kind: string) => `btn btn-${kind}`;

  return (
    <div className="periph-panel">
      <h3 className="periph-title">
        外设控制
      </h3>

      {/* 软驱 */}
      {floppyDriveCount > 0 && (
        <div className="periph-group">
          <div className="periph-label">💾 软驱（{floppyDriveCount} 个）</div>
          {Array.from({ length: floppyDriveCount }).map((_, i) => {
            const st = floppyStates[i];
            return (
              <div key={i} className="periph-row">
                <span className="periph-drive-no">驱动器 {i + 1}</span>
                <FloppyLed motorRef={floppyMotorRef} index={i} />
                <input
                  ref={(el) => { fileInputRefs.current[i] = el; }}
                  type="file"
                  accept=".img,.dsk,.fdi,.td0,.86f"
                  onChange={(e) => handleFloppyFileChange(i, e)}
                  className="hidden-input"
                />
                {st?.inserted ? (
                  <>
                    <span className="periph-name" title={st.name}>
                      {st.name}
                      {st.modified && <span className="periph-modified">（已修改）</span>}
                    </span>
                    {st.modified && (
                      <button
                        className={btn('warn')}
                        onClick={() => handleSaveFloppy(i)}
                      >
                        保存
                      </button>
                    )}
                    <button
                      className={btn('danger')}
                      onClick={() => onFloppyEject(i)}
                    >
                      弹出
                    </button>
                  </>
                ) : (
                  <>
                    <span className="periph-empty">（空）</span>
                    <button
                      className={btn('primary')}
                      onClick={() => fileInputRefs.current[i]?.click()}
                    >
                      插入
                    </button>
                  </>
                )}
              </div>
            );
          })}
        </div>
      )}

      {/* 打印机 */}
      {hasPrinter && (
        <div className="periph-group">
          <div className="periph-head">
            <span className="periph-label">🖨️ 打印机</span>
            <span className="periph-meta">已输出 {printerPages} 页</span>
          </div>
          <div className="periph-actions">
            <button
              className={btn('purple')}
              onClick={() => setShowPrinterView((v) => !v)}
              disabled={printerPages === 0}
            >
              {showPrinterView ? '隐藏打印预览' : '查看最后一页'}
            </button>
          </div>
          {showPrinterView && printerPages > 0 && (
            <div className="periph-printview">
              <canvas
                ref={printerCanvasRef}
                style={{ display: 'block', imageRendering: 'pixelated' }}
              />
            </div>
          )}
        </div>
      )}

      {/* 磁带机 */}
      {hasTape && (
        <div className="periph-group">
          <div className="periph-head">
            <span className="periph-label">📼 磁带机</span>
            <span className="periph-meta">
              {tapeLoaded ? (tapePlaying ? '▶ 播放中' : '已载入') : '未载入'}
            </span>
          </div>
          <input
            ref={tapeInputRef}
            type="file"
            accept=".wav,.mp3,.ogg,.flac"
            onChange={async (e) => {
              const file = e.target.files?.[0];
              if (file) {
                await onTapeLoad(file);
              }
              e.target.value = '';
            }}
            className="hidden-input"
          />
          <div className="periph-actions">
            <button
              className={btn('primary')}
              onClick={() => tapeInputRef.current?.click()}
            >
              载入磁带音频
            </button>
            <button
              className={btn('orange')}
              onClick={onTapeRewind}
              disabled={!tapeLoaded}
            >
              ⏪ 倒带
            </button>
          </div>
          <p className="periph-hint">
            支持 WAV/MP3/OGG/FLAC 格式，自动重采样到 96kHz。
          </p>
        </div>
      )}
    </div>
  );
}
