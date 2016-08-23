#include "label.h"

#include "util/geom.h"
#include "glm/gtx/rotate_vector.hpp"
#include "tangram.h"
#include "platform.h"

namespace Tangram {

const float Label::activation_distance_threshold = 2;

Label::Label(Label::Transform _transform, glm::vec2 _size, Type _type, Options _options)
    : m_state(State::none),
      m_type(_type),
      m_transform(_transform),
      m_dim(_size),
      m_options(_options) {

    if (!m_options.collide || m_type == Type::debug) {
        enterState(State::visible, 1.0);
    } else {
        m_transform.state.alpha = 0.0;
    }

    m_occludedLastFrame = false;
    m_occluded = false;
    m_parent = nullptr;
    m_anchorIndex = 0;
}

Label::~Label() {}

bool Label::updateScreenTransform(const glm::mat4& _mvp, float _tileResolution, float _tileInverseScale,
                                  const glm::vec2& _screenSize, bool _drawAllLabels) {

    glm::vec2 screenPosition;
    bool clipped = false;

    switch (m_type) {
        case Type::debug:
        case Type::point:
        {
            glm::vec2 p0 = m_transform.modelPosition1;

            if (m_options.flat) {
                glm::vec2 halfMetersDimension = m_dim * _tileResolution * 0.5f;

                glm::vec2 sw = p0 - halfMetersDimension * _tileInverseScale;
                glm::vec2 se = p0 + glm::vec2(halfMetersDimension.x, -halfMetersDimension.y)
                             * _tileInverseScale;
                glm::vec2 nw = p0 + glm::vec2(-halfMetersDimension.x, halfMetersDimension.y)
                             * _tileInverseScale;
                glm::vec2 ne = p0 + halfMetersDimension * _tileInverseScale;

                // Rotate in clockwise order on the ground plane
                if (m_options.angle != 0.f) {
                    glm::vec2 rotation(cos(DEG_TO_RAD * m_options.angle),
                                       sin(DEG_TO_RAD * m_options.angle));

                    sw = rotateBy(sw, rotation);
                    se = rotateBy(se, rotation);
                    nw = rotateBy(nw, rotation);
                    ne = rotateBy(ne, rotation);
                }

                screenBillboard[0] = worldToScreenSpace(_mvp, glm::vec4(sw, 0.0, 1.0),
                                                        _screenSize, clipped);
                if (clipped) { return false; }
                screenBillboard[1] = worldToScreenSpace(_mvp, glm::vec4(se, 0.0, 1.0),
                                                        _screenSize, clipped);
                if (clipped) { return false; }
                screenBillboard[3] = worldToScreenSpace(_mvp, glm::vec4(ne, 0.0, 1.0),
                                                        _screenSize, clipped);
                if (clipped) { return false; }
                screenBillboard[2] = worldToScreenSpace(_mvp, glm::vec4(nw, 0.0, 1.0),
                                                        _screenSize, clipped);
                if (clipped) { return false; }
            } else {
                screenPosition = worldToScreenSpace(_mvp, glm::vec4(p0, 0.0, 1.0),
                                                    _screenSize, clipped);
                if (clipped) { return false; }
            }

            m_transform.state.screenPos = screenPosition + m_options.offset;

            break;
        }
        case Type::line:
        {
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
            if (clipped) {
                return false;
            }

            float length = glm::length(ap2 - ap0);

            // default heuristic : allow label to be 30% wider than segment
            float minLength = m_dim.x * 0.7;

            if (!_drawAllLabels && length < minLength) {
                return false;
            }

            glm::vec2 p1 = glm::vec2(p2 + p0) * 0.5f;

            glm::vec2 ap1 = worldToScreenSpace(_mvp, glm::vec4(p1, 0.0, 1.0),
                                               _screenSize, clipped);

            // Keep screen position center at world center (less sliding in tilted view)
            screenPosition = ap1;

            glm::vec2 rotation = (ap0.x <= ap2.x ? ap2 - ap0 : ap0 - ap2) / length;
            rotation = glm::vec2{rotation.x, -rotation.y};

            m_transform.state.screenPos = screenPosition + rotateBy(m_options.offset, rotation);
            m_transform.state.rotation = rotation;

            break;
        }
    }

    return true;
}

void Label::setParent(Label& _parent, bool _definePriority) {
    m_parent = &_parent;

    if (_definePriority) {
        m_options.priority = _parent.options().priority + 0.5f;
    }

    applyAnchor(m_options.anchors[m_anchorIndex]);
}

bool Label::offViewport(const glm::vec2& _screenSize) {
    const auto& quad = m_obb.getQuad();

    for (int i = 0; i < 4; ++i) {
        if (m_options.flat) {
            if (screenBillboard[i].x < _screenSize.x && screenBillboard[i].x > 0 &&
                screenBillboard[i].y < _screenSize.y && screenBillboard[i].y > 0) {
                return false;
            }
        } else {
            if (quad[i].x < _screenSize.x && quad[i].x > 0 &&
                quad[i].y < _screenSize.y && quad[i].y > 0) {
                return false;
            }
        }
    }

    return true;
}

bool Label::canOcclude() {
    if (!m_options.collide) {
        return false;
    }

    int occludeFlags = (State::visible |
                        State::none |
                        State::skip_transition |
                        State::fading_in |
                        State::fading_out |
                        State::sleep |
                        State::out_of_screen |
                        State::dead);

    return (occludeFlags & m_state) && !(m_type == Type::debug);
}

bool Label::visibleState() const {
    int visibleFlags = (State::visible |
                        State::fading_in |
                        State::fading_out |
                        State::skip_transition);

    return (visibleFlags & m_state);
}

void Label::skipTransitions() {
    enterState(State::skip_transition, 0.0);
}

glm::vec2 Label::center() const {
    return m_obb.getCentroid();
}

void Label::enterState(const State& _state, float _alpha) {
    if (m_state == State::dead) { return; }

    m_state = _state;
    setAlpha(_alpha);

    if (m_state == State::sleep) {
        // Reset anchor fallback index
        m_anchorIndex = 0;
    }

}

void Label::setAlpha(float _alpha) {
    m_transform.state.alpha = CLAMP(_alpha, 0.0, 1.0);
}

void Label::resetState() {

    if (m_state == State::dead) { return; }

    m_occludedLastFrame = false;
    m_occluded = false;
    m_anchorIndex = 0;
    enterState(State::none, 0.0);
}

void Label::print() const {
    LOG("Label - %p", this);
    LOG("\tm_occludedLastFrame: %d", m_occludedLastFrame);
    LOG("\tm_occluded: %d", m_occluded);
    std::string state;
    switch (m_state) {
        case State::none: state = "none"; break;
        case State::visible: state = "visible"; break;
        case State::fading_in: state = "fading_in"; break;
        case State::fading_out: state = "fading_out"; break;
        case State::skip_transition: state = "skip_transition"; break;
        case State::sleep: state = "sleep"; break;
        case State::dead: state = "dead"; break;
        case State::out_of_screen: state = "out_of_screen"; break;
    }
    LOG("\tm_state: %s", state.c_str());
    LOG("\tm_anchorIndex: %d", m_anchorIndex);
    LOG("\tscreenPos: %f/%f", m_transform.state.screenPos.x, m_transform.state.screenPos.y);

}

bool Label::nextAnchor() {
    int index = m_anchorIndex;
    setAnchorIndex((index + 1) % m_options.anchors.count);

    return m_anchorIndex != index;
}

bool Label::setAnchorIndex(int _index) {
    if (_index >= int(m_options.anchors.count) || _index < 0) {
        return false;
    }
    m_anchorIndex = _index;

    applyAnchor(m_options.anchors[m_anchorIndex]);

    return true;
}

bool Label::update(const glm::mat4& _mvp, float _tileResolution, float _tileInverseScale, const glm::vec2& _screenSize,
                   float _zoomFract, bool _drawAllLabels) {

    m_occludedLastFrame = m_occluded;
    m_occluded = false;

    if (m_state == State::dead) {
        if (_drawAllLabels) {
            m_occluded = true;
        } else {
            return false;
        }
    }

    bool ruleSatisfied = updateScreenTransform(_mvp, _tileResolution, _tileInverseScale,
                                               _screenSize, _drawAllLabels);

    // one of the label rules has not been satisfied
    if (!ruleSatisfied) {
        enterState(State::sleep, 0.0);
        return false;
    }

    // update the view-space bouding box
    updateBBoxes(_zoomFract);

    // checks whether the label is out of the viewport
    if (offViewport(_screenSize)) {
        enterState(State::out_of_screen, 0.0);
        if (m_occludedLastFrame) {
            m_occluded = true;
            return false;
        }
    } else if (m_state == State::out_of_screen) {
        enterState(State::sleep, 0.0);
    }

    return true;
}

bool Label::evalState(float _dt) {

#ifdef DEBUG
    if (Tangram::getDebugFlag(DebugFlags::draw_all_labels)) {
        enterState(State::visible, 1.0);
        return false;
    }
#endif

    switch (m_state) {
        case State::none:
        case State::sleep:
            if (m_occluded) {
                enterState(State::sleep, 0.0);
                return false;
            }
            if (m_options.showTransition.time == 0.f) {
                enterState(State::visible, 1.0);
                return false;
            }
            m_fade.reset(true, m_options.showTransition.ease,
                         m_options.showTransition.time);
            enterState(State::fading_in, 0.0);
            return true;

        case State::visible:
            if (!m_occluded) { return false; }

            if (m_options.hideTransition.time == 0.f) {
                enterState(State::sleep, 0.0);
                return false;
            }
            m_fade.reset(false, m_options.hideTransition.ease,
                         m_options.hideTransition.time);

            enterState(State::fading_out, 1.0);
            return true;

        case State::fading_in:
            if (m_occluded) {
                enterState(State::sleep, 0.0);
                return false;
            }
            setAlpha(m_fade.update(_dt));
            if (m_fade.isFinished()) {
                enterState(State::visible, 1.0);
                return false;
            }
            return true;

        case State::fading_out:
            // if (!m_occluded) {
            //     enterState(State::fading_in, m_transform.state.alpha);
            //     m_fade.reset(false, m_options.hideTransition.ease,
            //                  m_options.showTransition.time);
            //     return true;
            // }
            setAlpha(m_fade.update(_dt));
            if (m_fade.isFinished()) {
                enterState(State::sleep, 0.0);
                return false;
            }
            return true;

        case State::skip_transition:
            if (m_occluded) {
                enterState(State::sleep, 0.0);
            } else {
                enterState(State::visible, 1.0);
            }
            return true;

        case State::dead:
        case State::out_of_screen:
            break;
    }

    return false;
}

}
