# GodotSpoutPlugin
[WIP] Godot Spout Plugin via GDNative

# Build
```
git clone https://github.com/buresu/GodotSpoutPlugin.git
cd GodotSpoutPlugin
git submodule update --init --recursive
mkdir build
cd build
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build . --config Release --target install
```

## Without GDNative build for developer
```
cmake -G "Visual Studio 16 2019" -A x64 .. -DBUILD_GDNATIVE=OFF
```

# License
MIT License

# Known Issues
- Can not instance GDNative Texture calss due to Texture class has pre virtual method.
- Can not send proxy texture such as ProxyTexture, ViewportTexture and AnimatedTexture.
