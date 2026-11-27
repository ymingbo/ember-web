import tvMeshData from './tvMesh.json';

/**
 * 3D CRT 电视机渲染器 —— 移植自 em-fceux 的 es2 渲染管线。
 *
 * 对照文件：
 *   src/drivers/em/es2.cpp              渲染流程 / 常量 / uniform
 *   src/drivers/em/es2util.cpp          网格构建 / 法线解码
 *   src/drivers/em/meshes.h             网格数据（已导出为 tvMesh.json）
 *   src/drivers/em/assets/data/*.vert    着色器
 *   src/drivers/em/assets/data/*.frag
 *
 * 管线（es2Render 的 CRT 分支）：
 *   passScreen → passDownsample → passTV → passCombine
 *
 * 结构要点，全部照原版，不做任何"优化"：
 *   · passScreen 与 passTV 写**同一块 tv_fb**：屏幕先画进去，外壳再叠上。
 *   · passDownsample 采样的是 **tv_fb 自己**（TEX(TV_I + i)），
 *     对"已含屏幕的画面"逐级模糊，供外壳采样出反射光。
 *   · passCombine 把 tv_fb 与 downsample[3]/[5] 叠辉光后输出到屏幕。
 *
 * 尺寸（es2.cpp:88-93）：
 *   INPUT_W=256 INPUT_H=240 IDX_H=240
 *   SCREEN_W = NUM_SUBPS*INPUT_W = 4*256 = 1024   （NUM_SUBPS 取 4）
 *   SCREEN_H = 4*IDX_H = 960
 *   downsample 尺寸表：/4 /4 /16 /16 /64 /64
 */


// ---------------------------------------------------------------------------
// 常量 —— 全部照抄 es2.cpp 的 #define / static const
// ---------------------------------------------------------------------------

const INPUT_W = 256;   // es2.cpp:82  电视输入宽
const NUM_SUBPS = 4;   // es2.cpp:76
const IDX_H = 240;     // es2.cpp:88（与 INPUT_H 相等，原版单独定义）
const SCREEN_W = NUM_SUBPS * INPUT_W;   // 1024
const SCREEN_H = 4 * IDX_H;             // 960
/* es2.cpp:82-93：输入纹理带左右 overscan
     INPUT_W=256  OVERSCAN_W=12  IDX_W = INPUT_W + 2*OVERSCAN_W = 280
   sharpen.vert 里写死了 floor((256.0*a_2.x + 12.0)/280.0)，就是按 280 标定的。
   之前这里用 256 建纹理，那条映射全错。 */
/* video.cpp:62 —— 原版 s_targetAspect。
   292 是含左右 overscan 的幅面（IDX_W = 256 + 2*12 = 280，加补边到 292），
   纵向给满 240 行。es2Init 拿到的就是这个常量，不随窗口变。 */
const TARGET_ASPECT = 292 / 240;

const OVERSCAN_W = 12;
const IDX_W = INPUT_W + 2 * OVERSCAN_W;   // 280

/* ---- 放大链的两个 pass 参数 ----
   全部来自 em-fceux 的 config.inc.hpp 默认值与 es2.cpp 的换算公式：

     CONTROLLER( 5,  0.2, SHARPNESS)   CONTROLLER( 6,  1.0, NTSC_EMU)
     CONTROLLER( 7,  1.0, CRT_ENABLED) CONTROLLER( 8,  0.1, SCANLINES)
     CONTROLLER( 9,  0.4, CONVERGENCE)

   es2.cpp:158  updateSharpenKernel:
     v = NTSC_EMU * 0.4 * (SHARPNESS + 0.5) = 1.0 * 0.4 * 0.7 = 0.28
   es2.cpp:798  u_convergence = CRT_ENABLED * 2.0 * CONVERGENCE = 0.8
   es2.cpp:784  u_smoothenOffs.y = NTSC_EMU * -0.25 / IDX_H = -0.00104167
   es2.cpp:807  u_scanlines = CRT_ENABLED * 0.45 * SCANLINES = 0.045 */
/* 三枪会聚（CRT 的红蓝电子束没对正）。sharpen.vert 里用 u_convergence/1120
   把两个采样点左右各偏移，sharpen.frag 的 kernel 给这两点 R=1.0 / B=1.0 满权重，
   等于红色整体右移、蓝色整体左移，文字就有了红蓝重影。归零后不再错位。

   fceux 默认 CONVERGENCE=0.4 -> u_convergence = CRT_ENABLED*2.0*0.4 = 0.8 */
const CONVERGENCE_V = 0.0;
/* 扫描线强度。归零 —— 撤掉扫描线效果。

   fceux 原式 es2.cpp:807 是 CRT_ENABLED*0.45*SCANLINES（默认 0.1 得 0.045）。
   试过多档强度与几种频率/形状，在「内部 960 行 → canvas 约 733 行、缩放比 0.76」
   的输出链路上都做不出真 CRT 的观感：
     · 240 条（一条 ROM 行一条）：间距 3 像素、暗带 1 像素，看着是细密纹理
     · 120 条（每 2 行一条）：间距 6 像素，暗带够宽了但整屏发灰
   根因是最终缩放到屏幕时线性过滤把细条纹平均掉，靠调数值补不回来。
   所以直接关掉，保持画面纯净。 */
const SCANLINES_V = 0.0;
/* 纵向平滑偏移。fceux 原式 es2.cpp:784 是 NTSC_EMU * -0.25 / IDX_H，
   注释写明「smoothen if NTSC emulation is enabled」—— 这层混合是为 NTSC
   扫描线渗色准备的。NTSC 已移除，留着等于纯模糊，1 像素高的笔画被上下糊开。
   取 0 关掉混合，恢复像素级锐利。 */
const SMOOTHEN_Y = 0.0;
/* 锐化核。关掉 —— 改成恒等核（只取中心点，不做邻域加权）。
   fceux 原核（es2.cpp:updateSharpenKernel，SHARPNESS=0.2 → k=0.28）是：
       [-0.28,-0.28,-0.28]  左2
       [ 1.0 , 0.0 , 0.0 ]  左1（只给红）
       [ 0.56, 1.56, 0.56]  中心
       [ 0.0 , 0.0 , 1.0 ]  右1（只给蓝）
       [-0.28,-0.28,-0.28]  右2
   四个 -0.28 是【负权重】，会造成过冲：实测阶跃边缘上，
     亮侧算到 326（clamp 成 255，看着还正常）
     暗侧算到 -71（clamp 成 0）+ 蓝通道 183
   结果就是白字边缘镶深色、黑字边缘镶浅色 —— 用户肉眼能直接看到。
   同时红/蓝抽头权重 1.0 vs 绿 0，三个通道在同一像素上算出不同值，
   边缘还会带彩色。真 CRT 有过冲（光圈效应），但在文字上就是脏像素。

   改成恒等核后 passSharpen 变成直通，边缘干净，锐度全靠放大链本身。
   画面若偏软，用「平滑/像素」开关切到像素模式来补。 */
const SHARPEN_KERNEL = new Float32Array([
  0.0, 0.0, 0.0,
  0.0, 0.0, 0.0,
  1.0, 1.0, 1.0,
  0.0, 0.0, 0.0,
  0.0, 0.0, 0.0,
]);

/** es2.cpp:570-573 */
const LIGHT_DIR: [number, number, number] = [0.0, 0.866025, 0.5];
const VIEW_POS: [number, number, number] = [0, 0, 2.5];
const X_AXIS: [number, number, number] = [1, 0, 0];
const SHADOW_POINT: [number, number, number] = [0, 0.7737, 0.048];

/** es2.cpp:148-149 */
const DS_OFFS = [-6.892337, -4.922505, -2.953262, -0.98438, 0.98438, 2.953262, 4.922505, 6.892337];
const DS_WEIGHTS = [0.045894, 0.096038, 0.157115, 0.200954, 0.200954, 0.157115, 0.096038, 0.045894];

/** es2.cpp:151-152 降采样各级尺寸（相对 SCREEN_W / SCREEN_H 的除数） */
/* 降采样尺寸表，照 es2.cpp:151-152 的原文 —— **宽高不同步**，
   横向先降、纵向后降，交替进行：

     widths  = { SCREEN_W,    SCREEN_W/4,  SCREEN_W/4,  SCREEN_W/16, SCREEN_W/16, SCREEN_W/64, SCREEN_W/64 }
     heights = { SCREEN_H,    SCREEN_H,    SCREEN_H/4,  SCREEN_H/4,  SCREEN_H/16, SCREEN_H/16, SCREEN_H/64 }

   passDownsample 里第 i 级写进 fbo[i]，viewport 用 [i+1] 的尺寸，
   采样偏移按 [i]（源）归一。之前用单一 DS_DIVS 让宽高同步缩，
   fbo[0] 的高度就错了（应该是全高 960，我缩成了 240），
   整条模糊链错位，外框边缘的柔光也就不对了。 */
const DS_W_DIVS = [4, 4, 16, 16, 64, 64];
const DS_H_DIVS = [1, 4, 4, 16, 16, 64];

// ---------------------------------------------------------------------------
// 着色器 —— 逐字照抄 assets/data/ 下的文件，只把 a_0..a_3 换成可读名字
// ---------------------------------------------------------------------------

