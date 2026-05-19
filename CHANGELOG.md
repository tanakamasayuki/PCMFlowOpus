# Changelog / 変更履歴

## Unreleased
- (EN) Repository scaffolding: README / SPEC / library metadata / src & tests skeleton. No decoder or encoder implementation yet.
- (EN) Upstream sync: L1 (notify only) wired up — `tools/sync_opus.py` plus weekly `.github/workflows/upstream-check.yml`.
- (EN) Release workflow copied verbatim from parent PCMFlow.
- (JA) リポジトリ雛形整備: README / SPEC / ライブラリメタデータ / src・tests スケルトン。デコーダ・エンコーダ本体は未実装。
- (JA) 上流同期: L1(通知のみ)を実装 — `tools/sync_opus.py` と週次 `.github/workflows/upstream-check.yml`。
- (JA) リリースワークフローを親 PCMFlow からそのまま流用。
- (EN) Added `doc/sibling_library_brief.md` — codec-agnostic handoff memo for spinning up sibling codec libraries under the same conventions.
- (JA) `doc/sibling_library_brief.md` を追加 — 同じ規約で兄弟コーデックライブラリを立ち上げるための、コーデック非依存の横展開作業メモ。
