# godot-spout
Godot Spout Addon via GDExtension  
Forward+ D3D12 and Vulkan are supported for input/output.  
Compatibility/OpenGL3 is optionally supported for output only.  

## Build
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" ..
cmake --build . --config [Debug|Release] --target install
```

## Build with Vulkan
```
scoop install vulkan
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" .. -DGODOT_SPOUT_ENABLE_VULKAN=ON
cmake --build . --config [Debug|Release] --target install
```

## Build with OpenGL
```
git clone --recursive https://github.com/buresu/godot-spout.git
cd godot-spout
mkdir build && cd build
cmake -G "Visual Studio 18 2026" .. -DGODOT_SPOUT_ENABLE_OPENGL=ON
cmake --build . --config [Debug|Release] --target install
```

## License
MIT License

## Known issues
- Dynamic switching to output viewport texture results in an empty texture.
- OpenGL3 input is not supported yet because can not get native input texture handle from godot.