/** data/screen.vert */
const SCREEN_VS = `
attribute vec4 a_0;
attribute vec3 a_1;
attribute vec2 a_2;
uniform mat4 u_mvp;
uniform vec2 u_uvScale;
varying vec2 v_uv;
varying vec3 v_norm;
varying vec3 v_pos;
varying highp vec2 v_noiseUV;
void main() {
  v_uv = (0.5 + (u_uvScale * (a_2 - 0.5)));
  v_norm = a_1;
  v_pos = a_0.xyz;
  gl_Position = (u_mvp * a_0);
  highp vec4 tmpvar_1;
  tmpvar_1.zw = vec2(0.0, 1.0);
  tmpvar_1.xy = (gl_Position.xy / gl_Position.w);
  gl_Position = tmpvar_1;
  v_noiseUV = ((vec2(1024.0, 896.0) * ((tmpvar_1.xy * 0.5) + 0.5)) / vec2(256.0, 256.0));
}`;

/** data/screen.frag */
const SCREEN_FS = `
uniform highp vec3 u_lightDir;
uniform highp vec3 u_viewPos;
uniform highp vec4 u_material;
uniform highp vec3 u_fresnel;
uniform highp vec4 u_shadowPlane;
uniform sampler2D u_stretchTex;
uniform sampler2D u_noiseTex;
varying highp vec2 v_uv;
varying highp vec3 v_norm;
varying highp vec3 v_pos;
varying highp vec2 v_noiseUV;
void main() {
  lowp vec3 color_1;
  lowp vec4 tmpvar_2 = texture2D(u_stretchTex, v_uv);
  color_1 = (tmpvar_2.xyz * tmpvar_2.xyz);
  highp vec3 tmpvar_3 = normalize(v_norm);
  /* 屏幕玻璃的高光「重影」采样 —— 关掉。
     原版把画面沿法线反方向偏移 0.021 个 uv 再取一次色，乘 0.018 叠上去，
     模拟显像管玻璃内表面的反射。在纯色区域看不出来，但文字边缘会被
     复制出一层错位的淡影，笔画看着发毛、不干净。
     代价：玻璃表面少了那点反光质感，文字变锐利。 */
  // lowp vec4 tmpvar_4 = texture2D(u_stretchTex, (v_uv - (0.021 * tmpvar_3.xy)));
  // color_1 = (color_1 + ((0.018 * tmpvar_4.xyz) * tmpvar_4.xyz));
  /* 画面边缘软化 —— 原版就在这里，之前被我改成了 1.0（不软化）。

     vec2 tmpvar_5 = max((abs(v_uv - 0.5) - vec2(0.4814453, 0.484375)), 0.0);
     float tmpvar_6 = clamp((3.0 - (9000.0 * dot(tmpvar_5, tmpvar_5))), 0.0, 1.0);
     color_1 = (color_1 * tmpvar_6);

     逻辑：abs(v_uv-0.5) 在 0.4814453 以内 → tmpvar_5=0 → tmpvar_6=clamp(3,0,1)=1；
     越往外 tmpvar_5 线性增大，9000 倍平方迅速把 3 拉到 0 → 边缘平滑淡出。
     0.5-0.4814453 = 0.0185547，这就是软化的宽度（约 1.9% 画面宽）。
     同时光照项 (0.638*tmpvar_6 + 0.362) 也跟着从 1.0 落到 0.362，
     边缘既有亮度淡出又有高光衰减，所以看起来是柔的而不是硬切。

     之前我嫌它留暗边把它删了 —— 那是删错了。现在 idx 纹理已经带
     左右 overscan（IDX_W=280），这段必须原样恢复。 */
  highp vec2 tmpvar_5 = max((abs(v_uv - 0.5) - vec2(0.4814453, 0.484375)), 0.0);
  highp float tmpvar_6 = clamp((3.0 - (9000.0 * dot(tmpvar_5, tmpvar_5))), 0.0, 1.0);
  color_1 = (color_1 * tmpvar_6);
  highp float result_7;
  highp vec3 tmpvar_8 = normalize((u_lightDir + normalize((u_viewPos - v_pos))));
  highp float tmpvar_9 = dot(tmpvar_3, u_lightDir);
  if ((tmpvar_9 > 0.0)) {
    result_7 = (mix(u_material.x, (u_material.y * pow(max(dot(tmpvar_3, tmpvar_8), 0.0), u_material.z)),
      (u_fresnel.x + (u_fresnel.y * pow((1.0 - tmpvar_9), u_fresnel.z)))) * tmpvar_9);
  } else {
    result_7 = (-(tmpvar_9) * u_material.w);
  }
  result_7 = (result_7 * (0.21 + (0.79 * clamp((38.0 * max((dot(u_shadowPlane.xyz, v_pos)
    - u_shadowPlane.w), (v_pos.z - 0.023))), 0.0, 1.0))));
  lowp vec4 tmpvar_10;
  tmpvar_10.w = 1.0;
  /* 逐像素随机噪声 —— 关掉。
     原版给每个像素叠 ±0.0059（0.01171875 × (noise-0.5)）的随机扰动，
     模拟 CRT 的电子噪点。幅度在纯色背景上察觉不到，但经过 sqrt 曲线
     在文字边缘的过渡区被放大，形成肉眼可见的彩色杂点 ——
     这正是「文字周围有不干净像素」最直接的来源。
     代价：画面少了那层细微颗粒感，变成完全干净的数字画面。 */
  tmpvar_10.xyz = sqrt((color_1 + (result_7 * ((0.638 * tmpvar_6) + 0.362))));
  gl_FragColor = tmpvar_10;
}`;

/** data/tv.vert */
const TV_VS = `
attribute vec4 a_0;
attribute vec3 a_1;
attribute vec3 a_2;
attribute vec3 a_3;
uniform mat4 u_mvp;
varying vec3 v_radiance;
varying highp vec2 v_noiseUVs[2];
varying vec2 v_uv;
varying vec3 v_pos;
varying vec3 v_norm;
varying float v_blend;
void main() {
  vec3 tmpvar_1;
  tmpvar_1 = normalize(a_1);
  v_norm = a_1;
  v_pos = a_0.xyz;
  v_radiance = ((a_3 * a_3) * vec3(0.15, 0.2, 1.0));
  float tmpvar_2;
  tmpvar_2 = sqrt(dot(a_2, a_2));
  float tmpvar_3;
  tmpvar_3 = sqrt(dot(a_2.xy, a_2.xy));
  vec2 tmpvar_4;
  if ((tmpvar_3 > 0.0)) {
    tmpvar_4 = (a_2.xy / tmpvar_3);
  } else {
    tmpvar_4 = vec2(0.0, 0.0);
  }
  vec4 tmpvar_5;
  tmpvar_5.xy = (a_0.xy + ((vec2(5.5, 4.8125) * (vec2(tmpvar_3) + vec2(0.0014, 0.0019)))
    * normalize(mix(tmpvar_4, normalize(tmpvar_1.xy), max((1.0 - (30.0 * tmpvar_3)), 0.0)))));
  tmpvar_5.zw = a_0.zw;
  vec4 tmpvar_6;
  tmpvar_6 = (u_mvp * tmpvar_5);
  v_uv = (0.5 + ((0.5 * tmpvar_6.xy) / tmpvar_6.w));
  gl_Position = (u_mvp * a_0);
  highp vec4 tmpvar_7;
  tmpvar_7.zw = vec2(0.0, 1.0);
  tmpvar_7.xy = (gl_Position.xy / gl_Position.w);
  gl_Position = tmpvar_7;
  v_noiseUVs[0] = ((vec2(1024.0, 896.0) * ((tmpvar_7.xy * 0.5) + 0.5)) / vec2(256.0, 256.0));
  v_noiseUVs[1] = ((0.707 * v_noiseUVs[0]) + vec2(0.001953125, 0.001953125));
  v_blend = min((19.6 * tmpvar_2), 1.0);
}`;

