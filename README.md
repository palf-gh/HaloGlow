# HaloGlow

[English](#english) · [日本語](#日本語) · [中文](#中文) · [한국어](#한국어)

**Current plug-in version: 1.0.0 (release)**

---

## English

HaloGlow is an MIT-licensed glow effect for Adobe After Effects. From coarse
to fine, it screens the low-pass of each scale and spreads a halo around light
sources without a hard edge. The design is specified in
[`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md) (Japanese).

### Features

- Each scale's low-pass is screened. Size is how far the glow spreads, and
  Falloff is the gain ratio from scale to scale. A flat white area does not
  rise above a brightness of 1, and HDR values above 1 are left as they are.
- Width and Height scale Size on each axis.
- Gamma, Saturation and two-colour tint on the glow only; separate Glow and
  Source Opacity; Glow Only output with a matching alpha.
- GPU rendering through CUDA and OpenCL (Windows) and Metal (macOS), with a CPU
  path that uses the same filter and the same arithmetic.
- Multi-Frame Rendering support.
- English, Japanese, Simplified Chinese and Korean parameter names.
- When a newer public release exists, that is shown.

### Parameters

| Group | Parameter | Range | Default |
| --- | --- | --- | --- |
| Status | GPU Acceleration (read-only) | Active / ready / unavailable, with framework and device | — |
| Basic | Intensity | 0–8 | 1 |
| | Size | 0–512 px | 64 |
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
guesses or decodes a transfer function. A linear working space supplies linear
input values, but the final composite uses a nonlinear highlight roll-off. **Luminance Space**
selects the primaries used for luminance; *Auto* is a best-effort value read on
the main thread (After Effects 25.1 or later) and may be stale after the
project colour settings change — choose the space explicitly when it matters.
Without a recognised working space Auto falls back to Rec.709 / sRGB.

### Installation

On Windows, copy `HaloGlow.aex` into the After Effects Plug-ins folder. On
macOS, unzip the archive and copy the complete `HaloGlow.plugin` bundle.
Restart After Effects. The effect is under **Effect → Stylize → HaloGlow**.

Windows:

```text
C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\
```

macOS:

```text
/Applications/Adobe After Effects <version>/Plug-ins/
```

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

### Updates

When a newer public release exists, that is shown.

To publish one, set the version in `Source/HG_Target.h` and create a public
GitHub Release on `palf-gh/HaloGlow` tagged `vMAJOR.MINOR.BUG` (for example
`v1.1.0`). It must not be a draft or a pre-release. Attach the archive to
that release. The plug-in reads `releases/latest` and carries no access token.
Quit After Effects, replace the plug-in, and start After Effects again.

### Tests

`hg_core_tests` checks extraction, tone, DC gain, tiled rendering, PSF shape
and continuity. `hg_gpu_parity` compares CUDA and OpenCL, and `hg_metal_parity`
compares Metal, with the CPU reference implementation.

### Release notes

**1.0.0** (2026-11-06)

First public release.

- Each scale's low-pass is screened. A flat white area does not rise above a brightness of 1. HDR values above 1 are left as they are.
- Windows renders on CPU, CUDA and OpenCL. macOS renders on CPU and Metal. A DirectX render uses the CPU path.
- 8-bit, 16-bit and 32-bit float, with Multi-Frame Rendering.
- Parameter names in English, Japanese, Simplified Chinese and Korean.
- When a newer public release exists, that is shown.
- Release tests passed for the Windows CPU, CUDA and OpenCL paths, and for the macOS CPU and Metal paths.

### Remaining limitations

- GPU memory is allocated per render. The pooled allocator and the shared-memory local filter are open items.
- DirectX rendering uses the CPU path.

### Licence

MIT — see [LICENSE](LICENSE).

---

## 日本語

**現在のプラグインバージョン: 1.0.0（リリース）**

HaloGlowはAdobe After Effects用のMITライセンスのグローエフェクトです。粗い段から細かい段へ、各段のローパスをスクリーン合成し、光源の周りへ縁のない光の裾を広げます。設計は[`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)にあります。

### 機能

- 各段のローパスをスクリーン合成します。サイズは光の広がる距離、減衰は段ごとの強さの比です。一面が白い領域は明るさ1を超えず、1を超えるHDRの値は切りません。
- 幅と高さは、サイズに対する軸ごとの倍率です。
- ガンマ、彩度、二色の着色はグローにだけ掛かります。グローと元画像の不透明度は別々に調整でき、グローのみの出力では対応するアルファも出力します。
- GPUではWindowsでCUDAとOpenCL、macOSでMetalを使います。CPU経路も同じフィルターと同じ演算で描画します。
- マルチフレームレンダリングに対応します。
- パラメータ名は英語、日本語、簡体字中国語、韓国語で表示します。
- 新しい公開版があれば、その旨を表示します。

### パラメータ

| グループ | パラメータ | 範囲 | 既定値 |
| --- | --- | --- | --- |
| 状態 | GPU アクセラレーション（表示のみ） | 有効／準備完了／利用不可。有効時はフレームワークとデバイス名も表示 | — |
| 基本 | 強度 | 0〜8 | 1 |
| | サイズ | 0〜512 px | 64 |
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

HaloGlowはAfter Effectsが渡す作業用のRGB値をそのまま処理し、伝達関数を推測して変換することはしません。線形の作業色空間では線形の入力値を扱いますが、最終合成には非線形のハイライト抑制を使います。「輝度の色空間」は輝度の計算に使う原色を選ぶ項目です。「自動」はメインスレッドで読んだ、取れる範囲の値です（After Effects 25.1以降）。プロジェクトの色設定を変えたあと、古い値が残ることがあります。確実さが必要なときは、色空間を明示してください。作業色空間を判別できない場合、「自動」はRec.709 / sRGBの係数を使います。

### 導入

Windowsでは `HaloGlow.aex` を、After Effects の Plug-ins フォルダへコピーします。macOSでは zip を展開し、`HaloGlow.plugin` を中身を崩さずにコピーします。コピーしたあとは、After Effects を再起動します。エフェクトは **エフェクト → スタイライズ → HaloGlow** にあります。

Windows:

```text
C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\
```

macOS:

```text
/Applications/Adobe After Effects <version>/Plug-ins/
```

### ビルド

このリポジトリをAfter Effects SDKの`Examples`フォルダの中に置きます。

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

macOSでは`cmake -S . -B build/Mac -G Xcode`を使います。CUDA、OpenCL、Metalは自動で検出し、`-DHG_FORCE_CPU_ONLY=ON`を付けるとCPUだけのプラグインになります。ビルドしたプラグインは`dist/<プラットフォーム>/<構成>/`に出力されます。

### 更新

新しい公開版があれば、その旨を表示します。

公開するときは `Source/HG_Target.h` の版番号に合わせ、公開リポジトリ `palf-gh/HaloGlow` にタグ `vMAJOR.MINOR.BUG`（例: `v1.1.0`）の GitHub Release を作ります。下書きとプレリリースは対象外です。配布物はその Release に添付します。プラグインは `releases/latest` をトークンなしで読みます。After Effects を終了し、プラグインを置き換えてから、もう一度起動します。

### テスト

`hg_core_tests`は抽出、階調、DCゲイン、タイル分割、PSFの形状と連続性を確かめます。`hg_gpu_parity`はCUDAとOpenCLを、`hg_metal_parity`はMetalを、それぞれCPUの参照実装と比べます。

### リリースノート

**1.0.0**（2026-11-06）

初回の公開版です。

- 各段のローパスをスクリーン合成します。一面が白い領域は明るさ1を超えません。1を超えるHDRの値は切りません。
- WindowsはCPU、CUDA、OpenCLで描画します。macOSはCPUとMetalで描画します。DirectXでのレンダーはCPU経路です。
- 8 bit、16 bit、32 bit floatと、マルチフレームレンダリングに対応します。
- パラメータ名は英語、日本語、簡体字中国語、韓国語です。
- 新しい公開版があれば、その旨を表示します。
- WindowsのReleaseビルドでは、CPU、CUDA、OpenCLの一致試験を通しています。macOSのReleaseビルドでは、CPUとMetalの一致試験を通しています。

### 残している制限

- GPUメモリはレンダーごとに確保します。プール方式の確保と、共有メモリを使う局所フィルターは今後の課題です。
- DirectXでのレンダーはCPU経路です。

### ライセンス

MITです。[LICENSE](LICENSE)を参照してください。

---

## 中文

**当前插件版本：1.0.0（发布版）**

HaloGlow 是一款采用 MIT 许可证的 Adobe After Effects 光晕效果。从粗到细，以滤色方式合成每一级的低通，在光源周围展开没有硬边的光晕。设计规格见 [`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)（日语）。

### 功能

- 以滤色方式合成每一级低通。大小是光晕展开的距离，衰减是各级强度之比。一片白色的区域亮度不超过 1，超过 1 的 HDR 值保持原样。
- 宽度和高度是大小在各自轴上的倍率。
- 伽马、饱和度和双色着色只作用于光晕；光晕不透明度与源图像不透明度分别调整；“仅光晕”输出带有对应的 Alpha。
- GPU 渲染在 Windows 上使用 CUDA 和 OpenCL，在 macOS 上使用 Metal；CPU 路径使用相同的滤波器和相同的运算。
- 支持多帧渲染。
- 参数名称提供英语、日语、简体中文和韩语。
- 有新的正式版本时，会显示这一情况。

### 参数

| 分组 | 参数 | 范围 | 默认值 |
| --- | --- | --- | --- |
| 状态 | GPU 加速（只读） | 已启用 / 已就绪 / 不可用；启用时显示框架和设备名称 | — |
| 基本 | 强度 | 0–8 | 1 |
| | 大小 | 0–512 px | 64 |
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

HaloGlow 直接处理 After Effects 提供的工作空间 RGB 值，不会猜测或解码传递函数。线性工作色彩空间提供线性输入值，但最终合成使用非线性高光衰减。“亮度色彩空间”用于选择计算亮度时使用的原色；“自动”是在主线程上读取的尽力而为的值（After Effects 25.1 及以上），更改项目色彩设置后可能仍保留旧值——需要确定结果时请明确选择色彩空间。无法识别工作色彩空间时，“自动”使用 Rec.709 / sRGB 系数。

### 安装

在 Windows 上，将 `HaloGlow.aex` 复制到 After Effects 的 Plug-ins 文件夹。在 macOS 上，解压后复制完整的 `HaloGlow.plugin`。复制后重新启动 After Effects。效果位于 **效果 → 风格化 → HaloGlow**。

Windows:

```text
C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\
```

macOS:

```text
/Applications/Adobe After Effects <version>/Plug-ins/
```

### 构建

将本仓库放在 After Effects SDK 的 `Examples` 文件夹中。

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

在 macOS 上使用 `cmake -S . -B build/Mac -G Xcode`。CUDA、OpenCL 和 Metal 会自动检测；加上 `-DHG_FORCE_CPU_ONLY=ON` 可构建仅 CPU 的插件。插件输出到 `dist/<平台>/<配置>/`。

### 更新

有新的正式版本时，会显示这一情况。

发布时，使 `Source/HG_Target.h` 中的版本与公开仓库 `palf-gh/HaloGlow` 上的 GitHub Release 标签 `vMAJOR.MINOR.BUG`（例如 `v1.1.0`）一致。草稿和预发布不算。将压缩包附在该 Release 上。插件不带访问令牌，只读取 `releases/latest`。退出 After Effects，替换插件后再启动。

### 测试

`hg_core_tests` 检查提取、色调、直流增益、分块渲染、PSF 形状与连续性；`hg_gpu_parity` 将 CUDA 和 OpenCL 与 CPU 参考实现进行比较；`hg_metal_parity` 将 Metal 与 CPU 参考实现进行比较。

### 发布说明

**1.0.0**（2026-11-06）

首次公开发布。

- 以滤色方式合成每一级低通。一片白色的区域亮度不超过 1。超过 1 的 HDR 值保持原样。
- Windows 使用 CPU、CUDA 和 OpenCL 渲染。macOS 使用 CPU 和 Metal 渲染。DirectX 渲染使用 CPU 路径。
- 支持 8 bit、16 bit、32 bit float 和多帧渲染。
- 参数名称提供英语、日语、简体中文和韩语。
- 有新的正式版本时，会显示这一情况。
- Windows Release 已通过 CPU、CUDA 与 OpenCL 的一致性测试；macOS Release 已通过 CPU 与 Metal 的一致性测试。

### 仍存在的限制

- GPU 内存按每次渲染分配。池化分配器和使用共享内存的局部滤波器仍待实现。
- DirectX 渲染使用 CPU 路径。

### 许可证

MIT，详见 [LICENSE](LICENSE)。

---

## 한국어

**현재 플러그인 버전: 1.0.0(릴리스)**

HaloGlow는 MIT 라이선스의 Adobe After Effects 글로우 이펙트입니다. 거친 단계에서 가는 단계로, 각 단계의 저역 통과를 스크린으로 합성해 광원 주위에 가장자리가 갈라지지 않는 빛의 자락을 펼칩니다. 설계 사양은 [`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)(일본어)에 있습니다.

### 기능

- 각 단계의 저역 통과를 스크린으로 합성합니다. 크기는 빛이 퍼지는 거리이고, 감쇠는 단계마다의 세기 비율입니다. 하얗게 펼쳐진 영역은 밝기 1을 넘지 않으며, 1을 넘는 HDR 값은 자르지 않습니다.
- 너비와 높이는 크기에 대한 축마다의 배율입니다.
- 감마, 채도, 2색 착색은 글로우에만 적용됩니다. 글로우 불투명도와 원본 불투명도를 따로 조정할 수 있으며, 글로우만 출력할 때는 대응하는 알파도 출력합니다.
- GPU 렌더링은 Windows에서 CUDA와 OpenCL, macOS에서 Metal을 사용하며, CPU 경로도 같은 필터와 같은 연산으로 렌더링합니다.
- 다중 프레임 렌더링을 지원합니다.
- 매개변수 이름은 영어, 일본어, 중국어 간체, 한국어로 표시됩니다.
- 새 공개 버전이 있으면 그 사실을 표시합니다.

### 매개변수

| 그룹 | 매개변수 | 범위 | 기본값 |
| --- | --- | --- | --- |
| 상태 | GPU 가속(표시 전용) | 활성 / 준비 완료 / 사용 불가, 활성 시 프레임워크와 장치 이름 표시 | — |
| 기본 | 강도 | 0–8 | 1 |
| | 크기 | 0–512 px | 64 |
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

HaloGlow는 After Effects가 전달하는 작업 공간 RGB 값을 그대로 처리하며, 전달 함수를 추측하여 변환하지 않습니다. 선형 작업 색 공간은 선형 입력값을 제공하지만 최종 합성은 비선형 하이라이트 감쇠를 사용합니다. "휘도 색 공간"은 휘도 계산에 쓰는 원색을 고르는 항목입니다. "자동"은 메인 스레드에서 읽은 정보를 사용하는 최선 노력 방식의 값이며(After Effects 25.1 이상), 프로젝트 색 설정을 바꾼 뒤에는 이전 값이 남아 있을 수 있습니다. 결과가 확실해야 할 때는 색 공간을 직접 선택하십시오. 작업 색 공간을 식별할 수 없으면 "자동"은 Rec.709 / sRGB 계수를 사용합니다.

### 설치

Windows에서는 `HaloGlow.aex`를 After Effects의 Plug-ins 폴더에 복사합니다. macOS에서는 zip을 풀고 `HaloGlow.plugin` 번들 전체를 복사합니다. 복사한 뒤 After Effects를 다시 시작합니다. 이펙트는 **효과 → 스타일화 → HaloGlow**에 있습니다.

Windows:

```text
C:\Program Files\Adobe\Adobe After Effects <version>\Support Files\Plug-ins\
```

macOS:

```text
/Applications/Adobe After Effects <version>/Plug-ins/
```

### 빌드

이 저장소를 After Effects SDK의 `Examples` 폴더 안에 둡니다.

```bash
cmake -S . -B build/Win
cmake --build build/Win --config Release
ctest --test-dir build/Win -C Release
```

macOS에서는 `cmake -S . -B build/Mac -G Xcode`를 사용합니다. CUDA, OpenCL, Metal은 자동으로 감지되며, `-DHG_FORCE_CPU_ONLY=ON`을 지정하면 CPU 전용 플러그인을 빌드합니다. 플러그인은 `dist/<플랫폼>/<구성>/`에 출력됩니다.

### 업데이트

새 공개 버전이 있으면 그 사실을 표시합니다.

배포할 때는 `Source/HG_Target.h`의 버전과 맞춰, 공개 저장소 `palf-gh/HaloGlow`에 태그 `vMAJOR.MINOR.BUG`(예: `v1.1.0`)인 GitHub Release를 만듭니다. 초안과 사전 릴리스는 대상이 아닙니다. 배포 파일은 그 Release에 첨부합니다. 플러그인은 접근 토큰 없이 `releases/latest`를 읽습니다. After Effects를 종료하고 플러그인을 바꾼 뒤 다시 실행합니다.

### 테스트

`hg_core_tests`는 추출, 톤, DC 이득, 타일 렌더링, PSF 모양과 연속성을 확인합니다. `hg_gpu_parity`는 CUDA와 OpenCL을, `hg_metal_parity`는 Metal을 각각 CPU 참조 구현과 비교합니다.

### 릴리스 노트

**1.0.0**（2026-11-06）

첫 공개 버전입니다.

- 각 단계의 저역 통과를 스크린으로 합성합니다. 하얗게 펼쳐진 영역은 밝기 1을 넘지 않습니다. 1을 넘는 HDR 값은 자르지 않습니다.
- Windows는 CPU, CUDA, OpenCL로 렌더링합니다. macOS는 CPU와 Metal로 렌더링합니다. DirectX 렌더링은 CPU 경로입니다.
- 8 bit, 16 bit, 32 bit float와 다중 프레임 렌더링을 지원합니다.
- 매개변수 이름은 영어, 일본어, 중국어 간체, 한국어입니다.
- 새 공개 버전이 있으면 그 사실을 표시합니다.
- Windows Release에서는 CPU, CUDA, OpenCL 일치 시험을 통과했고, macOS Release에서는 CPU와 Metal 일치 시험을 통과했습니다.

### 남아 있는 제한

- GPU 메모리는 렌더링마다 할당합니다. 풀 방식 할당과 공유 메모리를 쓰는 로컬 필터는 향후 과제입니다.
- DirectX 렌더링은 CPU 경로입니다.

### 라이선스

MIT입니다. [LICENSE](LICENSE)를 참조하십시오.
