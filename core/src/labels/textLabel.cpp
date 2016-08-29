#include "textLabel.h"
#include "textLabels.h"
#include "style/textStyle.h"
#include "text/fontContext.h"
#include "gl/dynamicQuadMesh.h"
#include "util/geom.h"
#include "util/lineSampler.h"

namespace Tangram {

using namespace LabelProperty;
using namespace TextLabelProperty;

const float TextVertex::position_scale = 4.0f;
const float TextVertex::alpha_scale = 65535.0f;

TextLabel::TextLabel(Label::Transform _transform, Type _type, Label::Options _options,
                     TextLabel::FontVertexAttributes _attrib,
                     glm::vec2 _dim,  TextLabels& _labels, TextRange _textRanges,
                     Align _preferedAlignment, size_t _anchorPoint, const std::vector<glm::vec2>& _line)
    : Label(_transform, _dim, _type, _options),
      m_textLabels(_labels),
      m_textRanges(_textRanges),
      m_fontAttrib(_attrib),
      m_preferedAlignment(_preferedAlignment),
      m_anchorPoint(_anchorPoint),
      m_line(_line) {

    m_options.repeatDistance = 0;

    applyAnchor(m_options.anchors[0]);
}

void TextLabel::applyAnchor(Anchor _anchor) {

    if (m_preferedAlignment == Align::none) {
        Align newAlignment = alignFromAnchor(_anchor);
        m_textRangeIndex = int(newAlignment);
    } else {
        m_textRangeIndex = int(m_preferedAlignment);
    }

    if (m_textRanges[m_textRangeIndex].length == 0) {
        m_textRangeIndex = 0;
    }

    glm::vec2 offset = m_dim;
    if (m_parent) { offset += m_parent->dimension(); }

    m_anchor = LabelProperty::anchorDirection(_anchor) * offset * 0.5f;

}

bool TextLabel::updateScreenTransform(const glm::mat4& _mvp, const glm::vec2& _screenSize,
                                      bool _drawAllLabels, ScreenTransform& _transform) {

    bool clipped = false;

    if (m_type == Type::point || m_type == Type::debug) {
        glm::vec2 p0 = m_transform.modelPosition1;

        glm::vec2 screenPosition = worldToScreenSpace(_mvp, glm::vec4(p0, 0.0, 1.0),
                                                      _screenSize, clipped);

        if (clipped) { return false; }

        m_transform.state.screenPos = screenPosition + m_options.offset;

        return true;
    }

    if (m_type == Type::line) {

        glm::vec2 rotation = {1, 0};

        // project label position from mercator world space to screen
        // coordinates
        glm::vec2 p0 = m_transform.modelPosition1;
        glm::vec2 p2 = m_transform.modelPosition2;

        glm::vec2 ap0 = worldToScreenSpace(_mvp, glm::vec4(p0, 0.0, 1.0),
                                           _screenSize, clipped);
        glm::vec2 ap2 = worldToScreenSpace(_mvp, glm::vec4(p2, 0.0, 1.0),
                                           _screenSize, clipped);

        // check whether the label is behind the camera using the
        // perspective division factor
        if (_drawAllLabels && clipped) {
            return false;
        }

        float length = glm::length(ap2 - ap0);

        // default heuristic : allow label to be 30% wider than segment
        float minLength = m_dim.x * 0.7;

        if (_drawAllLabels && length < minLength) {
            return false;
        }

        glm::vec2 p1 = glm::vec2(p2 + p0) * 0.5f;

        // Keep screen position center at world center (less sliding in tilted view)
        glm::vec2 screenPosition = worldToScreenSpace(_mvp, glm::vec4(p1, 0.0, 1.0),
                                                      _screenSize, clipped);


        rotation = (ap0.x <= ap2.x ? ap2 - ap0 : ap0 - ap2) / length;

        m_transform.state.screenPos = screenPosition + rotateBy(m_options.offset, rotation);
        m_transform.state.rotation = rotation;

        return true;
    }

    /* Label::Type::curved */

    LineSampler<ScreenTransform> sampler { _transform };

    bool inside = false;

    for (auto& p : m_line) {
        glm::vec2 sp = worldToScreenSpace(_mvp, glm::vec4(p, 0.0, 1.0),
                                          _screenSize, clipped);

        if (clipped) { return false; }

        sampler.add(sp);

        if (!inside){
            if ((sp.x >= 0 && sp.x <= _screenSize.x) ||
                (sp.y >= 0 && sp.y <= _screenSize.y)) {
                inside = true;
            }
        }
    }

    float length = sampler.sumLength();

    if (!inside || length < m_dim.x) {
        return false;
    }

    float center = sampler.point(m_anchorPoint).length;

    if (center - m_dim.x * 0.5f < 0 || center + m_dim.x * 0.5f > length) {
        return false;
    }

    return true;
}

void TextLabel::obbs(const ScreenTransform& _transform, std::vector<OBB>& _obbs,
                     Range& _range, bool _append) {

    if (m_type == Label::Type::curved) {
        // Dont update curved label ranges for now (This isn't used anyway)
        _append = true;
    }

    if (_append) { _range.start = int(_obbs.size()); }

    glm::vec2 dim = m_dim - m_options.buffer;

    if (m_occludedLastFrame) { dim += Label::activation_distance_threshold; }

    // FIXME: Only for testing
    if (state() == State::dead) { dim -= 4; }

    if (m_type == Label::Type::curved) {
        float width = dim.x;
        LineSampler<ScreenTransform> sampler { _transform };

        auto center = sampler.point(m_anchorPoint).length;
        auto start = center - width * 0.5f;

        glm::vec2 p1, p2, rotation;
        sampler.sample(start, p1, rotation);

        float prevLength = start;

        int count = 0;
        for (size_t i = sampler.curSegment()+1; i < _transform.size(); i++) {

            float currLength = sampler.point(i).length;
            float segmentLength = currLength - prevLength;
            count++;

            if (start + width > currLength) {
                p2 = sampler.point(i).coord;

                rotation = sampler.segmentDirection(i-1);
                _obbs.push_back({(p1 + p2) * 0.5f, rotation, segmentLength, dim.y});
                prevLength = currLength;
                p1 = p2;

            } else {

                segmentLength = (start + width) - prevLength;
                sampler.sample(start + width, p2, rotation);
                _obbs.push_back({(p1 + p2) * 0.5f, rotation, segmentLength, dim.y});
                break;
            }
        }
        _range.length = count;
    } else {

        auto obb = OBB(m_transform.state.screenPos + m_anchor,
                       glm::vec2{m_transform.state.rotation.x, -m_transform.state.rotation.y},
                       dim.x, dim.y);

        if (_append) {
            _obbs.push_back(obb);
        } else {
            _obbs[_range.start] = obb;
        }
        _range.length = 1;
    }
}

void TextLabel::pushTransform(ScreenTransform& _transform) {
    if (!visibleState()) { return; }

    glm::vec2 rotation = m_transform.state.rotation;
    bool rotate = (rotation.x != 1.f);

    TextVertex::State state {
        m_fontAttrib.fill,
        m_fontAttrib.stroke,
        uint16_t(m_transform.state.alpha * TextVertex::alpha_scale),
        uint16_t(m_fontAttrib.fontScale),
    };

    auto it = m_textLabels.quads.begin() + m_textRanges[m_textRangeIndex].start;
    auto end = it + m_textRanges[m_textRangeIndex].length;
    auto& style = m_textLabels.style;

    auto& meshes = style.getMeshes();
    if (m_type == Label::Type::curved) {
        LineSampler<ScreenTransform> sampler { _transform };

        float width = m_dim.x;

        if (sampler.sumLength() < width) { return; }

        float center = sampler.point(m_anchorPoint).length;

        glm::vec2 p1, p2;
        sampler.sample(center + it->quad[0].pos.x / TextVertex::position_scale, p1, rotation);
        // Check based on first charater whether labels needs to be flipped
        // sampler.sample(center + it->quad[2].pos.x, p2, rotation);
        sampler.sample(center + (end-1)->quad[2].pos.x / TextVertex::position_scale, p2, rotation);


        if (p1.x > p2.x) {
            sampler.reversePoints();
            center = sampler.sumLength() - center;
        }

        // if (center < width * 0.5f) {
        //     center = width * 0.5f;
        // } else if (sampler.sumLength() - center < width * 0.5f) {
        //     center = width * 0.5f;
        // }

        for (; it != end; ++it) {
            auto quad = *it;

            glm::vec2 origin = {(quad.quad[0].pos.x + quad.quad[2].pos.x) * 0.5f, 0 };
            glm::vec2 point, pa, pb, ra, rb;
            float px = origin.x / TextVertex::position_scale;

            if (!sampler.sample(center + px, point, rotation)) {
                // break;
            }
            bool ok1 = sampler.sample(center + px - 10, pa, ra);
            size_t s1 = sampler.curSegment();
            bool ok2 = sampler.sample(center + px + 10, pb, rb);
            size_t s2 = sampler.curSegment();


            if (s1 != s2 && ok1 && ok2) {
                auto da = glm::distance(point, pa);
                auto db = glm::distance(point, pb);
                float f = da / (da + db);
                rotation = glm::normalize(ra * f + rb * (1.0f - f));

                point =  (point + pa + pb) * 0.333333f;
            }

            point *= TextVertex::position_scale;
            rotation = {rotation.x, -rotation.y};

            auto* quadVertices = meshes[it->atlas]->pushQuad();

            for (int i = 0; i < 4; i++) {
                TextVertex& v = quadVertices[i];

                v.pos = glm::i16vec2{point + rotateBy(glm::vec2(quad.quad[i].pos) - origin, rotation)};

                v.uv = quad.quad[i].uv;
                v.state = state;
            }
        }
    } else {

        glm::vec2 screenPosition = m_transform.state.screenPos;
        screenPosition += m_anchor;
        glm::i16vec2 sp = glm::i16vec2(screenPosition * TextVertex::position_scale);

        for (; it != end; ++it) {
            auto quad = *it;
            auto* quadVertices = meshes[it->atlas]->pushQuad();

            for (int i = 0; i < 4; i++) {
                TextVertex& v = quadVertices[i];
                if (rotate) {
                    v.pos = sp + glm::i16vec2{rotateBy(quad.quad[i].pos, rotation)};
                } else {
                    v.pos = sp + quad.quad[i].pos;
                }
                v.uv = quad.quad[i].uv;
                v.state = state;
            }
        }
    }
}

TextLabels::~TextLabels() {
    style.context()->releaseAtlas(m_atlasRefs);
}

void TextLabels::setQuads(std::vector<GlyphQuad>&& _quads, std::bitset<FontContext::max_textures> _atlasRefs) {
    quads = std::move(_quads);
    m_atlasRefs = _atlasRefs;

}

}
