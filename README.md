# HaloGlow

[English](#english) · [日本語](#日本語) · [中文](#中文) · [한국어](#한국어)

**Status: 0.1.0 development build — algorithm version 1 uses a provisional calibration table.**

---

## English

HaloGlow is an MIT-licensed glow effect for Adobe After Effects. It spreads
bright parts of an image with a three-part point-spread function — a tight
core, a mid-range diffusion and a long, faint halo — instead of one large
Gaussian. The design is specified in [`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)
(Japanese).

### Features

- One luminance-based soft-knee extraction that keeps the colour ratio of the
  source light; HDR values are never clipped at 1.
- Normalised elliptical PSF with independent Width / Height, calibrated
  against the exact pyramid the plug-in runs.
- Gamma, Saturation and two-colour tint on the glow only; separate Glow and
  Source Opacity; Glow Only output with a matching alpha.
- GPU rendering through CUDA and OpenCL (Windows) and Metal (macOS), with a CPU
  path that uses the same filter and the same arithmetic.
- Multi-Frame Rendering support.
- English, Japanese, Simplified Chinese and Korean parameter names.

### Parameters

| Group | Parameter | Range | Default |
| --- | --- | --- | --- |
| Status | GPU Acceleration (read-only) | Active / ready / unavailable, with framework and device | — |
| Basic | Intensity | 0–8 | 1 |
| | Size (90 % energy radius, px) | 0–512 | 64 |
| | Threshold | 0–1600 % (slider 0–100 %) | 25 % |
| | Threshold Softness | 0–100 % | 50 % |
| Shape | Width / Height | 0–400 % | 100 % |
| | Falloff | 0–100 | 50 |
| Colour & Tone | Gamma | 0.1–3 | 1 |
| | Saturation | 0–200 % | 100 % |
| | Colour Strength | 0–100 % | 0 % |
| | Colour 1 / Colour 2 | colour | warm white / white |
| | Luminance Space | Auto, Rec.709 / sRGB, Display P3, ACEScg | Auto |
| Output | Glow Opacity / Source Opacity | 0–100 % | 100 % |
| | Glow Only | on / off | off |

### Colour management

HaloGlow processes the working RGB values After Effects supplies and never
guesses or decodes a transfer function. The glow is a true linear-light glow
only when the host supplies linear working values (for example with
*Linearize Working Space* or a linear OCIO working space). **Luminance Space**
selects the primaries used for luminance; *Auto* is a best-effort value read on
the main thread (After Effects 25.1 or later) and may be stale after the
project colour settings change — choose the space explicitly when it matters.
Without a recognised working space Auto falls back to Rec.709 / sRGB.

### Building

Place this repository inside the After Effects SDK `Examples` folder.

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

On macOS use `cmake -S . -B build/Mac -G Xcode`. CUDA, OpenCL and Metal are
detected automatically; `-DHG_FORCE_CPU_ONLY=ON` builds a CPU-only plug-in. The
plug-in is written to `dist/<platform>/<config>/`.

### Tests

`hg_core_tests` checks extraction, tone, DC gain, tiled rendering, PSF shape
and continuity. `hg_gpu_parity` compares CUDA and OpenCL, and `hg_metal_parity`
compares Metal, with the CPU reference implementation.

### Known limitations of 0.1.0

- The coefficient table is provisional; anisotropic and full-phase shape
  tests (spec §12.1, §12.1a) are not complete.
- Not yet validated inside After Effects; performance targets are unmeasured.
- GPU memory is allocated per render; the pooled allocator and the shared-memory
  local filter are open items.
- DirectX rendering falls back to the CPU path.

### Licence

MIT — see [LICENSE](LICENSE).

---

## 日本語

HaloGlowはAdobe After Effects用のMITライセンスのグローエフェクトです。大きなGaussianを一つ掛けるのではなく、狭い芯、中距離の拡散、遠くまで弱く続くハローの三成分からなる点像分布関数（PSF）で、画像の明るい部分を広げます。設計は[`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)にまとめてあります。

### 機能

- 輝度に基づく一つのソフトニーで発光源を抽出し、光源の色比を保ちます。HDRの値を1で切りません。
- 幅と高さを独立に変えられる正規化した楕円PSFを使います。係数は、プラグインが実際に実行するピラミッドに合わせて校正しています。
- ガンマ、彩度、二色の着色はグローにだけ掛かります。グローと元画像の不透明度は別々に調整でき、グローのみの出力では対応するアルファも出力します。
- GPUではWindowsでCUDAとOpenCL、macOSでMetalを使います。CPU経路も同じフィルターと同じ演算で描画します。
- マルチフレームレンダリングに対応します。
- パラメータ名は英語、日本語、簡体字中国語、韓国語で表示します。

### パラメータ

| グループ | パラメータ | 範囲 | 既定値 |
| --- | --- | --- | --- |
| 状態 | GPU アクセラレーション（表示のみ） | 有効／準備完了／利用不可。有効時はフレームワークとデバイス名も表示 | — |
| 基本 | 強度 | 0〜8 | 1 |
| | サイズ（エネルギー90%の半径、px） | 0〜512 | 64 |
| | しきい値 | 0〜1600%（スライダーは0〜100%） | 25% |
| | しきい値の柔らかさ | 0〜100% | 50% |
| 形状 | 幅／高さ | 0〜400% | 100% |
| | 減衰 | 0〜100 | 50 |
| 色・階調 | ガンマ | 0.1〜3 | 1 |
| | 彩度 | 0〜200% | 100% |
| | 着色の強さ | 0〜100% | 0% |
| | カラー 1／カラー 2 | 色 | 暖白／白 |
| | 輝度の色空間 | 自動、Rec.709 / sRGB、Display P3、ACEScg | 自動 |
| 出力 | グローの不透明度／元画像の不透明度 | 0〜100% | 100% |
| | グローのみ | オン／オフ | オフ |

### 色管理

HaloGlowは、After Effectsが渡す作業用のRGB値をそのまま処理し、伝達関数を推測して変換することはしません。線形光のグローとして正しく働くのは、Linearize Working Spaceや線形のOCIO作業色空間などで、ホストが線形の作業値を渡す場合に限ります。「輝度の色空間」は輝度の計算に使う原色を選ぶ項目です。「自動」はmain threadで読み取った情報を使うbest-effortの値で（After Effects 25.1以降）、プロジェクトの色設定を変えた後は古い値が残ることがあります。確実さが必要なときは色空間を明示してください。作業色空間を判別できない場合、「自動」はRec.709 / sRGBの係数を使います。

### ビルド

このリポジトリをAfter Effects SDKの`Examples`フォルダの中に置きます。

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

macOSでは`cmake -S . -B build/Mac -G Xcode`を使います。CUDA、OpenCL、Metalは自動で検出し、`-DHG_FORCE_CPU_ONLY=ON`を付けるとCPUだけのプラグインになります。ビルドしたプラグインは`dist/<プラットフォーム>/<構成>/`に出力されます。

### テスト

`hg_core_tests`は抽出、階調、DCゲイン、タイル分割、PSFの形状と連続性を確かめます。`hg_gpu_parity`はCUDAとOpenCLを、`hg_metal_parity`はMetalを、それぞれCPUの参照実装と比べます。

### 0.1.0の既知の制限

- 係数表は暫定で、異方性と全位相の形状試験（仕様書§12.1、§12.1a）は終わっていません。
- After Effects上での動作確認と性能目標の測定はまだ行っていません。
- GPUメモリはレンダーごとに確保しています。プール方式の確保と共有メモリを使う局所フィルターは今後の課題です。
- DirectXでのレンダーはCPU経路に切り替わります。

### ライセンス

MITです。[LICENSE](LICENSE)を参照してください。

---

## 中文

HaloGlow 是一款采用 MIT 许可证的 Adobe After Effects 光晕效果。它不使用单一的大高斯模糊，而是用由紧凑核心、中距离扩散和延伸很远的微弱光晕三部分组成的点扩散函数（PSF）来扩散图像的明亮区域。设计规格见 [`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)（日语）。

### 功能

- 基于亮度的单一软拐点提取，保持光源的颜色比例；HDR 值不会在 1 处截断。
- 宽度和高度可独立调整的归一化椭圆 PSF，其系数针对插件实际运行的金字塔进行校准。
- 伽马、饱和度和双色着色只作用于光晕；光晕不透明度与源图像不透明度分别调整；“仅光晕”输出带有对应的 Alpha。
- GPU 渲染在 Windows 上使用 CUDA 和 OpenCL，在 macOS 上使用 Metal；CPU 路径使用相同的滤波器和相同的运算。
- 支持多帧渲染。
- 参数名称提供英语、日语、简体中文和韩语。

### 参数

| 分组 | 参数 | 范围 | 默认值 |
| --- | --- | --- | --- |
| 状态 | GPU 加速（只读） | 已启用 / 已就绪 / 不可用；启用时显示框架和设备名称 | — |
| 基本 | 强度 | 0–8 | 1 |
| | 大小（90% 能量半径，px） | 0–512 | 64 |
| | 阈值 | 0–1600%（滑块 0–100%） | 25% |
| | 阈值柔和度 | 0–100% | 50% |
| 形状 | 宽度 / 高度 | 0–400% | 100% |
| | 衰减 | 0–100 | 50 |
| 颜色与色调 | 伽马 | 0.1–3 | 1 |
| | 饱和度 | 0–200% | 100% |
| | 着色强度 | 0–100% | 0% |
| | 颜色 1 / 颜色 2 | 颜色 | 暖白 / 白 |
| | 亮度色彩空间 | 自动、Rec.709 / sRGB、Display P3、ACEScg | 自动 |
| 输出 | 光晕不透明度 / 源图像不透明度 | 0–100% | 100% |
| | 仅光晕 | 开 / 关 | 关 |

### 色彩管理

HaloGlow 直接处理 After Effects 提供的工作空间 RGB 值，不会猜测或解码传递函数。只有当宿主提供线性工作值时（例如启用“线性化工作空间”或使用线性 OCIO 工作色彩空间），才能保证得到线性光光晕。“亮度色彩空间”用于选择计算亮度时使用的原色；“自动”是在主线程上读取的尽力而为的值（After Effects 25.1 及以上），更改项目色彩设置后可能仍保留旧值——需要确定结果时请明确选择色彩空间。无法识别工作色彩空间时，“自动”使用 Rec.709 / sRGB 系数。

### 构建

将本仓库放在 After Effects SDK 的 `Examples` 文件夹中。

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

在 macOS 上使用 `cmake -S . -B build/Mac -G Xcode`。CUDA、OpenCL 和 Metal 会自动检测；加上 `-DHG_FORCE_CPU_ONLY=ON` 可构建仅 CPU 的插件。插件输出到 `dist/<平台>/<配置>/`。

### 测试

`hg_core_tests` 检查提取、色调、直流增益、分块渲染、PSF 形状与连续性；`hg_gpu_parity` 将 CUDA 和 OpenCL 与 CPU 参考实现进行比较；`hg_metal_parity` 将 Metal 与 CPU 参考实现进行比较。

### 0.1.0 的已知限制

- 系数表为暂定版本；各向异性和全相位的形状测试（规格 §12.1、§12.1a）尚未完成。
- 尚未在 After Effects 中验证，性能目标尚未测量。
- GPU 内存按每次渲染分配；池化分配器和使用共享内存的局部滤波器仍待实现。
- DirectX 渲染会切换到 CPU 路径。

### 许可证

MIT，详见 [LICENSE](LICENSE)。

---

## 한국어

HaloGlow는 MIT 라이선스의 Adobe After Effects 글로우 이펙트입니다. 하나의 큰 가우시안을 쓰는 대신, 좁은 코어, 중간 거리 확산, 멀리까지 약하게 이어지는 헤일로의 세 성분으로 이루어진 점확산함수(PSF)로 이미지의 밝은 부분을 퍼뜨립니다. 설계 사양은 [`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)(일본어)에 있습니다.

### 기능

- 휘도 기반의 단일 소프트 니로 발광원을 추출하여 광원의 색 비율을 유지하며, HDR 값을 1에서 자르지 않습니다.
- 너비와 높이를 독립적으로 조정할 수 있는 정규화된 타원 PSF를 사용하며, 계수는 플러그인이 실제로 실행하는 피라미드에 맞추어 보정합니다.
- 감마, 채도, 2색 착색은 글로우에만 적용됩니다. 글로우 불투명도와 원본 불투명도를 따로 조정할 수 있으며, 글로우만 출력할 때는 대응하는 알파도 출력합니다.
- GPU 렌더링은 Windows에서 CUDA와 OpenCL, macOS에서 Metal을 사용하며, CPU 경로도 같은 필터와 같은 연산으로 렌더링합니다.
- 다중 프레임 렌더링을 지원합니다.
- 매개변수 이름은 영어, 일본어, 중국어 간체, 한국어로 표시됩니다.

### 매개변수

| 그룹 | 매개변수 | 범위 | 기본값 |
| --- | --- | --- | --- |
| 상태 | GPU 가속(표시 전용) | 활성 / 준비 완료 / 사용 불가, 활성 시 프레임워크와 장치 이름 표시 | — |
| 기본 | 강도 | 0–8 | 1 |
| | 크기(에너지 90% 반경, px) | 0–512 | 64 |
| | 임계값 | 0–1600%(슬라이더 0–100%) | 25% |
| | 임계값 부드러움 | 0–100% | 50% |
| 모양 | 너비 / 높이 | 0–400% | 100% |
| | 감쇠 | 0–100 | 50 |
| 색상 및 톤 | 감마 | 0.1–3 | 1 |
| | 채도 | 0–200% | 100% |
| | 색상 강도 | 0–100% | 0% |
| | 색상 1 / 색상 2 | 색상 | 따뜻한 흰색 / 흰색 |
| | 휘도 색 공간 | 자동, Rec.709 / sRGB, Display P3, ACEScg | 자동 |
| 출력 | 글로우 불투명도 / 원본 불투명도 | 0–100% | 100% |
| | 글로우만 | 켜기 / 끄기 | 끄기 |

### 색 관리

HaloGlow는 After Effects가 전달하는 작업 공간 RGB 값을 그대로 처리하며, 전달 함수를 추측하여 변환하지 않습니다. 선형광 글로우가 보장되는 것은 작업 공간 선형화나 선형 OCIO 작업 색 공간 등으로 호스트가 선형 작업 값을 전달하는 경우뿐입니다. "휘도 색 공간"은 휘도 계산에 쓰는 원색을 고르는 항목입니다. "자동"은 메인 스레드에서 읽은 정보를 사용하는 최선 노력 방식의 값이며(After Effects 25.1 이상), 프로젝트 색 설정을 바꾼 뒤에는 이전 값이 남아 있을 수 있습니다. 결과가 확실해야 할 때는 색 공간을 직접 선택하십시오. 작업 색 공간을 식별할 수 없으면 "자동"은 Rec.709 / sRGB 계수를 사용합니다.

### 빌드

이 저장소를 After Effects SDK의 `Examples` 폴더 안에 둡니다.

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

macOS에서는 `cmake -S . -B build/Mac -G Xcode`를 사용합니다. CUDA, OpenCL, Metal은 자동으로 감지되며, `-DHG_FORCE_CPU_ONLY=ON`을 지정하면 CPU 전용 플러그인을 빌드합니다. 플러그인은 `dist/<플랫폼>/<구성>/`에 출력됩니다.

### 테스트

`hg_core_tests`는 추출, 톤, DC 이득, 타일 렌더링, PSF 모양과 연속성을 확인합니다. `hg_gpu_parity`는 CUDA와 OpenCL을, `hg_metal_parity`는 Metal을 각각 CPU 참조 구현과 비교합니다.

### 0.1.0의 알려진 제한

- 계수 표는 잠정 버전이며, 이방성 및 전체 위상 모양 시험(사양 §12.1, §12.1a)은 아직 끝나지 않았습니다.
- After Effects에서의 동작 확인과 성능 목표 측정은 아직 하지 않았습니다.
- GPU 메모리는 렌더링마다 할당합니다. 풀 방식 할당과 공유 메모리를 쓰는 로컬 필터는 향후 과제입니다.
- DirectX 렌더링은 CPU 경로로 전환됩니다.

### 라이선스

MIT입니다. [LICENSE](LICENSE)를 참조하십시오.
