# godot-spout
[WIP] Godot Spout Plugin via GDExtension
The current functionality is limited to transmission in compatibility mode only.
I'm waiting for DirectX12 support.

# Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release --target install
```

# License
MIT License
