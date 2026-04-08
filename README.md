# stuffdump — Native IL2CPP Info Dumper for Stuff: Horror

## What it dumps

Once you're in a room, writes `stuffdump.txt` to `/sdcard/` containing:

- `MapItemGenerator.baseItems[].PathToItem` — the actual prefab path strings used for spawning
- `SpawnItemZone.items[].itemName` — all registered spawnable item names across all zones
- `ItemManager.curentPath` — paths of every item currently alive in the scene
- `EntitySceneManager.entityColections[].EntityResourcesName` — mob/entity resource names
- Confirmed field offsets for ShamcerManager, ShotPungManager, ClientGameManager

## Build requirements

- Android NDK r25+ (download from developer.android.com/ndk)
- CMake 3.22+

```bash
export ANDROID_NDK_HOME=/path/to/your/ndk
chmod +x build.sh
./build.sh
```

Output: `build_arm64/libstuffdump.so`

## Installation

Place `libstuffdump.so` in the same native mods folder as your other `.so` mods
(alongside `libil2cpp.so`, typically in the game's `lib/arm64-v8a/` folder or
wherever your mod loader loads from).

## Reading the output

After launching the game and joining a room:

```bash
adb pull /sdcard/stuffdump.txt .
cat stuffdump.txt
```

The file is also tried at:
- `/sdcard/Android/data/com.stuffhorror/files/stuffdump.txt`
- `/data/local/tmp/stuffdump.txt`

Check Android logcat for `stuffdump` tag to see progress:
```bash
adb logcat -s stuffdump
```

## Notes

- Waits up to 2 minutes for you to join a room before dumping
- If classes aren't found in scene it will say so — run in a room with items spawned
- The `x19` register note in the output confirms how Frida should read `this` pointer
  in Interceptor hooks for this build
