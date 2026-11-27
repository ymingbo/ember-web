/*
 * ------------------------------------------------------------------------
 * 名称：win32-dev-support.mjs
 * 说明：原生外壳（ember-win.exe）的开发期脚本（冒烟、延迟测量）共用的工具：启动与结束自己启动的进程、用系统输入驱动窗口（点击、按键、截屏取像素统计、读窗口属性、正常关闭）。
 * 作者：sungbin
 * 邮箱：sungbin@3578.cn
 * 日期：2026-11-19
 * 备注：窗口里没有 CDP，所以用 PowerShell 经 SendInput 一类系统接口驱动，截屏统计亮像素数来判断画面、菜单、提示与面板是否出现；
 *       坐标都是窗口的逻辑像素（默认窗口 524×500），驱动时按窗口实际宽度换算成设备像素，所以任何缩放比例下都能用；
 *       只结束本脚本启动的进程（按 PID），不按进程名结束任何进程；用户数据目录是临时目录，不影响已打开的 Ember，也避开单实例锁；
 *       可执行文件是发布配置的 ember-win.exe（`npm run win32:build` 构建），环境变量 EMBER_WIN_EXE 可指定别的路径（如调试配置的）。
 * ------------------------------------------------------------------------
 */

import { execFileSync, spawn } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { WIN32_BUILD_DIRECTORY } from '../build/build-paths.mjs';

/** Ember 项目根目录。 */
export const projectRoot = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..');

/** 解析命令行：--名称=值 或 --名称。 */
export function parseArguments(argv) {
  const options = new Map();
  for (const argument of argv) {
    const match = /^--([^=]+)(?:=(.*))?$/.exec(argument);
    if (match) options.set(match[1], match[2] ?? 'true');
  }
  return options;
}

/** 等待指定毫秒。 */
export function sleep(milliseconds) {
  return new Promise((done) => setTimeout(done, milliseconds));
}

/** 默认窗口的逻辑尺寸（与 bootstrap/application.cpp 的 kWindowWidth、kWindowHeight 一致）。 */
export const WINDOW_LOGICAL = { width: 524, height: 500 };

/** ember-win.exe 的路径。 */
export function executablePath() {
  return process.env.EMBER_WIN_EXE ?? join(WIN32_BUILD_DIRECTORY, 'app', 'Release', 'ember-win.exe');
}

/**
 * 启动一个原生外壳实例。
 * @param {{ romPath?: string, diagnostics?: boolean, args?: string[] }} options romPath 为启动后加载的卡带；diagnostics 为 true 时退出时写出帧计数与延迟；args 为额外的命令行参数。
 * @returns {{ pid: number, userData: string, diagnosticsFile: string | undefined, exited: Promise<number | null>, stop: () => void }}
 */