/** data/tv.frag */
const TV_FS = `
uniform highp vec3 u_lightDir;
uniform highp vec3 u_viewPos;
uniform highp vec4 u_material;
uniform highp vec3 u_fresnel;
uniform highp vec4 u_shadowPlane;
uniform sampler2D u_downsample1Tex;
uniform sampler2D u_downsample3Tex;
uniform sampler2D u_downsample5Tex;
uniform sampler2D u_noiseTex;
varying highp vec3 v_radiance;
varying vec2 v_noiseUVs[2];
varying highp vec3 v_pos;
varying highp vec3 v_norm;
varying highp vec2 v_uv;
varying highp float v_blend;
void main() {
  lowp vec3 color_1;
  lowp vec3 ds1_2;
  lowp vec4 tmpvar_3 = texture2D(u_noiseTex, v_noiseUVs[0]);
  lowp vec4 tmpvar_4 = texture2D(u_downsample1Tex, v_uv);
  lowp vec4 tmpvar_5 = texture2D(u_downsample3Tex, v_uv);
  lowp vec4 tmpvar_6 = texture2D(u_downsample5Tex, v_uv);
  ds1_2 = (tmpvar_5.xyz * tmpvar_5.xyz);
  color_1 = (v_radiance.x * mix(ds1_2, (tmpvar_6.xyz * tmpvar_6.xyz), v_blend));
  color_1 = (color_1 + (v_radiance.y * mix((tmpvar_4.xyz * tmpvar_4.xyz), ds1_2, v_blend)));
  color_1 = (color_1 * (1.0 - (((0.15625 * v_blend) + 0.14578)
    * sqrt(abs(((2.0 * texture2D(u_noiseTex, v_noiseUVs[1]).x) - 1.0))))));
  highp vec3 tmpvar_7 = normalize(v_norm);
  highp float result_8;
  highp vec3 tmpvar_9 = normalize((u_lightDir + normalize((u_viewPos - v_pos))));
  highp float tmpvar_10 = dot(tmpvar_7, u_lightDir);
  if ((tmpvar_10 > 0.0)) {
    result_8 = (mix(u_material.x, (u_material.y * pow(max(dot(tmpvar_7, tmpvar_9), 0.0), u_material.z)),
      (u_fresnel.x + (u_fresnel.y * pow((1.0 - tmpvar_10), u_fresnel.z)))) * tmpvar_10);
  } else {
    result_8 = (-(tmpvar_10) * u_material.w);
  }
  result_8 = (result_8 * (0.21 + (0.79 * clamp((38.0 * max((dot(u_shadowPlane.xyz, v_pos)
    - u_shadowPlane.w), (v_pos.z - 0.023))), 0.0, 1.0))));

  lowp vec4 tmpvar_11;
  tmpvar_11.w = 1.0;
  tmpvar_11.xyz = (sqrt((color_1 + result_8)) + (0.01171875 * (tmpvar_3.x - 0.5)));
  gl_FragColor = tmpvar_11;
}`;

/** data/downsample.vert */
const DOWNSAMPLE_VS = `
uniform vec2 u_offsets[8];
attribute vec4 a_0;
attribute vec2 a_2;
varying vec2 v_uv[8];
void main() {
  gl_Position = a_0;
  v_uv[0] = (a_2 + u_offsets[0]);
  v_uv[1] = (a_2 + u_offsets[1]);
  v_uv[2] = (a_2 + u_offsets[2]);
  v_uv[3] = (a_2 + u_offsets[3]);
  v_uv[4] = (a_2 + u_offsets[4]);
  v_uv[5] = (a_2 + u_offsets[5]);
  v_uv[6] = (a_2 + u_offsets[6]);
  v_uv[7] = (a_2 + u_offsets[7]);
}`;

/** data/downsample.frag */
const DOWNSAMPLE_FS = `
uniform sampler2D u_downsampleTex;
uniform float u_weights[8];
varying vec2 v_uv[8];
void main() {
  lowp vec3 result_1;
  lowp vec4 tmpvar_2 = texture2D(u_downsampleTex, v_uv[0]);
  result_1 = ((u_weights[0] * tmpvar_2.xyz) * tmpvar_2.xyz);
  lowp vec4 t3 = texture2D(u_downsampleTex, v_uv[1]);
  result_1 = (result_1 + ((u_weights[1] * t3.xyz) * t3.xyz));
  lowp vec4 t4 = texture2D(u_downsampleTex, v_uv[2]);
  result_1 = (result_1 + ((u_weights[2] * t4.xyz) * t4.xyz));
  lowp vec4 t5 = texture2D(u_downsampleTex, v_uv[3]);
  result_1 = (result_1 + ((u_weights[3] * t5.xyz) * t5.xyz));
  lowp vec4 t6 = texture2D(u_downsampleTex, v_uv[4]);
  result_1 = (result_1 + ((u_weights[4] * t6.xyz) * t6.xyz));
  lowp vec4 t7 = texture2D(u_downsampleTex, v_uv[5]);
  result_1 = (result_1 + ((u_weights[5] * t7.xyz) * t7.xyz));
  lowp vec4 t8 = texture2D(u_downsampleTex, v_uv[6]);
  result_1 = (result_1 + ((u_weights[6] * t8.xyz) * t8.xyz));
  lowp vec4 t9 = texture2D(u_downsampleTex, v_uv[7]);
  result_1 = (result_1 + ((u_weights[7] * t9.xyz) * t9.xyz));
  lowp vec4 tmpvar_10;
  tmpvar_10.w = 1.0;
  tmpvar_10.xyz = sqrt(result_1);
  gl_FragColor = tmpvar_10;
}`;

/** data/combine.vert */
const COMBINE_VS = `
attribute vec4 a_0;
attribute vec2 a_2;
varying vec2 v_uv;
varying vec2 v_noiseUV;
void main() {
  gl_Position = a_0;
  v_uv = a_2;
  v_noiseUV = (vec2(4.0, 3.5) * a_2);
}`;

/** data/combine.frag */
const COMBINE_FS = `
uniform sampler2D u_tvTex;
uniform sampler2D u_downsample3Tex;
uniform sampler2D u_downsample5Tex;
uniform sampler2D u_noiseTex;
uniform highp vec3 u_glow;
varying highp vec2 v_uv;
varying highp vec2 v_noiseUV;
void main() {
  lowp vec3 color_1;
  lowp vec4 tmpvar_2 = texture2D(u_tvTex, v_uv);
  lowp vec4 tmpvar_3 = texture2D(u_downsample3Tex, v_uv);
  lowp vec4 tmpvar_4 = texture2D(u_downsample5Tex, v_uv);
  color_1 = (tmpvar_2.xyz * tmpvar_2.xyz);
  color_1 = (((color_1 + (u_glow.x * (tmpvar_3.xyz * tmpvar_3.xyz)))
    + (u_glow.y * (tmpvar_4.xyz * tmpvar_4.xyz))) / (1.0 + u_glow.y));
  lowp vec4 tmpvar_5;
  tmpvar_5.w = 1.0;
  tmpvar_5.xyz = (sqrt(color_1) + ((u_glow.z * 0.046875)
    * (texture2D(u_noiseTex, v_noiseUV).x - 0.5)));
  gl_FragColor = tmpvar_5;
}`;

/** data/sharpen.vert —— 逐字照抄 */
const SHARPEN_VS = `
attribute vec4 a_0;
attribute vec2 a_2;
uniform float u_convergence;
varying vec2 v_uv[5];
void main() {
  vec2 uv_1;
  uv_1.y = a_2.y;
  uv_1.x = (floor(((256.0 * a_2.x) + 12.0)) / 280.0);
  v_uv[0] = (uv_1 + vec2(-0.003571429, 0.0));
  vec2 tmpvar_2;
  tmpvar_2.y = 0.0;
  tmpvar_2.x = (u_convergence / 1120.0);
  v_uv[1] = (uv_1 + tmpvar_2);
  v_uv[2] = uv_1;
  vec2 tmpvar_3;
  tmpvar_3.y = 0.0;
  tmpvar_3.x = (-(u_convergence) / 1120.0);
  v_uv[3] = (uv_1 + tmpvar_3);
  v_uv[4] = (uv_1 + vec2(0.003571429, 0.0));
  gl_Position = a_0;
}`;

/** data/sharpen.frag —— 逐字照抄 */
const SHARPEN_FS = `
uniform sampler2D u_rgbTex;
uniform vec3 u_sharpenKernel[5];
varying vec2 v_uv[5];
void main() {
  lowp vec3 color_1;
  lowp vec4 tmpvar_2 = texture2D(u_rgbTex, v_uv[0]);
  color_1 = ((u_sharpenKernel[0] * tmpvar_2.xyz) * tmpvar_2.xyz);
  lowp vec4 tmpvar_3 = texture2D(u_rgbTex, v_uv[1]);
  color_1 = (color_1 + ((u_sharpenKernel[1] * tmpvar_3.xyz) * tmpvar_3.xyz));
  lowp vec4 tmpvar_4 = texture2D(u_rgbTex, v_uv[2]);
  color_1 = (color_1 + ((u_sharpenKernel[2] * tmpvar_4.xyz) * tmpvar_4.xyz));
  lowp vec4 tmpvar_5 = texture2D(u_rgbTex, v_uv[3]);
  color_1 = (color_1 + ((u_sharpenKernel[3] * tmpvar_5.xyz) * tmpvar_5.xyz));
  lowp vec4 tmpvar_6 = texture2D(u_rgbTex, v_uv[4]);
  color_1 = (color_1 + ((u_sharpenKernel[4] * tmpvar_6.xyz) * tmpvar_6.xyz));
  lowp vec4 tmpvar_7;
  tmpvar_7.w = 1.0;
  tmpvar_7.xyz = sqrt(color_1);
  gl_FragColor = tmpvar_7;
}`;

/** data/stretch.vert —— 逐字照抄 */
const STRETCH_VS = `
uniform vec2 u_smoothenOffs;
attribute vec4 a_0;
attribute vec2 a_2;
varying vec2 v_uv[2];
void main() {
  vec2 tmpvar_1;
  tmpvar_1.x = a_2.x;
  tmpvar_1.y = (1.0 - a_2.y);
  v_uv[0] = tmpvar_1;
  v_uv[1] = (v_uv[0] + u_smoothenOffs);
  gl_Position = a_0;
}`;

