
// Selection pass-specific rendering
#if defined(TANGRAM_FEATURE_SELECTION) && defined(TANGRAM_VERTEX_SHADER)
    if (a_selection_color.rgb == vec3(0.0)) {
        // Discard the vertex
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    v_selection_color = a_selection_color;
#endif
