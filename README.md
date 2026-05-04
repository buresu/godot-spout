# godot-spout
Godot Spout Plugin via GDExtension  
Only Forward+ rendering (D3D12) is supported.  

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