/** data/stretch.frag —— 逐字照抄 */
const STRETCH_FS = `
uniform highp float u_scanlines;
uniform sampler2D u_sharpenTex;
/* u_doSmoothen = 1 时才做纵向两行混合；纵向偏移为 0 时传 0，走直通分支。 */
uniform float u_doSmoothen;
varying vec2 v_uv[2];

void main() {
  /* fceux 原文（stretch.frag）无条件把本行与偏移 -0.25 行做平方和再开方：
       tmpvar_3 = c0*c0 + c1*c1;
       out = sqrt(mix(0.5*tmpvar_3, max(tmpvar_3-1.0,0.0), scanlines_term));
     纵向偏移归零时 c0 与 c1 是同一个点，上式在实数上等于 c0，
     但三个 lowp 浮点（8 位定点）来回一折腾会丢掉低位，像素边缘出现色阶断层，
     表现为文字周围一圈不干净的杂色。这里直接短路成单次采样。
     u_doSmoothen=1（偏移非零）时仍走原式，供将来需要纵向混合时使用。
     精度全部用 highp —— 这个 pass 的输出就是最终画面，lowp 不值得。 */
  vec3 c0 = texture2D(u_sharpenTex, v_uv[0]).rgb;

  if (u_doSmoothen < 0.5) {
    /* 直通：不做任何行间运算。扫描线强度为 0 时完全等价于原式。 */
    vec3 col = c0;
    if (u_scanlines > 0.0) {
      col = mix(col * col, max(col * col - 1.0, 0.0),
                (u_scanlines * (1.0 - abs(sin(((753.9822 * v_uv[0].y) - 0.3926991))))));
      col = sqrt(max(col, 0.0));
    }
    gl_FragColor = vec4(col, 1.0);
    return;
  }

  /* 原式：本行与偏移行平方和 */
  vec3 c1 = texture2D(u_sharpenTex, v_uv[1]).rgb;
  vec3 sq = c0 * c0 + c1 * c1;
  vec3 col = sqrt(mix(0.5 * sq, max(sq - 1.0, 0.0),
    (u_scanlines * (1.0 - abs(sin(((753.9822 * v_uv[0].y) - 0.3926991)))))));
  gl_FragColor = vec4(col, 1.0);
}`;

// ---------------------------------------------------------------------------
// 数学工具（对应 es2util.cpp）
// ---------------------------------------------------------------------------

/** es2util.cpp mat4Persp */
function mat4Persp(out: Float32Array, fovy: number, aspect: number, near: number, far: number) {
  const t = 1.0 / Math.tan(fovy / 2);
  const nf = 1 / (near - far);
  out.fill(0);
  out[0] = t / aspect;
  out[5] = t;
  out[10] = (far + near) * nf;
  out[11] = -1;
  out[14] = 2 * far * near * nf;
}

/** es2util.cpp mat4Trans */
function mat4Trans(out: Float32Array, v: [number, number, number]) {
  out.fill(0);
  out[0] = out[5] = out[10] = out[15] = 1;
  out[12] = v[0];
  out[13] = v[1];
  out[14] = v[2];
}

/** es2util.cpp mat4Mul：out = a * b */
function mat4Mul(out: Float32Array, a: Float32Array, b: Float32Array) {
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
      out[c * 4 + r] = s;
    }
  }
}

// ---------------------------------------------------------------------------
// 网格（对应 es2.cpp:938-968 的 rim_extra 计算 + es2util.cpp createUniqueEdges）
// ---------------------------------------------------------------------------

interface Mesh {
  pos: WebGLBuffer;
  norm: WebGLBuffer;
  /** 屏幕是 uv(2)，外壳是 extra(3) */
  attr2: WebGLBuffer;
  attr2Size: number;
  /** 只有外壳有 */
  vcol: WebGLBuffer | null;
  idx: WebGLBuffer;
  count: number;
}

/** es2util.cpp createUniqueEdges：找出只属于一个面的边 */
function createUniqueEdges(faces: number[]): number[] {
  const seen = new Map<string, number>();
  for (let f = 0; f < faces.length; f += 3) {
    const t = [faces[f], faces[f + 1], faces[f + 2]];
    for (let e = 0; e < 3; e++) {
      const a = t[e];
      const b = t[(e + 1) % 3];
      const key = a < b ? `${a}_${b}` : `${b}_${a}`;
      seen.set(key, (seen.get(key) ?? 0) + 1);
    }
  }
  const out: number[] = [];
  for (const [key, n] of seen) {
    if (n !== 1) continue;
    const [a, b] = key.split('_').map(Number);
    out.push(a, b);
  }
  return out;
}

/** es2util.cpp vec3ClosestOnSegment：把最近点写入 out */
function closestOnSegment(out: number[], p: number[], a: number[], b: number[]) {
  const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
  const len2 = ux * ux + uy * uy + uz * uz;
  let t = len2 > 0 ? ((p[0] - a[0]) * ux + (p[1] - a[1]) * uy + (p[2] - a[2]) * uz) / len2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  out[0] = a[0] + ux * t;
  out[1] = a[1] + uy * t;
  out[2] = a[2] + uz * t;
}

/** 照抄 es2.cpp:939-966，为外壳每个顶点算「到最近屏幕边」的向量 */
function computeRimExtra(screenVerts: number[], screenFaces: number[], rimVerts: number[]): Float32Array {
  const edges = createUniqueEdges(screenFaces);
  const rimCount = rimVerts.length / 3;
  const out = new Float32Array(rimCount * 3);
  const closest = [0, 0, 0];

  for (let i = 0; i < rimCount; i++) {
    const p = [rimVerts[3 * i], rimVerts[3 * i + 1], rimVerts[3 * i + 2]];
    const shortest = [0, 0, 0];
    let shortestDist = 1000000;

    for (let j = 0; j < edges.length; j += 2) {
      const ai = 3 * edges[j];
      const bi = 3 * edges[j + 1];
      closestOnSegment(closest, p,
        [screenVerts[ai], screenVerts[ai + 1], screenVerts[ai + 2]],
        [screenVerts[bi], screenVerts[bi + 1], screenVerts[bi + 2]]);
      const dx = closest[0] - p[0];
      const dy = closest[1] - p[1];
      const dz = closest[2] - p[2];
      const dist = dx * dx + dy * dy + dz * dz;
      if (dist < shortestDist) {
        shortestDist = dist;
        shortest[0] = dx; shortest[1] = dy; shortest[2] = dz;
      }
    }
    out[3 * i] = shortest[0];
    out[3 * i + 1] = shortest[1];
    out[3 * i + 2] = shortest[2];
  }
  return out;
}

// ---------------------------------------------------------------------------
// 着色器编译
// ---------------------------------------------------------------------------

/** 最近一次 WebGL 初始化错误，界面直接显示，避免静默退回 2D 后无从查起。 */
let lastGlError = '';
export function getLastTvError(): string {
  return lastGlError;
}

function compileShader(gl: WebGLRenderingContext, type: number, src: string): WebGLShader | null {
  const sh = gl.createShader(type);
  if (!sh) return null;
  gl.shaderSource(sh, src);
  gl.compileShader(sh);
  if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(sh) ?? '(无日志)';
    lastGlError = `${type === gl.VERTEX_SHADER ? '顶点' : '片元'}着色器编译失败: ${log}`;
    console.error('[tvRenderer]', lastGlError);
    gl.deleteShader(sh);
    return null;
  }
  return sh;
}

/** 对应 shaders.h:1 的 common_src —— 原版把它拼在每个着色器源码前面 */
const COMMON_SRC = 'precision highp float;\n';

function linkProgram(gl: WebGLRenderingContext, vs: string, fs: string): WebGLProgram | null {
  const v = compileShader(gl, gl.VERTEX_SHADER, COMMON_SRC + vs);
  const f = compileShader(gl, gl.FRAGMENT_SHADER, COMMON_SRC + fs);
  if (!v || !f) return null;
  const p = gl.createProgram();
  if (!p) return null;
  gl.attachShader(p, v);
  gl.attachShader(p, f);
  gl.linkProgram(p);
  gl.deleteShader(v);
  gl.deleteShader(f);
  if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
    lastGlError = `程序链接失败: ${gl.getProgramInfoLog(p) ?? '(无日志)'}`;
    console.error('[tvRenderer]', lastGlError);
    gl.deleteProgram(p);
    return null;
  }
  return p;
}

// ---------------------------------------------------------------------------
// initShading —— 照抄 es2.cpp:575-593
// ---------------------------------------------------------------------------

