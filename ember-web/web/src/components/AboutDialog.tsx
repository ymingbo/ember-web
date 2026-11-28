import { useEffect } from 'react';

interface AboutDialogProps {
  open: boolean;
  onClose: () => void;
  version: string;
}

/** 上游项目地址。移植声明必须指向这个仓库，不要改。 */
const UPSTREAM_URL = 'https://github.com/sungbindev/famiclone/tree/master/ember';

/** 本项目开源地址。 */
const PROJECT_URL = 'https://github.com/ymingbo/ember-web';

/**
 * 关于窗口 —— 显示当前版本号和上游移植声明。
 *
 * 交互约定：
 * - 点遮罩关闭、点右上角 × 关闭、按 Esc 关闭
 * - 打开时锁住页面滚动，避免背后画面跟着动
 */
export function AboutDialog({ open, onClose, version }: AboutDialogProps) {
  /* Esc 关闭。监听挂在 window 上，这样焦点在哪都能关。 */
  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  if (!open) return null;

  return (
    <div className="about-mask" onClick={onClose}>
      {/* 点内容区不关闭 —— stopPropagation 拦住冒泡到遮罩 */}
      <div className="about-box" onClick={(e) => e.stopPropagation()}>
        <div className="about-head">
          <h2 className="about-title">关于</h2>
          <button type="button" className="about-close" onClick={onClose} aria-label="关闭">
            ×
          </button>
        </div>

        <div className="about-body">
          <div className="about-row">
            <span className="about-label">软件版本</span>
            <span className="about-value about-ver">v{version}</span>
          </div>

          <div className="about-row">
            <span className="about-label">开源地址</span>
            <span className="about-value">
              <a
                className="about-link about-link-inline"
                href={PROJECT_URL}
                target="_blank"
                rel="noopener noreferrer"
              >
                github.com/ymingbo/ember-web
              </a>
            </span>
          </div>

          <p className="about-note">
            本模拟器移植自{' '}
            <a
              className="about-link about-link-inline"
              href={UPSTREAM_URL}
              target="_blank"
              rel="noopener noreferrer"
            >
              sungbindev/famiclone · ember
            </a>{' '}
            开源项目。
          </p>
        </div>
      </div>
    </div>
  );
}
