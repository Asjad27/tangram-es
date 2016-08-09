#include "labels/spriteLabel.h"

#include "gl/dynamicQuadMesh.h"
#include "style/pointStyle.h"
#include "util/geom.h"
#include "platform.h"

namespace Tangram {

using namespace LabelProperty;

const float SpriteVertex::position_scale = 4.0f;
const float SpriteVertex::alpha_scale = 65535.0f;
const float SpriteVertex::texture_scale = 65535.0f;

SpriteLabel::SpriteLabel(Label::Transform _transform, glm::vec2 _size, Label::Options _options,
                         float _extrudeScale, SpriteLabels& _labels, size_t _labelsPos)
    : Label(_transform, _size, Label::Type::point, _options),
      m_labels(_labels),
      m_labelsPos(_labelsPos),
      m_extrudeScale(_extrudeScale) {

    applyAnchor(m_options.anchors[0]);
}

void SpriteLabel::applyAnchor(LabelProperty::Anchor _anchor) {

    m_anchor = LabelProperty::anchorDirection(_anchor) * m_dim * 0.5f;
}

bool SpriteLabel::updateScreenTransform(const glm::mat4& _mvp, const glm::vec2& _screenSize,
                                        bool _testVisibility, ScreenTransform& _transform) {

    bool clipped = false;
    glm::vec2 p0 = m_transform.modelPosition1;

    glm::vec2 screenPosition = worldToScreenSpace(_mvp, glm::vec4(p0, 0.0, 1.0),
                                                  _screenSize, clipped);

    if (_testVisibility && clipped) { return false; }

    screenPosition += m_anchor;
    screenPosition.x += m_options.offset.x;
    screenPosition.y -= m_options.offset.y;

    m_transform.state.screenPos = screenPosition;

    return true;
}

void SpriteLabel::obbs(const ScreenTransform& _transform, std::vector<OBB>& _obbs,
                       Range& _range, bool _append) {

    if (_append) { _range.start = int(_obbs.size()); }
    _range.length = 1;

    glm::vec2 sp = m_transform.state.screenPos;
    glm::vec2 dim = m_dim + glm::vec2(m_extrudeScale * 2.f); // * _zoomFract);

    if (m_occludedLastFrame) { dim += Label::activation_distance_threshold; }

    auto obb = OBB(sp, m_transform.state.rotation, dim.x, dim.y);

    if (_append) {
        _obbs.push_back(obb);
    } else {
        _obbs[_range.start] = obb;
    }
}

void SpriteLabel::pushTransform(ScreenTransform& _transform) {

    if (!visibleState()) { return; }

    // TODO
    // if (a_extrude.x != 0.0) {
    //     float dz = u_map_position.z - abs(u_tile_origin.z);
    //     vertex_pos.xy += clamp(dz, 0.0, 1.0) * UNPACK_EXTRUDE(a_extrude.xy);
    // }

    auto& quad = m_labels.quads[m_labelsPos];

    SpriteVertex::State state {
        quad.color,
        uint16_t(m_transform.state.alpha * SpriteVertex::alpha_scale),
        0,
    };

    auto& style = m_labels.m_style;

    auto* quadVertices = style.getMesh()->pushQuad();

    glm::i16vec2 sp = glm::i16vec2(m_transform.state.screenPos * SpriteVertex::position_scale);

    for (int i = 0; i < 4; i++) {
        SpriteVertex& v = quadVertices[i];
        v.pos = sp + quad.quad[i].pos;
        v.uv = quad.quad[i].uv;
        //v.extrude = quad.quad[i].extrude;
        v.state = state;
    }
}

}
