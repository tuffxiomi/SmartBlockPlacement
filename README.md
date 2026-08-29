# SmartBlockPlacement

Client-side Levi LaunchDroid native mod for Minecraft Bedrock.

SmartBlockPlacement is inspired by Accurate Block Placement: while a valid block-use interaction has recently succeeded, the mod watches the current block target and automatically retries placement when the aim moves to a new block instead of requiring precise right-click timing.

## Implementation basis

The project was built from the behavior and interfaces present in the supplied BedrockTools source, but it does **not** bundle BedrockTools source files or headers.

Only these signature patterns are used at runtime:

- `NormalTick`
- `GameModeUseItemOn`
- `SurvivalModeUseItemOn`
- `LevelGetHitResult`
- `BlockSourceGetBlock`

The signatures are resolved dynamically against `libminecraftpe.so`; no fixed code addresses are hard-coded.

The relevant offsets learned from the supplied source are:

- `Actor::mLevel = 464`
- `Actor::mDimension = 448`
- `Dimension::mBlockSource = 208`
- `HitResult::mType = 24`
- `HitResult::mPos = 44`

## Build

GitHub Actions targets Android `arm64-v8a` with NDK `r28c`, xmake, and Preloader Android. The workflow produces `SmartBlockPlacement.levipack` as an artifact.

## Notes

This implementation is intentionally conservative: it only becomes automatic after a successful block-hit interaction, only reacts to a new block target, and stops after a short idle timeout. This reduces accidental continued placement after the use input is released.
