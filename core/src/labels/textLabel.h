#pragma once

#include "labels/label.h"

#include <glm/glm.hpp>

namespace Tangram {

class TextLabels;
class TextStyle;

struct GlyphQuad {
    size_t atlas;
    struct {
        glm::i16vec2 pos;
        glm::u16vec2 uv;
    } quad[4];
};

using TextRange = std::array<Range, 3>;

struct TextVertex {
    glm::i16vec2 pos;
    glm::u16vec2 uv;
    struct State {
        uint32_t color;
        uint32_t stroke;
        uint16_t alpha;
        uint16_t scale;
    } state;

    const static float position_scale;
    const static float alpha_scale;
};

class TextLabel : public Label {

public:

    struct FontVertexAttributes {
        uint32_t fill;
        uint32_t stroke;
        uint8_t fontScale;
    };

    TextLabel(Label::Transform _transform, Type _type, Label::Options _options,
              TextLabel::FontVertexAttributes _attrib,
              glm::vec2 _dim, TextLabels& _labels, TextRange _textRanges,
              TextLabelProperty::Align _preferedAlignment,
              size_t _anchorPoint = 0, const std::vector<glm::vec2>& _line = {});

    bool updateScreenTransform(const glm::mat4& _mvp, const glm::vec2& _screenSize,
                               bool _drawAllLabels, ScreenTransform& _transform) override;

    TextRange& textRanges() {
        return m_textRanges;
    }


    void obbs(const ScreenTransform& _transform, std::vector<OBB>& _obbs,
              Range& _range, bool _append) override;


protected:

    void pushTransform(ScreenTransform& _transform) override;

private:

    void applyAnchor(LabelProperty::Anchor _anchor) override;

    // Back-pointer to owning container
    const TextLabels& m_textLabels;

    // first vertex and count in m_textLabels quads (left,right,center)
    TextRange m_textRanges;

    // TextRange currently used for drawing
    int m_textRangeIndex;

    FontVertexAttributes m_fontAttrib;

    // The text LAbel prefered alignment
    TextLabelProperty::Align m_preferedAlignment;

    size_t m_anchorPoint;
    std::vector<glm::vec2> m_line;

    LineSampler m_sampler;
};

}