export function startApp(options = {}) {
  const exe = executablePath();
  if (!existsSync(exe)) throw new Error(`找不到 ${exe}，先运行 npm run win32:build`);
  const userData = mkdtempSync(join(tmpdir(), 'ember-win-smoke-'));
  const diagnosticsFile = options.diagnostics ? join(userData, 'diagnostics.json') : undefined;
  const args = [`--user-data-dir=${userData}`];
  if (options.romPath) args.push(`--cartridge=${options.romPath}`);
  if (options.args) args.push(...options.args);
  const env = { ...process.env };
  if (diagnosticsFile) env.EMBER_WIN_DIAGNOSTICS = diagnosticsFile;
  const child = spawn(exe, args, { env, stdio: 'ignore' });
  const exited = new Promise((done) => child.once('exit', (code) => done(code)));
  return {
    pid: child.pid,
    userData,
    diagnosticsFile,
    exited,
    stop() {
      try {
        execFileSync('taskkill', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore' });
      } catch {
        // 进程已经退出。
      }
    }
  };
}

/** 读出诊断文件（退出时写出）；没有时返回 undefined。 */
export function readDiagnostics(app) {
  if (!app.diagnosticsFile || !existsSync(app.diagnosticsFile)) return undefined;
  return JSON.parse(readFileSync(app.diagnosticsFile, 'utf8'));
}

/**
 * 经 VS Code 扩展里的调试客户端对实例发一个调试请求（实例要带 --debug-port 与 --debug-token 启动）。
 * @param {{ userData: string }} app 实例。
 * @param {string} method 请求名，如 runtime.status。
 * @param {...string} params key=value 形式的参数。
 * @returns {unknown} 请求的结果。
 * @throws {Error} 客户端没有构建（先在 vscode 目录运行 npm run build）、连不上或请求失败。
 */
export function debugCall(app, method, ...params) {
  const script = join(projectRoot, 'tools', 'dev', 'debug-call.mjs');
  const output = execFileSync(process.execPath, [script, app.userData, method, ...params], { encoding: 'utf8', timeout: 60_000 });
  return JSON.parse(output);
}

/** 删除实例的临时用户数据目录；进程刚结束时文件句柄可能还没释放，所以失败后重试。 */
export function removeUserData(app) {
  rmSync(app.userData, { recursive: true, force: true, maxRetries: 20, retryDelay: 250 });
}

/** 驱动脚本的 PowerShell 部分（只含 ASCII，经 -EncodedCommand 传入，避开脚本文件的编码问题）。 */
const DRIVER_HEAD = `
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class W32 {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextLengthW(IntPtr h);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint type);
  public static IntPtr Dialog(uint pid, IntPtr main) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, p) => { uint id; GetWindowThreadProcessId(h, out id); if (id == pid && h != main && IsWindowVisible(h) && GetWindowTextLengthW(h) > 0) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr e);
  [DllImport("user32.dll", EntryPoint="GetWindowLongW")] public static extern int GetLong(IntPtr h, int i);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  public static int Bright(Bitmap bmp, int x1, int y1, int x2, int y2) {
    x1 = Math.Max(0, x1); y1 = Math.Max(0, y1); x2 = Math.Min(bmp.Width, x2); y2 = Math.Min(bmp.Height, y2);
    if (x2 <= x1 || y2 <= y1) return 0;
    BitmapData data = bmp.LockBits(new Rectangle(x1, y1, x2 - x1, y2 - y1), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    byte[] bytes = new byte[data.Stride * data.Height];
    Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
    int count = 0;
    for (int y = 0; y < data.Height; y++) {
      int row = y * data.Stride;
      for (int x = 0; x < data.Width; x++) {
        int i = row + x * 4;
        if (bytes[i] > 160 || bytes[i + 1] > 160 || bytes[i + 2] > 160) count++;
      }
    }
    bmp.UnlockBits(data);
    return count;
  }
}
"@
[void][W32]::SetProcessDPIAware()
$proc = Get-Process -Id $ProcessIdValue
$h = $proc.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Output 'RESULT error no-window'; exit 0 }
# 先直接把窗口提到前台；有对话框时提的是对话框（主窗口被它禁用）；失败时按一下 Alt 取得权限再试（Alt 会被应用当成菜单键，所以不要每次都按）
$target = [W32]::Dialog([uint32]$proc.Id, $h)
if ($target -eq [IntPtr]::Zero) { $target = $h }
[void][W32]::SetForegroundWindow($target)
Start-Sleep -Milliseconds 300
if ([W32]::GetForegroundWindow() -ne $target) {
  [W32]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [W32]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [void][W32]::SetForegroundWindow($target)
  Start-Sleep -Milliseconds 300
}
Write-Output ('RESULT foreground ' + ([W32]::GetForegroundWindow() -eq $target))
$vk = @{ F7=0x76; F8=0x77; F9=0x78; F11=0x7A; ESC=0x1B; ENTER=0x0D; DOWN=0x28; UP=0x26; COMMA=0xBC; CTRL=0x11; ALT=0x12; SHIFT=0x10 }
function Press($code, $mods) {
  foreach ($m in $mods) { [W32]::keybd_event([byte]$vk[$m], 0, 0, [UIntPtr]::Zero) }
  $scan = [byte][W32]::MapVirtualKeyW([uint32]$code, 0)
  [W32]::keybd_event([byte]$code, $scan, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
  [W32]::keybd_event([byte]$code, $scan, 2, [UIntPtr]::Zero)
  foreach ($m in $mods) { [W32]::keybd_event([byte]$vk[$m], 0, 2, [UIntPtr]::Zero) }
}
$dlg = [IntPtr]::Zero
$dlgWidth = 720.0
function Get-Rect { $r = New-Object W32+RECT; [void][W32]::GetWindowRect($h, [ref]$r); $r }
function Get-Scale { $r = Get-Rect; ($r.Right - $r.Left) / ${WINDOW_LOGICAL.width} }
`;

/**
 * 在窗口上依次执行步骤，返回每个 RESULT 行解析成的对象。
 * 步骤写法（坐标是窗口的逻辑像素）：click:x:y、move:x:y、key:名称（F7、F8、F9、F11、ESC、ENTER、UP、DOWN）、chord:修饰键:名称（如 chord:CTRL:COMMA）、type:文字（逐个字母按键）、wait:毫秒、
 * count:名称:x1:y1:x2:y2（统计区域里的亮像素数）、style:名称（读窗口的置顶状态、宽高与位置）、close（正常关闭窗口）；
 * 对话框：dlgfind:名称（找本进程的对话框并返回是否找到）、dlgwidth:逻辑宽度（告诉脚本对话框的逻辑宽度，用来换算缩放）、dclick:x:y（点对话框里的位置，逻辑像素）、dlgvisible:名称、dshot:路径（把找到的对话框截成 PNG，路径里的盘符冒号照常写）。
 * @param {number} pid 进程号。
 * @param {string[]} steps 步骤。
 * @returns {Map<string, string>} 名称对应的结果文字。
 */
export function drive(pid, steps) {
  const lines = steps.map((step) => `'${step.replaceAll("'", "''")}'`).join(', ');
  const body = `
$steps = @(${lines})
foreach ($step in $steps) {
  $p = $step.Split(':')
  $scale = Get-Scale
  switch ($p[0]) {
    'click' { $r = Get-Rect; [void][W32]::SetCursorPos($r.Left + [int]([double]$p[1] * $scale), $r.Top + [int]([double]$p[2] * $scale)); Start-Sleep -Milliseconds 80; [W32]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W32]::mouse_event(4,0,0,0,[UIntPtr]::Zero) }
    'move' { $r = Get-Rect; [void][W32]::SetCursorPos($r.Left + [int]([double]$p[1] * $scale), $r.Top + [int]([double]$p[2] * $scale)) }
    'key' { $k = $vk[$p[1].ToUpper()]; [W32]::keybd_event([byte]$k,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W32]::keybd_event([byte]$k,0,2,[UIntPtr]::Zero) }
    'wait' { Start-Sleep -Milliseconds ([int]$p[1]) }
    'count' {
      $r = Get-Rect; $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
      $bmp = New-Object System.Drawing.Bitmap $w, $hgt
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $hgt))
      $g.Dispose()
      $n = [W32]::Bright($bmp, [int]([double]$p[2] * $scale), [int]([double]$p[3] * $scale), [int]([double]$p[4] * $scale), [int]([double]$p[5] * $scale))
      $bmp.Dispose()
      Write-Output ('RESULT ' + $p[1] + ' ' + $n)
    }
    'style' {
      $r = Get-Rect
      $top = (([W32]::GetLong($h, -20) -band 8) -ne 0)
      Write-Output ('RESULT ' + $p[1] + ' ' + $top + ' ' + ($r.Right - $r.Left) + ' ' + ($r.Bottom - $r.Top) + ' ' + $r.Left + ' ' + $r.Top)
    }
    'close' { [void]$proc.CloseMainWindow() }
    'chord' { Press $vk[$p[2].ToUpper()] @($p[1].ToUpper().Split('+')) }
    'type' { foreach ($c in $p[1].ToCharArray()) { Press ([int][char]::ToUpper($c)) @(); Start-Sleep -Milliseconds 40 } }
    'dlgwidth' { $dlgWidth = [double]$p[1] }
    'dlgfind' { $dlg = [W32]::Dialog([uint32]$proc.Id, $h); Write-Output ('RESULT ' + $p[1] + ' ' + ($dlg -ne [IntPtr]::Zero)) }
    'dlgvisible' { Write-Output ('RESULT ' + $p[1] + ' ' + (($dlg -ne [IntPtr]::Zero) -and [W32]::IsWindowVisible($dlg))) }
    'dclick' {
      $dr = New-Object W32+RECT; [void][W32]::GetWindowRect($dlg, [ref]$dr)
      $ds = ($dr.Right - $dr.Left) / $dlgWidth
      [void][W32]::SetCursorPos($dr.Left + [int]([double]$p[1] * $ds), $dr.Top + [int]([double]$p[2] * $ds)); Start-Sleep -Milliseconds 80; [W32]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [W32]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    }
    'shot' {
      $r = Get-Rect; $w = $r.Right - $r.Left; $hgt = $r.Bottom - $r.Top
      $bmp = New-Object System.Drawing.Bitmap $w, $hgt
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size $w, $hgt))
      $g.Dispose(); $bmp.Save(($p[1] + ':' + $p[2]), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    }
    'dshot' {
      $dr = New-Object W32+RECT; [void][W32]::GetWindowRect($dlg, [ref]$dr); $w = $dr.Right - $dr.Left; $hgt = $dr.Bottom - $dr.Top
      $bmp = New-Object System.Drawing.Bitmap $w, $hgt
      $g = [System.Drawing.Graphics]::FromImage($bmp)
      $g.CopyFromScreen($dr.Left, $dr.Top, 0, 0, (New-Object System.Drawing.Size $w, $hgt))
      $g.Dispose(); $bmp.Save(($p[1] + ':' + $p[2]), [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    }
  }
}
`;
  const script = `$ProcessIdValue = ${pid}\n${DRIVER_HEAD}\n${body}`;
  const encoded = Buffer.from(script, 'utf16le').toString('base64');
  const output = execFileSync('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded], { encoding: 'utf8', timeout: 120_000, stdio: ['ignore', 'pipe', 'ignore'] });
  const results = new Map();
  for (const line of output.split(/\r?\n/)) {
    const match = /^RESULT (\S+) (.*)$/.exec(line.trim());
    if (match) results.set(match[1], match[2]);
  }
  return results;
}

/** 拖放脚本的 PowerShell 部分（只含 ASCII）：用一个带文件数据的窗口发起真实的拖放，再用鼠标事件把它拖到目标窗口上松开。 */
const DROP_SCRIPT = `
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class W32 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
[void][W32]::SetProcessDPIAware()
$proc = Get-Process -Id $ProcessIdValue
$r = New-Object W32+RECT
[void][W32]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
$scale = ($r.Right - $r.Left) / 524
$tx = $r.Left + [int]($TargetX * $scale)
$ty = $r.Top + [int]($TargetY * $scale)
$files = [string[]]@($FileList)
$script:done = $false
$form = New-Object System.Windows.Forms.Form
$form.FormBorderStyle = 'None'
$form.StartPosition = 'Manual'
$form.Location = New-Object System.Drawing.Point(20, 20)
$form.Size = New-Object System.Drawing.Size(200, 120)
$form.TopMost = $true
$form.BackColor = [System.Drawing.Color]::Red
$form.Add_MouseDown({
  $data = New-Object System.Windows.Forms.DataObject
  $data.SetData([System.Windows.Forms.DataFormats]::FileDrop, $files)
  [void]$form.DoDragDrop($data, [System.Windows.Forms.DragDropEffects]::Copy)
  $script:done = $true
})
$form.Show()
$job = Start-Job -ArgumentList $tx, $ty -ScriptBlock {
  param($tx, $ty)
  Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class M {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
}
"@
  [void][M]::SetProcessDPIAware()
  Start-Sleep -Milliseconds 500
  [void][M]::SetCursorPos(80, 80)
  Start-Sleep -Milliseconds 300
  [M]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 300
  for ($i = 1; $i -le 25; $i++) {
    [void][M]::SetCursorPos([int](80 + ($tx - 80) * $i / 25), [int](80 + ($ty - 80) * $i / 25))
    Start-Sleep -Milliseconds 40
  }
  Start-Sleep -Milliseconds 500
  [M]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
}
$watch = [System.Diagnostics.Stopwatch]::StartNew()
while (-not $script:done -and $watch.Elapsed.TotalSeconds -lt 25) {
  [System.Windows.Forms.Application]::DoEvents()
  Start-Sleep -Milliseconds 30
}
$form.Close()
Wait-Job $job -Timeout 5 | Out-Null
Remove-Job $job -Force
Write-Output ('RESULT dropped ' + $script:done)
`;

/**
 * 把文件拖到窗口上松开（真实的 OLE 拖放：先显示一个带文件数据的小窗口，再用鼠标事件拖过去），用来检查拖放载入。
 * @param {number} pid 进程号。
 * @param {string[]} files 文件的绝对路径。
 * @param {{ x: number, y: number }} target 松开的位置（窗口的逻辑像素）。
 * @returns {boolean} 拖放是否完成。
 */
export function dropFiles(pid, files, target) {
  const list = files.map((file) => `'${file.replaceAll("'", "''")}'`).join(', ');
  const script = `$ProcessIdValue = ${pid}\n$TargetX = ${target.x}\n$TargetY = ${target.y}\n${DROP_SCRIPT.replace('@($FileList)', `@(${list})`)}`;
  const encoded = Buffer.from(script, 'utf16le').toString('base64');
  const output = execFileSync('powershell', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-EncodedCommand', encoded], { encoding: 'utf8', timeout: 120_000, stdio: ['ignore', 'pipe', 'ignore'] });
  return /RESULT dropped True/.test(output);
}
