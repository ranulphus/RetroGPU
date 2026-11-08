# Half-Life on DOS-GL (plan halflife-dos.md, Part H): the forks of Xash3D FWGS
# (engine, renderer, menu) and hlsdk-portable (Half-Life's game code), each
# with a DJGPP target, pinned by commit. tools/halflife/build.sh reads these
# lines. Their submodules come at the commits the forks' gitlinks pin, with
# the forks' patch series (scripts/djgpp/patches) applied.
XASH_URL     := https://github.com/ranulphus/xash3d-fwgs-dos
XASH_COMMIT  := f5f14437915cf43af21b6868c3288c42345c439a
HLSDK_URL    := https://github.com/ranulphus/hlsdk-portable-dos
HLSDK_COMMIT := 79298b3e6a39444c800a99f0090bbec8296147fc