function initShading(
  gl: WebGLRenderingContext,
  prog: WebGLProgram,
  intensity: number, diff: number, fill: number, spec: number, m: number, fr0: number, frexp: number,
) {
  gl.uniform3fv(gl.getUniformLocation(prog, 'u_lightDir'), LIGHT_DIR);
  gl.uniform3fv(gl.getUniformLocation(prog, 'u_viewPos'), VIEW_POS);
  // u_material = (intensity*diff/π, intensity*spec*(m+8)/(8π), m, intensity*fill/π)
  gl.uniform4f(gl.getUniformLocation(prog, 'u_material'),
    (intensity * diff) / Math.PI,
    (intensity * spec * (m + 8.0)) / (8.0 * Math.PI),
    m,
    (intensity * fill) / Math.PI);
  gl.uniform3f(gl.getUniformLocation(prog, 'u_fresnel'), fr0, 1 - fr0, frexp);

  // shadowPlane = normalize(cross(xAxis, lightDir))，w = dot(plane, shadowPoint)
  const sx = X_AXIS[1] * LIGHT_DIR[2] - X_AXIS[2] * LIGHT_DIR[1];
  const sy = X_AXIS[2] * LIGHT_DIR[0] - X_AXIS[0] * LIGHT_DIR[2];
  const sz = X_AXIS[0] * LIGHT_DIR[1] - X_AXIS[1] * LIGHT_DIR[0];
  const len = Math.sqrt(sx * sx + sy * sy + sz * sz) || 1;
  const px = sx / len, py = sy / len, pz = sz / len;
  const pw = px * SHADOW_POINT[0] + py * SHADOW_POINT[1] + pz * SHADOW_POINT[2];
  gl.uniform4f(gl.getUniformLocation(prog, 'u_shadowPlane'), px, py, pz, pw);
}

// ---------------------------------------------------------------------------
// 噪声纹理 —— 对应 genNoiseTex（256x256 灰噪）
// ---------------------------------------------------------------------------

function createNoiseTexture(gl: WebGLRenderingContext): WebGLTexture {
  const size = 256;
  const data = new Uint8Array(size * size * 4);
  // 用固定种子的伪随机，保证每次一致
  let seed = 0x2f6e2b1;
  const rnd = () => {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    return seed / 0x7fffffff;
  };
  for (let i = 0; i < size * size; i++) {
    const v = Math.floor(rnd() * 256);
    data[i * 4] = v;
    data[i * 4 + 1] = v;
    data[i * 4 + 2] = v;
    data[i * 4 + 3] = 255;
  }
  const tex = gl.createTexture()!;
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, size, size, 0, gl.RGBA, gl.UNSIGNED_BYTE, data);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
  return tex;
}

// ---------------------------------------------------------------------------
// 主渲染器
// ---------------------------------------------------------------------------

export interface TvRenderer {
  draw(data: Uint8Array, width: number, height: number): void;
  resize(width?: number, height?: number): void;
  /** 画面平滑：true 双线性（LINEAR），false 硬像素（NEAREST）。
      照 ember win32 的做法 —— screen-window.cpp:326 用
      filter==Pixel ? pointSampler : linearSampler 二选一。
      这里作用在最终输出到屏幕那一步（passCombine 采 tvFbo），
      NEAREST 不跨行平均，扫描线和像素边缘会保持锐利。 */
  setSmooth(smooth: boolean): void;
  dispose(): void;
  /** 画面在画布内的归一化矩形（0~1）。鼠标/触摸要用它换算坐标：
   *  canvas 是 4:3 的整块，但 3D 电视机只占中间一块，
   *  用整块 canvas 换算会让「触发面积」比「实际画面」大一圈。 */
  getScreenRect(): { x0: number; y0: number; x1: number; y1: number };
}

interface Fbo {
  tex: WebGLTexture;
  fb: WebGLFramebuffer;
  w: number;
  h: number;
}

function makeTex(gl: WebGLRenderingContext, w: number, h: number, filter: number): Fbo | null {
  const tex = gl.createTexture();
  const fb = gl.createFramebuffer();
  if (!tex || !fb) return null;
  gl.bindTexture(gl.TEXTURE_2D, tex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, filter);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return { tex, fb, w, h };
}

function makeBuffer(gl: WebGLRenderingContext, data: Float32Array | Uint8Array | Uint16Array, target: number): WebGLBuffer {
  const b = gl.createBuffer()!;
  gl.bindBuffer(target, b);
  gl.bufferData(target, data, gl.STATIC_DRAW);
  return b;
}

