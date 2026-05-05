# godot-spout
Godot Spout Addon via GDExtension  
Only Forward+ rendering (D3D12) is supported.  

# Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" -A [x64|Win32] ..
cmake --build . --config [Debug|Release] --target install
```

# License
MIT License

# Known issues
- Dynamic switching to output viewport texture results in an empty texture.
