#pragma once

#include <Godot.hpp>
#include <Texture.hpp>

class SpoutReceiver;

namespace godot {

class SpoutTexture : public Texture {
    GODOT_CLASS(SpoutTexture, Texture)
public:
    void _init();
    static void _register_methods();

    SpoutTexture();
    virtual ~SpoutTexture();

    virtual int64_t get_width() const { return _width; }
	virtual int64_t get_height() const { return _height; }

    virtual RID get_rid() const { return _texture; }

    virtual bool has_alpha() const { return true; }

    virtual void set_flags(const int64_t flags) { _flags = flags; }
    virtual int64_t get_flags() const { return _flags; }

    /*
    virtual int get_width() const = 0;
	virtual int get_height() const = 0;
	virtual Size2 get_size() const;
	virtual RID get_rid() const = 0;

	virtual bool is_pixel_opaque(int p_x, int p_y) const;

	virtual bool has_alpha() const = 0;

	virtual void set_flags(uint32_t p_flags) = 0;
	virtual uint32_t get_flags() const = 0;
    */

    /*
    int64_t get_width() const;
	int64_t get_height() const;
	Vector2 get_size() const;
	bool has_alpha() const;
	void set_flags(const int64_t flags);
	int64_t get_flags() const;
	void draw(const RID canvas_item, const Vector2 position, const Color modulate = Color(1,1,1,1), const bool transpose = false, const Ref<Texture> normal_map = nullptr) const;
	void draw_rect(const RID canvas_item, const Rect2 rect, const bool tile, const Color modulate = Color(1,1,1,1), const bool transpose = false, const Ref<Texture> normal_map = nullptr) const;
	void draw_rect_region(const RID canvas_item, const Rect2 rect, const Rect2 src_rect, const Color modulate = Color(1,1,1,1), const bool transpose = false, const Ref<Texture> normal_map = nullptr, const bool clip_uv = true) const;
	Ref<Image> get_data() const;
    */

protected:
    bool _is_initialized() const;
    bool _create_receiver(const String &name);
    void _release_receiver();
    void _receive_texture();

private:
    // Texture
    RID _texture;
    int64_t _width = 0, _height = 0;
    uint32_t _flags;

    // Spout
    SpoutReceiver *_receiver = nullptr;
    String _channel_name;
};

}
