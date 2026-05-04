# godot-spout
[WIP] Godot Spout Plugin via GDExtension  
The current functionality supports transmission in Forward+ rendering (D3D12) mode only.  

# Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" -A x64 ..
cmake --build . --config Release --target install
```

# License
MIT License

# Known issues
- Dynamic switching to output viewport texture results in an empty texture.
