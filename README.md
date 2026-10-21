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

Tests: `hg_core_tests` (extraction, tone, DC gain, tiling, PSF shape and
continuity), `hg_gpu_parity` (CUDA / OpenCL against the CPU reference) and
`hg_metal_parity` (Metal against the CPU reference).

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

HaloGlowは、After Effects用のMITライセンスのグローエフェクトです。大きなGaussian一つで広げる代わりに、狭い芯・中距離の拡散・遠くまで弱く続くハローの三成分のPSFで明るい部分を広げます。設計は[`docs/HaloGlow_仕様書.md`](docs/HaloGlow_仕様書.md)にあります。

- GPU：Windowsは CUDA／OpenCL、macOSは Metal。GPUがない場合は同じフィルターのCPU経路で描画します。MFRに対応します。
- 表示名は英語・日本語・簡体字中国語・韓国語です。
- 色管理：ホストが渡す作業RGBをそのまま処理し、伝達関数を推測して変換することはしません。線形光のグローとして正しく動くのは、Linearize Working Spaceなどでホストが線形の作業値を渡す場合に限ります。Luminance SpaceのAutoは、main threadで取得できた情報を使う best-effort の機能です（AE 25.1以降）。色設定を変えた後は古い値が残ることがあるので、確実さが必要なときは色空間を明示してください。
- 0.1.0は開発版です。係数表は暫定で、AE上での動作確認と性能測定はまだ行っていません。

ビルド手順は英語の節を参照してください。

---

## 中文

HaloGlow 是一款采用 MIT 许可证的 After Effects 光晕效果。它使用由紧凑核心、中距离扩散和远距离微弱光晕组成的三段式点扩散函数，而不是单一的大高斯模糊。GPU 渲染支持 Windows 上的 CUDA / OpenCL 和 macOS 上的 Metal，并提供使用相同滤波器的 CPU 路径，支持多帧渲染。界面提供英语、日语、简体中文和韩语。

HaloGlow 直接处理宿主提供的工作空间 RGB 值，不会猜测或解码传递函数。只有当宿主提供线性工作值时（例如启用“线性化工作空间”），才能保证线性光光晕。0.1.0 为开发版本，系数表为暂定版本。

---

## 한국어

HaloGlow는 MIT 라이선스의 After Effects 글로우 이펙트입니다. 하나의 큰 가우시안 대신 좁은 코어, 중간 확산, 멀리까지 이어지는 약한 헤일로로 이루어진 세 성분 PSF로 밝은 부분을 퍼뜨립니다. GPU 렌더링은 Windows의 CUDA / OpenCL과 macOS의 Metal을 지원하며, 같은 필터를 쓰는 CPU 경로와 다중 프레임 렌더링을 지원합니다. 매개변수 이름은 영어, 일본어, 중국어 간체, 한국어로 제공됩니다.

HaloGlow는 호스트가 전달한 작업 공간 RGB 값을 그대로 처리하며 전달 함수를 추측하여 변환하지 않습니다. 선형 작업 값(예: 작업 공간 선형화)이 전달될 때에만 선형광 글로우가 보장됩니다. 0.1.0은 개발 버전이며 계수 표는 잠정 버전입니다.