export function createTvRenderer(canvas: HTMLCanvasElement): TvRenderer | null {
  lastGlError = '';

  const gl = canvas.getContext('webgl', {
    alpha: false,
    antialias: false,
    depth: false,
    stencil: false,
    preserveDrawingBuffer: true,
    premultipliedAlpha: false,
  }) as WebGLRenderingContext | null;
  if (!gl) {
    lastGlError = '创建 WebGL 上下文失败';
    return null;
  }

  const sharpenProg = linkProgram(gl, SHARPEN_VS, SHARPEN_FS);
  const stretchProg = linkProgram(gl, STRETCH_VS, STRETCH_FS);
  const screenProg = linkProgram(gl, SCREEN_VS, SCREEN_FS);
  const tvProg = linkProgram(gl, TV_VS, TV_FS);
  const dsProg = linkProgram(gl, DOWNSAMPLE_VS, DOWNSAMPLE_FS);
  const combineProg = linkProgram(gl, COMBINE_VS, COMBINE_FS);
  if (!sharpenProg || !stretchProg || !screenProg || !tvProg || !dsProg || !combineProg) {
    return null;
  }

  const noiseTex = createNoiseTexture(gl);


  /* passSharpen / passStretch 的两个目标缓冲，尺寸照 es2.cpp:905 / 911：
       sharpen_fb  SCREEN_W x IDX_H   GL_NEAREST
       stretch_fb  SCREEN_W x SCREEN_H GL_LINEAR
     这两个 pass 是 es2Render 里无条件执行的第一步，负责把 256x240
     先锐化、再双线性放大到 1024x960 —— 画面边缘的光滑感就来自这里。
     之前整条链缺了这两步，256 直接贴到网格上，放大后就是锯齿。 */
  const sharpenFbo = makeTex(gl, SCREEN_W, IDX_H, gl.NEAREST);
  const stretchFbo = makeTex(gl, SCREEN_W, SCREEN_H, gl.LINEAR);
  if (!sharpenFbo || !stretchFbo) {
    lastGlError = '创建放大链 FBO 失败';
    return null;
  }

  // ---- 网格 ----
  const mkMesh = (
    verts: number[], norms: number[], attr2: number[], attr2Size: number,
    vcols: number[] | null, faces: number[],
  ): Mesh => {
    const pos = makeBuffer(gl, new Float32Array(verts), gl.ARRAY_BUFFER);
    const norm = makeBuffer(gl, new Float32Array(norms), gl.ARRAY_BUFFER);
    const a2 = makeBuffer(gl, new Float32Array(attr2), gl.ARRAY_BUFFER);
    // 顶点色是 UNSIGNED_BYTE（es2.cpp:143），必须归一化上传
    const vcol = vcols ? makeBuffer(gl, new Uint8Array(vcols), gl.ARRAY_BUFFER) : null;
    const idx = makeBuffer(gl, new Uint16Array(faces), gl.ELEMENT_ARRAY_BUFFER);
    return { pos, norm, attr2: a2, attr2Size, vcol, idx, count: faces.length };
  };

  /* ---- 电视机的几何适配 ----

     模型原始尺寸（tvMesh.json）：
       外框  x=±1.0361  y=±0.8294  → 比例 1.2492
       屏幕  x=±0.9558  y=±0.7041
       外框内沿 x=±0.9387  y=±0.7038（外框在屏幕范围内最靠内的一圈）

     画布比例见 EmulatorCanvas.tsx 的 TV_ASPECT = 1.2012，
     buildMvp 里 aspect = (W/H)*1.04 = 1.2492，正好等于外框比例。
     fceux-web 同款做法：s_targetAspect = 292/240 = 1.2167（×1.04 = 1.2653），
     它的画布形状就是照着电视机定的，所以外框两边从来没有黑边。

     几何全部直接用网格原始数据，不做任何缩放 —— 和 fceux 一致：
     fceux 把网格按原尺寸投影，外框比视口还大，四边自然裁掉一点
     （画布 474x390 时左右各 5.0px、上下各 6.5px），屏幕到画布边横 13.7px 纵 23.9px。
 */

  /* 屏幕网格 uv：**原样使用网格自带的 uv**，不做任何重映射。

     fceux 里网格 uv 是什么就传什么（meshes.h 的 mesh_screen_uvs，
     u 0.03262~0.96692、v 0.0104~0.9896），画面范围的伸缩全靠
     screen.vert 的 u_uvScale 和 screen.frag 的软化项去处理。

     这里之前被我自己加了一步「把 uv 重新线性映射到 0~1」，
     以为这样画面才能铺满 —— 那是错的，而且连锁毁了两样东西：
       ① u_uvScale 外扩的量对不上了（uv 已经被拉满，再乘 1.09766
          就扩到 -0.048~1.048，CLAMP_TO_EDGE 一路钳到边缘像素）；
       ② 软化项 abs(v_uv-0.5) > 0.4814453 判断的是**外扩后**的 v_uv，
          uv 一变，软化圈的落点全偏，画面外圈被提前压黑。
     看起来就是画面缩在内腔里、四周留一圈暗边。
     删掉 remap 后 uv 回到原值，上面两项就和 fceux 完全一致了。 */
  const screenUvs = tvMeshData.screen.uvs;

  const screenMesh = mkMesh(
    tvMeshData.screen.verts, tvMeshData.screen.norms, screenUvs, 2,
    null, tvMeshData.screen.faces,
  );

  /* 外壳的 a_2 是运行时算出来的 rim_extra（es2.cpp:943-966）：
     每个外框顶点到**屏幕边缘最近点**的向量。tv.vert 用它算
       v_blend = min(19.6 * |a_2|, 1.0)
     靠近屏幕时 v_blend→0（外框显出更多反射光），远离时→1（纯材质）。
     外框边上那圈柔光就是它做出来的。

     输入是屏幕和外框的原网格顶点，两边同一坐标系，不算任何缩放。 */
  const rimExtra = computeRimExtra(
    tvMeshData.screen.verts, tvMeshData.screen.faces, tvMeshData.rim.verts,
  );
  const rimVerts = tvMeshData.rim.verts;
  const rimMesh = mkMesh(
    rimVerts, tvMeshData.rim.norms, Array.from(rimExtra), 3,
    tvMeshData.rim.vcols, tvMeshData.rim.faces,
  );

  // 全屏 quad：a_0 位置 = (-1,-1)..(1,1)，a_2 uv = (0,0)..(1,1)
  const quadPos = makeBuffer(gl, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.ARRAY_BUFFER);
  const quadUV = makeBuffer(gl, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.ARRAY_BUFFER);

  // ---- 纹理与 FBO ----
  /* idx 纹理：sharpening 链的输入。尺寸和过滤照 es2.cpp:887
       createTex(&s_p.idx_tex, IDX_W, IDX_H, GL_LUMINANCE, GL_NEAREST, GL_CLAMP_TO_EDGE, 0)
     即 280x240、NEAREST。NEAREST 这点很关键：sharpen.vert 里
       uv.x = floor((256.0*a_2.x + 12.0)) / 280.0
     已经把 uv 量化到像素格中心，采样必须用 NEAREST，用 LINEAR 会
     把相邻像素混进来，边缘反而糊/闪。 */
  const stretchTex = gl.createTexture()!;
  gl.bindTexture(gl.TEXTURE_2D, stretchTex);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, IDX_W, IDX_H, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

  // tv_fb：passScreen 与 passTV 共用（es2.cpp:726-744）
  const tvFbo = makeTex(gl, SCREEN_W, SCREEN_H, gl.NEAREST);
    /* 画面平滑开关。默认 false = 硬像素（NEAREST）。
       切换时只改纹理参数，不重建 FBO —— texParameteri 可随时调用。 */
    let smoothOutput = false;
  // 6 级降采样 FBO，尺寸取 es2.cpp:151-152 的表
  const dsFbos: (Fbo | null)[] = DS_W_DIVS.map((dw, i) =>
    makeTex(gl, Math.floor(SCREEN_W / dw), Math.floor(SCREEN_H / DS_H_DIVS[i]), gl.LINEAR));
  if (!tvFbo || dsFbos.some((f) => !f)) {
    lastGlError = '创建离屏帧缓冲失败';
    return null;
  }
  const ds = dsFbos as Fbo[];

  // ---- MVP（es2Init，es2.cpp:853-866）----
  const proj = new Float32Array(16);
  const view = new Float32Array(16);
  const mvp = new Float32Array(16);
  const buildMvp = () => {
    /* aspect 用 fceux 的**固定常量**，不是 canvas 的 w/h。

       es2Init(s_targetAspect) 传进去的是 video.cpp:62 的常量
         s_targetAspect = 292.0/240.0
       不是运行时窗口比例 —— 原版窗口怎么缩放，投影比例都不变。
       里面再 aspect *= 1.04（注释原文：stretch aspect slightly as the
       TV mesh is wee bit vertically squished）。

       我们之前用 canvas.width/height，取整后是 1.21461 而不是 1.21667，
       投影跟着漂，网格就不是按 fceux 的比例落到画布上了。 */
    const aspect = (TARGET_ASPECT) * 1.04;
    mat4Persp(proj, (0.25 * Math.PI) / aspect, aspect, 0.125, 16.0);
    mat4Trans(view, [0, 0, -2.5]);
    mat4Mul(mvp, proj, view);
  };
  buildMvp();

  // ---- 属性绑定 ----
  const bindMesh = (prog: WebGLProgram, mesh: Mesh, isRim: boolean) => {
    const a0 = gl.getAttribLocation(prog, 'a_0');
    const a1 = gl.getAttribLocation(prog, 'a_1');
    const a2 = gl.getAttribLocation(prog, 'a_2');
    const a3 = gl.getAttribLocation(prog, 'a_3');

    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.pos);
    gl.enableVertexAttribArray(a0);
    gl.vertexAttribPointer(a0, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.norm);
    gl.enableVertexAttribArray(a1);
    gl.vertexAttribPointer(a1, 3, gl.FLOAT, false, 0, 0);

    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.attr2);
    gl.enableVertexAttribArray(a2);
    gl.vertexAttribPointer(a2, mesh.attr2Size, gl.FLOAT, false, 0, 0);

    if (isRim && a3 >= 0 && mesh.vcol) {
      // a_3 是 UNSIGNED_BYTE 顶点色，normalize=true 归一化到 0..1
      gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vcol);
      gl.enableVertexAttribArray(a3);
      gl.vertexAttribPointer(a3, 3, gl.UNSIGNED_BYTE, true, 0, 0);
    }

    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, mesh.idx);
  };

  const bindQuad = (prog: WebGLProgram) => {
    const a0 = gl.getAttribLocation(prog, 'a_0');
    const a2 = gl.getAttribLocation(prog, 'a_2');
    gl.bindBuffer(gl.ARRAY_BUFFER, quadPos);
    gl.enableVertexAttribArray(a0);
    gl.vertexAttribPointer(a0, 2, gl.FLOAT, false, 0, 0);
    if (a2 >= 0) {
      gl.bindBuffer(gl.ARRAY_BUFFER, quadUV);
      gl.enableVertexAttribArray(a2);
      gl.vertexAttribPointer(a2, 2, gl.FLOAT, false, 0, 0);
    }
  };

  // ---- uniform 缓存 ----
  const u = {
    screenStretch: gl.getUniformLocation(screenProg, 'u_stretchTex'),
    screenNoise: gl.getUniformLocation(screenProg, 'u_noiseTex'),
    screenUvScale: gl.getUniformLocation(screenProg, 'u_uvScale'),
    screenMvp: gl.getUniformLocation(screenProg, 'u_mvp'),

    tvDs1: gl.getUniformLocation(tvProg, 'u_downsample1Tex'),
    tvDs3: gl.getUniformLocation(tvProg, 'u_downsample3Tex'),
    tvDs5: gl.getUniformLocation(tvProg, 'u_downsample5Tex'),
    tvNoise: gl.getUniformLocation(tvProg, 'u_noiseTex'),
    tvMvp: gl.getUniformLocation(tvProg, 'u_mvp'),

    dsTex: gl.getUniformLocation(dsProg, 'u_downsampleTex'),
    dsOffsets: gl.getUniformLocation(dsProg, 'u_offsets'),
    dsWeights: gl.getUniformLocation(dsProg, 'u_weights'),

    cbTv: gl.getUniformLocation(combineProg, 'u_tvTex'),
    cbDs3: gl.getUniformLocation(combineProg, 'u_downsample3Tex'),
    cbDs5: gl.getUniformLocation(combineProg, 'u_downsample5Tex'),
    cbNoise: gl.getUniformLocation(combineProg, 'u_noiseTex'),
    cbGlow: gl.getUniformLocation(combineProg, 'u_glow'),
  };

  // ---- 初始化 uniform（initUniformsScreen / initUniformsTV / initUniformsCombine）----
  gl.useProgram(screenProg);
  gl.uniform1i(u.screenStretch, 0);
  gl.uniform1i(u.screenNoise, 1);
  /* u_uvScale —— 照 es2.cpp:605 原值，不再自创。

       1.0 + 25.0/INPUT_W = 1 + 25/256 = 1.09766
       1.0 + 15.0/IDX_H   = 1 + 15/240 = 1.06250

     作用：把网格 uv（u 0.0326~0.9669、v 0.0104~0.9896，和原版网格完全一致）
     往外扩一圈，正好覆盖 idx 纹理里那圈 overscan 填充。
     配合 screen.frag 的软化项（abs(v_uv-0.5) > 0.4814453 开始衰减），
     软化的那 1.9% 落在 overscan 上，有效画面边缘才能保持清晰 ——
     这就是「边缘看着柔和但内容不糊」的来历。

     之前为了补缺失的放大链把它改成 1.0，软化圈就直接压在有效画面上，
     画面边缘发灰、外框柔光也接不上。放大链补齐后必须回到原值。 */
  gl.uniform2f(u.screenUvScale, 1.0 + 25.0 / INPUT_W, 1.0 + 15.0 / IDX_H);
  gl.uniformMatrix4fv(u.screenMvp, false, mvp);
  initShading(gl, screenProg, 4.0, 0.001, 0.0, 0.065, 41, 0.04, 4); // es2.cpp:609

  gl.useProgram(tvProg);
  gl.uniform1i(u.tvDs1, 2);
  gl.uniform1i(u.tvDs3, 3);
  gl.uniform1i(u.tvDs5, 4);
  gl.uniform1i(u.tvNoise, 1);
  gl.uniformMatrix4fv(u.tvMvp, false, mvp);
  initShading(gl, tvProg, 4.0, 0.0038, 0.0035, 0.039, 49, 0.03, 4); // es2.cpp:631

  gl.useProgram(dsProg);
  gl.uniform1i(u.dsTex, 2);
  gl.uniform1fv(u.dsWeights, new Float32Array(DS_WEIGHTS)); // es2.cpp:149

  gl.useProgram(combineProg);
  gl.uniform1i(u.cbTv, 0);
  gl.uniform1i(u.cbDs3, 3);
  gl.uniform1i(u.cbDs5, 5);
  gl.uniform1i(u.cbNoise, 1);
  /* es2.cpp:807-810  u_glow = (v, v², v + v²)，其中 v = 0.1 * GLOW；
     GLOW 默认值 0.2（config.inc.hpp:10 CONTROLLER(4, 0.2, GLOW, "glow")）
     ⇒ v = 0.02，u_glow = (0.02, 0.0004, 0.0204)。
     之前我传 (0.15, 0.08, 1.0) 是编的，辉光强了十几倍。 */
  const glowV = 0.1 * 0.2;
  gl.uniform3f(u.cbGlow, glowV, glowV * glowV, glowV + glowV * glowV);

  // ---- passDownsample（es2.cpp:714-724）----
  const dsOffsets = new Float32Array(16);
  const runDownsample = () => {
    gl.useProgram(dsProg);
    {
      for (let i = 0; i < 6; i++) {
        gl.bindFramebuffer(gl.FRAMEBUFFER, ds[i].fb);
        gl.viewport(0, 0, ds[i].w, ds[i].h);
        // updateUniformsDownsample：(offsets, texIdx, horizontal)
        // 水平分量用在偶数级、垂直用在奇数级；偏移按**源**尺寸归一
        const horizontal = (i & 1) === 0;
        const srcW = i === 0 ? SCREEN_W : ds[i - 1].w;
        const srcH = i === 0 ? SCREEN_H : ds[i - 1].h;
        for (let k = 0; k < 8; k++) {
          dsOffsets[k * 2] = horizontal ? DS_OFFS[k] / srcW : 0;
          dsOffsets[k * 2 + 1] = horizontal ? 0 : DS_OFFS[k] / srcH;
        }
        gl.uniform2fv(u.dsOffsets, dsOffsets);
        gl.uniform1fv(u.dsWeights, new Float32Array(DS_WEIGHTS));

        // 源纹理：第 0 级读 tv_fb，之后读上一级
        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, i === 0 ? tvFbo.tex : ds[i - 1].tex);

        bindQuad(dsProg);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      }
    }
  };

  let disposed = false;
  let srcW = 0;
  let srcH = 0;
  let padded: Uint8Array | null = null;
  /* 非 256x240 画面的适配缓冲（见 draw 开头的等比缩放）。 */
  let fitBuf: Uint8Array | null = null;

  const draw = (data: Uint8Array, width: number, height: number) => {
    if (disposed || width < 1 || height < 1) return;

    /* ---- 非 256x240 的画面先适配到 256x240（中华学习机 560x384）----
       整条 CRT 管线（stretch / sharpen / screen / TV / combine）都是按 NES 的
       256x240 标定的：sharpen.vert 里 floor((256.0*a_2.x + 12.0)/280.0) 写死了
       256 与 280，FBO 也按 SCREEN_W=4*256、SCREEN_H=4*240 分配。别的尺寸直接喂
       进来会错位成黑屏，实测 Apple II 类机型 560x384 就是全黑。

       这里的做法是在入口做一次等比缩放 + 居中 + 补背景色边，适配成 256x240 后
       再走原管线 —— 管线本身一行不改，其他机型走的分支和以前完全一致。
       最近邻采样（不插值），避免引入原管线没有的模糊。

       代价：560x384 的水平精度会被压到 256。Apple II 文本模式一个字符 7 像素宽，
       压完仍有约 6 像素，文字仍可辨认。要全精度就用 Canvas2D 模式，那条路径
       走 putImageData，不经过任何着色器。 */
    let src = data;
    if (width !== INPUT_W || height !== IDX_H) {
      if (!fitBuf || fitBuf.length !== INPUT_W * IDX_H * 4) {
        fitBuf = new Uint8Array(INPUT_W * IDX_H * 4);
      }
      /* 等比缩放比：取两个方向的较小值，保证整幅画面都放得下 */
      const ratio = Math.min(INPUT_W / width, IDX_H / height);
      const fitW = Math.max(1, Math.min(INPUT_W, Math.round(width * ratio)));
      const fitH = Math.max(1, Math.min(IDX_H, Math.round(height * ratio)));
      const offX = Math.floor((INPUT_W - fitW) / 2);
      const offY = Math.floor((IDX_H - fitH) / 2);
      /* 补边色取源画面左上角像素 —— 和下面 overscan 取 data[0..2] 是同一个思路 */
      const bg0 = data[0];
      const bg1 = data[1];
      const bg2 = data[2];
      for (let i = 0; i < fitBuf.length; i += 4) {
        fitBuf[i] = bg0;
        fitBuf[i + 1] = bg1;
        fitBuf[i + 2] = bg2;
        fitBuf[i + 3] = 255;
      }
      for (let y = 0; y < fitH; y++) {
        /* 最近邻：源行的中心处取样，避免整行取到边界 */
        const sy = Math.min(height - 1, Math.floor((y + 0.5) * height / fitH));
        const srcRow = sy * width * 4;
        const dstRow = ((y + offY) * INPUT_W + offX) * 4;
        for (let x = 0; x < fitW; x++) {
          const sx = Math.min(width - 1, Math.floor((x + 0.5) * width / fitW));
          const sOff = srcRow + sx * 4;
          const dOff = dstRow + x * 4;
          fitBuf[dOff] = data[sOff];
          fitBuf[dOff + 1] = data[sOff + 1];
          fitBuf[dOff + 2] = data[sOff + 2];
          fitBuf[dOff + 3] = 255;
        }
      }
      src = fitBuf;
    }

    // ---- 上传画面到 idx 纹理（IDX_W x IDX_H = 280 x 240）----
    /* 照 es2.cpp:1015-1030：256 宽的画面放进 280 宽的纹理里，左右各留
       12 像素的 overscan，用背景色填充。原版是：
         glTexSubImage2D(..., OVERSCAN_W, 0, IDX_W-2*OVERSCAN_W, IDX_H, ...)
         memset(overscan_pixels, overscan_color, OVERSCAN_W*IDX_H)
         glTexSubImage2D(..., 0, 0, OVERSCAN_W, IDX_H, ...)
         glTexSubImage2D(..., IDX_W-OVERSCAN_W, 0, OVERSCAN_W, IDX_H, ...)

       为什么这一步关系到边缘平滑：sharpen.vert 里 uv 是
         uv.x = floor((256.0*a_2.x + 12.0)) / 280.0
       它假设纹理宽 280、画面从第 12 列开始。纹理只有 256 宽时，
       这条映射整条错位，采样点在像素边界上跳动 —— 边缘就是硬锯齿。 */
    const rowBytes = INPUT_W * 4;
    if (!padded || padded.length !== IDX_W * IDX_H * 4) {
      padded = new Uint8Array(IDX_W * IDX_H * 4);
    }
    padded.fill(0);
    for (let y = 0; y < IDX_H; y++) {
      const srcOff = y * rowBytes;
      const dstOff = (y * IDX_W + OVERSCAN_W) * 4;
      padded.set(src.subarray(srcOff, srcOff + rowBytes), dstOff);
    }
    /* 左右 overscan：照 es2.cpp:1024 填 **overscan_color**。

       fceux 是：
         overscan_color = s_p.PALRAM[0]        // NES 背景色
         memset(s_p.overscan_pixels, overscan_color, OVERSCAN_W * IDX_H)
         glTexSubImage2D(..., 0, 0, OVERSCAN_W, IDX_H, overscan_pixels)
         glTexSubImage2D(..., IDX_W-OVERSCAN_W, 0, OVERSCAN_W, IDX_H, ...)
       一整条左右边栏都是同一个颜色，就是 PPU 的背景色。

       我们这里拿 data 的左下角像素当 overscan_color —— 画面第 0 扫描行第 0 列
       在裕兴/学习机 ROM 里就是清屏用的背景色，和 PALRAM[0] 同源。
       （之前用过「复制首末列」，那是错的：逐行复制的色带会把画面边缘
         拉出一条亮边，看起来就是画面和内腔之间有一道缝。） */
    const bgR = src[0];
    const bgG = src[1];
    const bgB = src[2];
    for (let y = 0; y < IDX_H; y++) {
      const dst = y * IDX_W * 4;
      for (let x = 0; x < OVERSCAN_W; x++) {
        const l = dst + x * 4;
        padded[l] = bgR;
        padded[l + 1] = bgG;
        padded[l + 2] = bgB;
        padded[l + 3] = 255;
        const r = dst + (OVERSCAN_W + INPUT_W + x) * 4;
        padded[r] = bgR;
        padded[r + 1] = bgG;
        padded[r + 2] = bgB;
        padded[r + 3] = 255;
      }
    }

    /* 这里**不翻转**，和原版一致（es2.cpp:1020-1022 直接 glTexSubImage2D）。
       原版数据流：idx 不翻 → stretch.vert 里 (1.0 - a_2.y) 翻一次
       → 写进 stretch_fb → screen.vert 用网格 uv 采样 → 读回正向。只翻一次。 */
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, stretchTex);
    /* 判断纹理要不要重建只能用**缩放后**的尺寸：适配层把一切都变成 256x240，
       原始尺寸恒定不变，拿原始尺寸判断会让 560x384 这类机型只重建一次。 */
    if (INPUT_W !== srcW || IDX_H !== srcH) {
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, IDX_W, IDX_H, 0, gl.RGBA, gl.UNSIGNED_BYTE, padded);
      srcW = INPUT_W;
      srcH = IDX_H;
    } else {
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, IDX_W, IDX_H, gl.RGBA, gl.UNSIGNED_BYTE, padded);
    }

    gl.disable(gl.DEPTH_TEST);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);

    /* ---- passSharpen（es2.cpp:696-703 + 155-167）----
       输入：stretchTex（256x240 原始画面）
       输出：sharpenFbo（SCREEN_W x IDX_H = 1024 x 240）
       逐字对应：NTSC 信号路径的锐化，u_sharpenKernel 用 updateSharpenKernel 的公式算。 */
    gl.bindFramebuffer(gl.FRAMEBUFFER, sharpenFbo.fb);
    gl.viewport(0, 0, SCREEN_W, IDX_H);
    gl.useProgram(sharpenProg);
    {
      const kRgb = gl.getUniformLocation(sharpenProg, 'u_rgbTex');
      gl.uniform1i(kRgb, 0);
      const kConv = gl.getUniformLocation(sharpenProg, 'u_convergence');
      gl.uniform1f(kConv, CONVERGENCE_V);
      const kKernel = gl.getUniformLocation(sharpenProg, 'u_sharpenKernel[0]');
      gl.uniform3fv(kKernel, SHARPEN_KERNEL);
    }
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, stretchTex);
    bindQuad(sharpenProg);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    /* ---- passStretch（es2.cpp:705-712）----
       输入：sharpen_fb（1024x240）
       输出：stretchFbo（SCREEN_W x SCREEN_H = 1024 x 960）
       u_scanlines 做扫描线，u_smoothenOffs 做纵向偏移采样 —— 这两个
       叠起来就是「画面光滑无锯齿」的原因。 */
    gl.bindFramebuffer(gl.FRAMEBUFFER, stretchFbo.fb);
    gl.viewport(0, 0, SCREEN_W, SCREEN_H);
    gl.useProgram(stretchProg);
    {
      const kSh = gl.getUniformLocation(stretchProg, 'u_sharpenTex');
      gl.uniform1i(kSh, 0);
      const kScan = gl.getUniformLocation(stretchProg, 'u_scanlines');
      gl.uniform1f(kScan, SCANLINES_V);
      const kOff = gl.getUniformLocation(stretchProg, 'u_smoothenOffs');
      gl.uniform2f(kOff, 0, SMOOTHEN_Y);
      /* 纵向偏移为 0 时两行采样是同一个点，直接走直通分支，
         避开 lowp 精度在平方和开方上的损失。 */
      const kDoSm = gl.getUniformLocation(stretchProg, 'u_doSmoothen');
      gl.uniform1f(kDoSm, SMOOTHEN_Y !== 0 ? 1 : 0);
    }
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, sharpenFbo.tex);
    bindQuad(stretchProg);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    // ---- passScreen（es2.cpp:726-733）：屏幕 → tv_fb ----
    gl.bindFramebuffer(gl.FRAMEBUFFER, tvFbo.fb);
    gl.viewport(0, 0, SCREEN_W, SCREEN_H);
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.useProgram(screenProg);
    gl.activeTexture(gl.TEXTURE0);
    // 采放大链的输出（1024x960），不是 256x240 原纹理
    gl.bindTexture(gl.TEXTURE_2D, stretchFbo.tex);
    /* 这一步才是决定扫描线存亡的地方：stretchFbo 里带着 1024x960 的
       扫描线周期，屏幕网格采样它时若用 LINEAR，相邻行会被平均掉，
       扫描线和像素硬边一起糊成灰雾；NEAREST 则原样取点，硬边保留。
       stretchFbo 平时是渲染目标（LINEAR），只有这里当采样源，
       所以每帧按开关重设一次。 */
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, smoothOutput ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, smoothOutput ? gl.LINEAR : gl.NEAREST);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, noiseTex);
    bindMesh(screenProg, screenMesh, false);
    gl.drawElements(gl.TRIANGLES, screenMesh.count, gl.UNSIGNED_SHORT, 0);

    // ---- passDownsample（es2.cpp:714-724）：tv_fb → 6 级 ----
    runDownsample();

    // ---- passTV（es2.cpp:735-744）：外壳叠到 tv_fb 上 ----
    gl.bindFramebuffer(gl.FRAMEBUFFER, tvFbo.fb);
    gl.viewport(0, 0, SCREEN_W, SCREEN_H);
    gl.useProgram(tvProg);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, noiseTex);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, ds[0].tex); // u_downsample1Tex
    gl.activeTexture(gl.TEXTURE3);
    gl.bindTexture(gl.TEXTURE_2D, ds[2].tex); // u_downsample3Tex
    gl.activeTexture(gl.TEXTURE4);
    gl.bindTexture(gl.TEXTURE_2D, ds[4].tex); // u_downsample5Tex
    bindMesh(tvProg, rimMesh, true);
    gl.drawElements(gl.TRIANGLES, rimMesh.count, gl.UNSIGNED_SHORT, 0);

    // ---- passCombine（es2.cpp:746-753）：tv_fb + ds3/ds5 辉光 → 屏幕 ----
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.useProgram(combineProg);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, tvFbo.tex);
    /* 输出到屏幕这一步的采样方式。NEAREST 保留像素与扫描线的硬边，
       LINEAR 把 1024x960 平滑缩到 canvas 尺寸。tvFbo 平时是渲染目标
       （NEAREST），只有这里当采样源用，所以每帧按开关重设一次即可。 */
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, smoothOutput ? gl.LINEAR : gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, smoothOutput ? gl.LINEAR : gl.NEAREST);
    gl.activeTexture(gl.TEXTURE3);
    gl.bindTexture(gl.TEXTURE_2D, ds[2].tex);
    gl.activeTexture(gl.TEXTURE5);
    gl.bindTexture(gl.TEXTURE_2D, ds[4].tex);
    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, noiseTex);
    bindQuad(combineProg);
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  };

  /* 画面在画布内的归一化矩形。
     按屏幕网格的边界和 buildMvp 同参数投影算出来，窗口比例变化时自动跟着变。 */
  function getScreenRect() {
    const aspect = (TARGET_ASPECT) * 1.04;   // 与 buildMvp 完全一致
    const t = Math.tan((0.25 * Math.PI) / aspect / 2);   // tan(fovy/2)
    const vz = 2.5;                      // mat4Trans([0,0,-2.5])

    /* 直接投影屏幕网格的每个顶点，取真实边界 —— 不用任何校正系数。

       网格是带 z 起伏的曲面（z -0.094~0.006），透视后外圈顶点比平面公式
       算出来更靠内。逐顶点投影取极值，结果自然就是实际画面边界。 */
    let ax = 0;
    let ay = 0;
    const verts = tvMeshData.screen.verts;
    for (let i = 0; i < verts.length; i += 3) {
      const x = verts[i];
      const y = verts[i + 1];
      const z = verts[i + 2];
      // 与 buildMvp 同一条变换：view 平移 z=2.5，再透视
      const zc = -(z - vz);              // mat4Trans([0,0,-2.5]) 后相机空间 z
      const ndcX = x / (t * aspect * zc);
      const ndcY = y / (t * zc);
      if (Math.abs(ndcX) > ax) ax = Math.abs(ndcX);
      if (Math.abs(ndcY) > ay) ay = Math.abs(ndcY);
    }
    return {
      x0: Math.max(0, (1 - ax) / 2),
      y0: Math.max(0, (1 - ay) / 2),
      x1: Math.min(1, (1 + ax) / 2),
      y1: Math.min(1, (1 + ay) / 2),
    };
  }

  return {
    draw,
    getScreenRect,
    setSmooth(smooth: boolean) {
      smoothOutput = smooth;
    },

    resize() {
      buildMvp();
      gl.useProgram(screenProg);
      gl.uniformMatrix4fv(u.screenMvp, false, mvp);
      gl.useProgram(tvProg);
      gl.uniformMatrix4fv(u.tvMvp, false, mvp);
    },
    dispose() {
      disposed = true;
      gl.deleteTexture(stretchTex);
      gl.deleteTexture(sharpenFbo.tex);
      gl.deleteFramebuffer(sharpenFbo.fb);
      gl.deleteTexture(stretchFbo.tex);
      gl.deleteFramebuffer(stretchFbo.fb);
      gl.deleteProgram(sharpenProg);
      gl.deleteProgram(stretchProg);
      gl.deleteTexture(noiseTex);
      gl.deleteTexture(tvFbo.tex);
      gl.deleteFramebuffer(tvFbo.fb);
      for (const f of ds) {
        gl.deleteTexture(f.tex);
        gl.deleteFramebuffer(f.fb);
      }
      for (const p of [screenProg, tvProg, dsProg, combineProg]) gl.deleteProgram(p);
    },
  };
}
