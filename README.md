# UnrealEnginePatch

An editor-only Unreal Engine plugin that applies and reverts versioned, line-level patches to stock engine source files. Patches are defined as JSON files and can be tied to specific plugins so they are automatically applied or removed when those plugins are enabled or disabled.

## How it works

- On editor close (`Deinitialize`), the subsystem reads the current project descriptor and for each patch:
  - If the associated plugin is **enabled** and the patch is **not applied** → applies it.
  - If the associated plugin is **disabled** and the patch is **applied** → removes it.
- Patches are idempotent: applying twice or removing when not applied is safe.
- Applied patches are wrapped in marker comments so they can be cleanly reverted without manually tracking what changed.

The **Engine Patch Manager** panel (`Tools > Engine Patch Manager`) shows the status of all discovered patches and lets you apply or revert them individually or all at once.

## Patch file location

Each plugin that requires engine patches places its JSON files under:

```
Plugins/<YourPlugin>/EnginePatch/*.json
```

Example:

```
Plugins/Recall/EnginePatch/mass-entity-handle-remove-transient.json
Plugins/Recall/EnginePatch/mass-processing-render-phase.json
```

`UnrealEnginePatch` scans `Plugins/Recall/EnginePatch/` automatically. To add discovery of another plugin's patches, extend `UUnrealEnginePatchSubsystem::Initialize()`.

## JSON format

One file per logical patch. A patch can touch multiple engine files and supports multiple operations per file.

```json
{
  "patchId": "my-patch-id",
  "description": "Human-readable description shown in the panel",
  "plugin": "MyPlugin",
  "versions": [
    {
      "engineVersion": "5.8",
      "files": [
        {
          "file": "Runtime/SomeModule/Public/SomeHeader.h",
          "operations": [
            {
              "id": "op-unique-id",
              "line": 42,
              "remove": [
                "\told line to replace;"
              ],
              "add": [
                "\tnew line A;",
                "\tnew line B;"
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

### Fields

| Field | Required | Description |
|---|---|---|
| `patchId` | Yes | Unique identifier used in marker comments. Must be stable across versions. |
| `description` | Yes | Shown in the Engine Patch Manager panel. |
| `plugin` | No | Plugin name (matches `.uplugin` filename without extension). When set, the patch auto-applies/reverts based on whether the plugin is enabled. Omit for unconditional patches. |
| `versions[]` | Yes | One entry per engine version. The patcher only applies the entry matching the running engine. |
| `engineVersion` | Yes | String in `MAJOR.MINOR` format, e.g. `"5.8"`. |
| `files[]` | Yes | Engine source files to patch. Paths are relative to `<EngineDir>/Source/`. |
| `operations[]` | Yes | Ordered list of operations for this file. Applied in order; line numbers are relative to the **original unpatched** file. |
| `id` | Yes | Unique operation ID within the patch. Used in marker comments. |
| `line` | Yes | 1-based line number in the **original** file where the operation starts. |
| `remove` | No | Lines to remove starting at `line`. Must match the file content exactly (including tabs/spaces). Omit or leave empty for pure insertions. |
| `add` | No | Lines to insert in place of the removed lines (or at `line` if `remove` is empty). |

### Pure insertion example

```json
{
  "id": "add-my-method",
  "line": 214,
  "remove": [],
  "add": [
    "\tMYMODULE_API void MyMethod(float DeltaTime);",
    "\tMYMODULE_API void MyOtherMethod();"
  ]
}
```

### Replace example

```json
{
  "id": "enable-parallel",
  "line": 14,
  "remove": [
    "#define MASS_DO_PARALLEL !UE_SERVER"
  ],
  "add": [
    "#define MASS_DO_PARALLEL 1"
  ]
}
```

## How patches are stored in the engine file

When applied, each operation is wrapped in marker comments:

```cpp
// @@PATCH_BEGIN(my-patch-id::op-unique-id)
// @@REMOVED: \told line to replace;
\tnew line A;
\tnew line B;
// @@PATCH_END(my-patch-id::op-unique-id)
```

- `@@REMOVED:` lines store the original content so the patcher can restore it on unpatch without the JSON needing to re-specify what was there.
- Markers use the `patchId::opId` pair, so multiple patches on the same file never collide.

## Multiple operations on the same file

When a patch has several operations targeting the same file, all are applied in a single read/write pass. Line numbers in the JSON always refer to the **original unpatched** file; the patcher tracks the running offset internally so later operations land at the correct position.

## Engine version mismatch

If no `versions` entry matches the running engine, the patch status shows **N/A** and it is silently skipped — no error, no modification.

## Adding patches for a new plugin

1. Create `Plugins/<YourPlugin>/EnginePatch/` in your plugin directory.
2. Add one JSON file per logical change following the format above.
3. Set `"plugin": "<YourPlugin>"` in each file so auto-sync works.
4. If your plugin directory is not `Recall`, add a discovery path in `UUnrealEnginePatchSubsystem::Initialize()`:
   ```cpp
   PatchDirectories.Add(FPaths::ConvertRelativePathToFull(
       FPaths::ProjectPluginsDir() / TEXT("<YourPlugin>/EnginePatch")));
   ```
5. Build and open the editor — patches appear in `Tools > Engine Patch Manager`.
